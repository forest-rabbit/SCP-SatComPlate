#!/usr/bin/env python3
"""Bounded B-only task-120 sizing pilot, or check an existing full B run.

Keeps the actual model, seed/run, orbit, arrival, endpoints and F3 time. A
successful pilot is only a candidate: full-workload interference must be checked.
"""
import argparse
import csv
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[5]
MODULE = ROOT / "contrib/satcompute"
RUN = runpy.run_path(str(Path(__file__).with_name("run-final-scenario.py")))
sys.path.insert(0, str(MODULE / "tools/generation"))
from task_workload_model import image_budget

TARGET = 120
NODE = 62
F3_NS = 1027055770726


def rows(directory, name):
    with (directory / name).open() as stream:
        result = list(csv.DictReader(stream))
    if any(None in r or None in r.values() for r in result):
        raise ValueError(f"malformed CSV: {name}")
    return result


def assess(directory):
    tasks = rows(directory, "task-summary.csv")
    task = next(t for t in tasks if int(t["task_id"]) == TARGET)
    faults = json.loads((directory / "fault-trace.json").read_text())["faults"]
    prior = [f for f in faults if f["node_id"] == NODE and f["fault_type"] == "compute"
             and f["fault_occurred"] and f["start_time_ns"] <= F3_NS]
    impacts = [r for r in rows(directory, "recovery-summary.csv") if int(r["task_id"]) == TARGET]
    decisions = [r for r in rows(directory, "frequency-decisions.csv") if int(r["task_id"]) == TARGET]
    starts = [r for r in decisions if r["proposed_action"] == "START" and r["decision_committed"] == "1"]
    init = [r for r in rows(directory, "protection-events.csv") if int(r["task_id"]) == TARGET
            and r["event"] == "INIT_COST_COMMITTED" and r["attempt_generation"] == "0"]
    r = impacts[0] if len(impacts) == 1 else {}
    flags = dict(no_prior_node_f1_f2=not prior,
                 target_running_at_f3=r.get("fault_type") == "satellite" and int(r.get("fault_time_ns", -1)) == F3_NS,
                 beneficial_start=bool(starts) and bool(starts[0]["j_off"]) and
                     float(starts[0]["j_start"]) < float(starts[0]["j_off"]),
                 initialized_before_f3=bool(init) and int(init[0]["time_ns"]) < F3_NS and r.get("phase_at_fault") == "ON",
                 checkpoint_used=r.get("chosen_path") in ("TAIL", "REMOTE_REDO", "MIGRATE_TAIL", "MIGRATE_REDO")
                     and r.get("checkpoint_state_exists") == "1" and int(r.get("remote_work_units") or 0) > 0,
                 completed=task["final_state"] == "COMPLETED" and task["compute_deadline_met"] == "1")
    return dict(input_bytes=int(task["input_bytes"]), work_units=int(task["compute_work_units"]),
        task_count=len(tasks), eligible=all(flags.values()), checks=flags,
        compute_start_ns=int(task["compute_start_time_ns"]),
        start=starts[0] if starts else None, initialization_ns=int(init[0]["time_ns"]) if init else None,
        first_decision=decisions[0] if decisions else None,
        recovery=r, prior_node_compute_faults=prior, final_state=task["final_state"])


def pilot(output, input_bytes):
    if output.exists():
        raise ValueError("refusing to overwrite existing pilot")
    output.mkdir(parents=True)
    source = json.loads((ROOT / RUN["SCENE"] / "workload/task-trace.json").read_text())
    task = next(t.copy() for t in source["tasks"] if t["task_id"] == TARGET)
    budget = image_budget(task["task_profile"], input_bytes, str(TARGET))
    task.update(input_bytes=budget.input_bytes, output_bytes=budget.output_bytes, compute_work_units=budget.compute_work_units)
    trace = output / "task-120.json"
    trace.write_text(json.dumps({"tasks": [task]}, indent=2) + "\n")
    command = RUN["arguments"](output, protection_mode="compfrr", placement_mode="fa-ffp")
    command = [f"--taskTrace={trace}" if arg.startswith("--taskTrace=") else arg for arg in command]
    started = time.monotonic()
    with (output / "run.log").open("w") as log:
        process = subprocess.run([str(ROOT / "ns3"), "run", "--no-build", shlex.join(command)],
                                 cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    execution = dict(command=command, returncode=process.returncode, elapsed_wall_s=time.monotonic()-started,
                     purpose="B-only controlled task-size selection; not an unbiased performance sample")
    (output / "execution.json").write_text(json.dumps(execution, indent=2) + "\n")
    if process.returncode:
        raise RuntimeError(f"pilot failed: {output}/run.log")
    return assess(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--input-bytes", type=int, nargs="+")
    parser.add_argument("--inspect-run", type=Path)
    args = parser.parse_args()
    if args.inspect_run:
        if args.output_dir or args.input_bytes:
            parser.error("inspection never generates a replacement run")
        directory = args.inspect_run.resolve()
        result = assess(directory)
        identity = json.loads((directory / "execution.json").read_text())
        execution = json.loads((directory / "execution-result.json").read_text())
        result["checks"]["full_clean_b_run"] = (
            result["task_count"] == 800 and execution["returncode"] == 0
            and identity["simulation_duration_s"] == 1300 and not identity["worktree_dirty"]
            and identity["protection_mode"] == "compfrr" and identity["placement_mode"] in ("ffp", "fa-ffp")
            and identity["fault_mode"] == "generate" and identity["seed"] == 1 and identity["run"] == 11
            and not identity["audit"] and not identity["shadow"])
        result["eligible"] = all(result["checks"].values())
        print(json.dumps(result, indent=2))
        return 0 if result["eligible"] else 1
    if not args.output_dir or not args.input_bytes or any(not 50_000_000 <= n <= 1_000_000_000 for n in args.input_bytes):
        parser.error("pilot needs a new output directory and byte sizes within [50 MB, 1 GB]")
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("refusing to overwrite existing pilot batch")
    output.mkdir(parents=True)
    results = []
    for size in sorted(set(args.input_bytes)):
        directory = output / f"bytes-{size}"
        result = pilot(directory, size)
        results.append(dict(output=str(directory), **result))
        (output / "selection.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps({k: result[k] for k in ("input_bytes", "work_units", "eligible", "checks", "final_state")}), flush=True)
        if result["eligible"]:
            break
    return 0 if results[-1]["eligible"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
