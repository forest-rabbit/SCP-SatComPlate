#!/usr/bin/env python3
"""N5B-G2: four existing tasks, online generate wiring, no formal algorithm comparison."""
import argparse
import csv
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[5]
FIXTURE = ROOT / "contrib/satcompute/tests/fixtures/protection"


def rows(directory, name):
    with (directory / name).open() as stream:
        result = list(csv.DictReader(stream))
    assert all(None not in row for row in result), f"malformed CSV: {name}"
    return result


def run(output, mode="compfrr", audit=False):
    arguments = ["satcompute", "--simulationDuration=15", "--randomSeed=1", "--randomRun=11",
        "--constellationConfig=contrib/satcompute/tests/fixtures/constellation/connected-16.csv",
        f"--taskTrace={FIXTURE / 'fixed-four-profiles.json'}",
        f"--computeProfile={FIXTURE / 'fixed-compute.json'}", "--faultMode=generate",
        "--faultEnableF1=1", "--faultEnableF2=1", "--faultEnableF3=0",
        "--taskCompletionPolicy=report", "--compfrr-shadow=0",
        f"--faultProbabilityAudit={int(audit)}", f"--protectionMode={mode}",
        "--routingMode=global-capacity-aware-hrw", "--islBandwidthBps=10000000000",
        "--delayMode=fixed", "--fixedDelay=0.001", f"--outputDir={output}"]
    process = subprocess.run([str(ROOT / "ns3"), "run", "--no-build", shlex.join(arguments)],
                             cwd=ROOT, text=True, capture_output=True, timeout=120)
    assert process.returncode == 0, process.stdout + process.stderr


def verify(root):
    plain, audit, off = (root / name for name in ("plain", "audit", "off"))
    run(plain)
    run(audit, audit=True)
    decisions = rows(audit, "frequency-decisions.csv")
    assert decisions, "no real fault-grid decisions"
    probabilities = {(r["task_id"], r["simulation_time_ns"]): r
                     for r in rows(audit, "fault-model-probabilities.csv")}
    for r in decisions:
        p = probabilities[(r["task_id"], r["fault_epoch_time_ns"])]
        assert float(r["q_current_sample"]) == float(p["combined_step_failure_probability"])
        assert float(r["p_fail_before_finish"]) == float(p["failure_before_finish_probability"])
        assert int(r["fault_epoch_time_ns"]) % 10**9 == 0
        assert int(r["fault_epoch_time_ns"]) > int(p["task_compute_start_time_ns"])
        if r["actual_fault_hit"] == "1":
            assert r["decision_committed"] == "0"
        if r["proposed_action"] == "START" and r["decision_committed"] == "1":
            assert r["phase_after"] == "INITIALIZING"
    for name in ("frequency-decisions.csv", "protection-events.csv", "protection-transfers.csv",
                 "protection-task-summary.csv", "protection-node-storage-summary.csv",
                 "recovery-summary.csv", "recovery-events.csv", "fault-trace.json"):
        assert (plain / name).read_bytes() == (audit / name).read_bytes(), name
    for event in rows(audit, "protection-events.csv"):
        assert int(event["remote_work_units"]) <= int(event["local_work_units"]) <= int(event["actual_work_units"])
    for pool in rows(audit, "protection-node-storage-summary.csv"):
        assert int(pool["used_bytes"]) == int(pool["reserved_bytes"]) == 0
        assert int(pool["peak_total_bytes"]) <= int(pool["capacity_bytes"])
    run(off, mode="off")
    assert not (off / "frequency-decisions.csv").exists()
    run(plain, mode="off")
    assert not (plain / "frequency-decisions.csv").exists(), "stale frequency audit survived off"
    for name in ("task-summary.csv", "transfer-summary.csv", "fault-trace.json"):
        assert (off / name).read_bytes() == (plain / name).read_bytes(), name
    print(json.dumps({"frequency_smoke": "passed", "tasks": 4, "decisions": len(decisions),
                      "sampler_probability_exact": True, "audit_independent": True,
                      "off_unchanged": True}, sort_keys=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--outputDir", type=Path)
    args = parser.parse_args()
    if args.outputDir:
        args.outputDir.mkdir(parents=True, exist_ok=True)
        verify(args.outputDir)
    else:
        with tempfile.TemporaryDirectory(prefix="satcompute-frequency-smoke-") as directory:
            verify(Path(directory))


if __name__ == "__main__":
    main()
