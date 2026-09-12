"""CB execution controls and shared frozen-scene access; no algorithm parameter overrides."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import threading
import time

BASE = Path(__file__).resolve().parents[1]
ROOT = BASE.parents[5]
REGRESSION = ROOT / "contrib/satcompute/tests/integration/regression"
SCENE_HELPER = runpy.run_path(str(REGRESSION / "run-final-scenario.py"))
SCENE = ROOT / SCENE_HELPER["SCENE"]
PROFILE = BASE / "calibration/frozen-mtbf-profile.json"
MODES = ("ffp", "lrl", "fa-ffp", "fa-lrl")
GROUPS = [(p, b) for p in MODES for b in ("recompute", "relocate")]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def rows(root, name):
    with (root / name).open() as stream:
        result = list(csv.DictReader(stream))
    require(all(None not in r and None not in r.values() for r in result), f"malformed {name}")
    return result


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def utc():
    return datetime.now(timezone.utc).isoformat()


def clean_head():
    require(not subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT),
            "execution requires a clean snapshot; runner never commits changes")
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def scene_identity():
    task_path = SCENE / "workload/task-trace.json"
    compute_path = SCENE / "compute/compute-profile.json"
    tasks = json.loads(task_path.read_text())["tasks"]
    nodes = json.loads(compute_path.read_text())["compute_nodes"]
    llm = [t for t in tasks if t["task_profile"] == "llm"]
    totals = dict(task_count=len(tasks), input_bytes=sum(t["input_bytes"] for t in tasks),
        output_bytes=sum(t["output_bytes"] for t in tasks), work_units=sum(t["compute_work_units"] for t in tasks),
        llm_work_units=sum(t["compute_work_units"] for t in llm),
        profile_counts=dict(Counter(t["task_profile"] for t in tasks)))
    require((totals["task_count"], totals["input_bytes"], totals["output_bytes"], totals["work_units"],
             totals["llm_work_units"]) == (800, 194119753287, 100166291859, 352513119, 61333200),
            "frozen scene totals changed")
    require(totals["profile_counts"] == dict(llm=80, **{"dense-image":240,"sparse-inference":240,"compression":240}),
            "frozen task composition changed")
    require(len(nodes) == 66 and {n["node_id"] for n in nodes} == set(range(66)) and
            all(n["compute_rate_work_units_per_second"] == 100000 for n in nodes), "frozen compute resource changed")
    require(all(t["compute_work_units"] % 400 == 0 for t in llm), "LLM whole-token WU changed")
    return dict(task_trace=str(task_path.relative_to(ROOT)), compute_profile=str(compute_path.relative_to(ROOT)),
                constellation=str((SCENE / "topology/constellation.csv").relative_to(ROOT)), **totals)


def flags(arguments):
    return dict(token[2:].split("=", 1) for token in arguments[1:])


def replace_flag(arguments, name, value):
    prefix = f"--{name}="
    result = [token for token in arguments if not token.startswith(prefix)]
    return [*result, prefix + str(value)]


def new_execution(stage):
    root = ROOT / "output/cb-sat-v2" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ-") + stage)
    root.mkdir(parents=True, exist_ok=False)
    return root


def execute(directory, arguments, head, identity):
    require(clean_head() == head, "execution HEAD changed")
    require(not directory.exists(), f"refusing to overwrite {directory}")
    directory.mkdir(parents=True)
    command = [str(ROOT / "ns3"), "run", "--no-build", shlex.join(arguments)]
    config = flags(arguments)
    record = dict(command=command, commit=head, worktree_dirty=False, started_utc=utc(), status="RUNNING",
                  seed=int(config["randomSeed"]), run=int(config["randomRun"]),
                  simulation_duration_s=float(config["simulationDuration"]),
                  fault_mode=config["faultMode"], protection_mode=config.get("protectionMode", "off"),
                  placement_mode=config.get("placementMode", "fa-ffp"),
                  remote_busy_recovery_policy=config.get("remoteBusyRecoveryPolicy", "relocate"),
                  probability_audit=config.get("faultProbabilityAudit", "0") == "1", **identity)
    write_json(directory / "execution.json", record)
    start = time.monotonic()
    with (directory / "run.log").open("w") as log:
        result = subprocess.run(["/usr/bin/time", "-v", "-o", str(directory / "time.txt"), *command],
                                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    outcome = dict(returncode=result.returncode, elapsed_wall_s=time.monotonic()-start,
                   ended_utc=utc(), status="FINISHED" if result.returncode == 0 else "FAILED")
    write_json(directory / "execution-result.json", outcome)
    require(result.returncode == 0, f"simulation failed; evidence retained at {directory}")
    require(clean_head() == head, "execution source changed during run")
    print(f"FINISHED {directory.name}: {outcome['elapsed_wall_s']:.1f} s", flush=True)
    return outcome


def batch(root, jobs, commands, head, identity):
    require(1 <= jobs <= 8, "jobs must be in 1..8")
    statuses = {name: "PENDING" for name in commands}
    status_lock = threading.Lock()
    write_json(root / "matrix-status.json", dict(commit=head, groups=statuses))
    def run(name, args):
        with status_lock:
            statuses[name] = "RUNNING"
            write_json(root / "matrix-status.json", dict(commit=head, groups=statuses))
        print(f"START {name}", flush=True)
        return execute(root / name, args, head, identity)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(run, name, args): name for name, args in commands.items()}
        failed = []
        for future in as_completed(futures):
            name = futures[future]
            try:
                future.result()
                status = "FINISHED"
            except Exception as error:
                status = "FAILED"
                failed.append(f"{name}: {error}")
            with status_lock:
                statuses[name] = status
                write_json(root / "matrix-status.json", dict(commit=head, groups=statuses, errors=failed))
    require(not failed, "; ".join(failed))
