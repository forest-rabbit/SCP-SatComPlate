#!/usr/bin/env python3
"""G3: controlled F3 through the actual CLI; no formal workload or random fault search."""
import argparse
import csv
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import runpy

ROOT = Path(__file__).resolve().parents[5]
FIXTURE = ROOT / "contrib/satcompute/tests/fixtures/protection"


def rows(directory, name):
    with (directory / name).open() as stream:
        return list(csv.DictReader(stream))


def run(directory, mode, validation_trace=None, cutoff=False):
    args = ["satcompute", "--simulationDuration=15", "--faultMode=generate",
            "--faultEnableF1=0", "--faultEnableF2=0", "--faultEnableF3=1",
            "--faultF3Mode=controlled", "--faultF3Node=3", "--faultF3Time=1.4",
            "--compfrr-shadow=0", "--taskCompletionPolicy=report",
            "--constellationConfig=contrib/satcompute/tests/fixtures/constellation/connected-16.csv",
            f"--taskTrace={FIXTURE / 'fixed-four-profiles.json'}",
            f"--computeProfile={FIXTURE / 'fixed-compute.json'}",
            f"--protectionMode={mode}", "--backupStorageBytesPerNode=10000000000",
            "--fixedProtectionDelta=0.05", "--fixedProtectionBatchN=4",
            "--routingMode=global-capacity-aware-hrw", "--islBandwidthBps=10000000000",
            "--delayMode=fixed", "--fixedDelay=0.001", f"--outputDir={directory}"]
    if validation_trace is not None:
        args.remove("--faultMode=generate")
        args += ["--faultMode=validation-replay", f"--validationFaultTrace={validation_trace}"]
    if cutoff:
        trace = json.loads((FIXTURE / "fixed-four-profiles.json").read_text())
        for task in trace["tasks"]:
            task["arrival_time_ns"] = 1
        path = directory.parent / "cutoff-tasks.json"
        path.write_text(json.dumps(trace))
        args.remove("--faultMode=generate")
        args.remove("--simulationDuration=15")
        args.remove(f"--taskTrace={FIXTURE / 'fixed-four-profiles.json'}")
        args += ["--faultMode=none", "--simulationDuration=0.1", f"--taskTrace={path}"]
    result = subprocess.run([str(ROOT / "ns3"), "run", "--no-build", shlex.join(args)],
                            cwd=ROOT, text=True, capture_output=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr


def verify(root):
    for mode in ("off", "fixed", "repeat"):
        run(root / mode, "off" if mode == "off" else "fixed")
    off, fixed = root / "off", root / "fixed"
    assert not (off / "recovery-summary.csv").exists()
    old_tasks = rows(off, "task-summary.csv")
    tasks = rows(fixed, "task-summary.csv")
    assert len(tasks) == 4 and all(r["final_state"] == "FAILED" for r in old_tasks)
    assert [r["final_state"] for r in tasks] == ["COMPLETED", "FAILED", "FAILED", "FAILED"]
    recovery = rows(fixed, "recovery-summary.csv")
    assert len(recovery) == 1
    r = recovery[0]
    assert r["task_id"] == "1" and r["terminal_state"] == "COMPLETED"
    assert r["primary_node"] == "3" and r["recovery_node"] == "0"
    assert r["result_delivery_mode"] == "LOCAL" and r["result_transfer_id"] == ""
    assert r["result_bytes"] == "52428800" and r["logical_completion"] == "1"
    assert r["deadline_met"] == "1" and r["chosen_path"] in ("TAIL", "REMOTE_REDO")
    assert int(tasks[0]["compute_service_time_ns"]) <= int(tasks[0]["compute_stage_elapsed_time_ns"])
    transfers = rows(fixed, "transfer-summary.csv")
    assert {int(t["transfer_id"]) for t in transfers} == set(range(1, 9))
    assert next(t for t in transfers if t["transfer_id"] == "2")["terminal_state"] == "CANCELLED"
    events = rows(fixed, "protection-events.csv")
    assert {"FAULT_SNAPSHOT", "RECOVERY_ACCEPTED", "CATCHUP_REACHED", "RECOVERY_RESULT_COMPLETE"} <= {
        e["event"] for e in events if e["attempt_generation"] == "1"}
    for pool in rows(fixed, "protection-node-storage-summary.csv"):
        assert pool["used_bytes"] == pool["reserved_bytes"] == "0"
    for name in ("recovery-summary.csv", "recovery-events.csv", "task-events.csv", "task-summary.csv",
                 "transfer-summary.csv", "protection-transfers.csv"):
        assert (fixed / name).read_bytes() == (root / "repeat" / name).read_bytes(), name
    for mode in ("off", "fixed"):
        replay = root / f"{mode}-replay"
        run(replay, mode, root / "off/fault-trace.json")
        for name in ("task-events.csv", "task-summary.csv", "transfer-summary.csv", "fault-events.csv",
                     "compute-node-summary.csv", "fault-trace.json"):
            assert (replay / name).read_bytes() == (root / mode / name).read_bytes(), f"replay {mode}: {name}"
        if mode == "fixed":
            for name in ("recovery-summary.csv", "recovery-events.csv", "protection-task-summary.csv"):
                assert (replay / name).read_bytes() == (fixed / name).read_bytes(), name
    analyze = runpy.run_path(str(ROOT / "contrib/satcompute/tests/integration/regression/analyze-protection-accounting.py"))["analyze"]
    for mode in ("off", "fixed", "fixed-replay"):
        analyze(root / mode)
    run(root / "cutoff", "fixed", cutoff=True)
    cutoff = analyze(root / "cutoff")
    assert cutoff["summary"]["failed"] == 4 and cutoff["storage"]["quiescent"]
    assert {t["failure_reason"] for t in rows(root / "cutoff", "task-summary.csv")} == {"SIMULATION_ENDED"}
    return {"recovery_smoke": "passed", "tasks": 4, "off_completed": 0, "fixed_completed": 1,
            "path": r["chosen_path"], "local_result_bytes": int(r["result_bytes"]),
            "local_result_network_flows": 0, "deterministic": True}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", type=Path)
    args = parser.parse_args()
    if args.output_root:
        print(json.dumps(verify(args.output_root)))
    else:
        with tempfile.TemporaryDirectory(prefix="satcompute-recovery-smoke-") as directory:
            print(json.dumps(verify(Path(directory))))
