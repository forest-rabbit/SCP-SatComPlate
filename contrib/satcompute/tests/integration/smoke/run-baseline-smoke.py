#!/usr/bin/env python3
"""Full baseline CLI/metrics wiring on four typed tasks; no formal workload changes."""
import csv
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[5]
FIXTURE = ROOT / "contrib/satcompute/tests/fixtures/protection"


def rows(path, name):
    with (path / name).open() as stream:
        return list(csv.DictReader(stream))


def run(directory, mode, trace):
    args = ["satcompute", "--simulationDuration=15", "--randomSeed=1", "--randomRun=11",
            "--faultMode=none", "--compfrr-shadow=0", "--taskCompletionPolicy=strict",
            "--constellationConfig=contrib/satcompute/tests/fixtures/constellation/connected-16.csv",
            f"--computeProfile={FIXTURE / 'fixed-compute.json'}", f"--taskTrace={trace}",
            f"--protectionMode={mode}", "--placementMode=ffp", "--backupStorageBytesPerNode=10000000000",
            "--routingMode=global-capacity-aware-hrw", "--islBandwidthBps=10000000000",
            "--delayMode=fixed", "--fixedDelay=0.001", f"--outputDir={directory}"]
    result = subprocess.run([str(ROOT / "ns3"), "run", "--no-build", shlex.join(args)],
                            cwd=ROOT, text=True, capture_output=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr


def main():
    with tempfile.TemporaryDirectory(prefix="satcompute-baseline-smoke-") as temporary:
        root = Path(temporary)
        trace = json.loads((FIXTURE / "fixed-four-profiles.json").read_text())
        for task in trace["tasks"]:
            task.update(source_node_id=1, result_node_id=6)
        trace_path = root / "tasks.json"
        trace_path.write_text(json.dumps(trace))
        off, recompute, replica, repeat = [root / name for name in ("off", "recompute", "replica", "repeat")]
        run(off, "off", trace_path)
        run(recompute, "recompute", trace_path)
        for name in ("task-events.csv", "task-summary.csv", "transfer-summary.csv", "compute-node-summary.csv"):
            assert (off / name).read_bytes() == (recompute / name).read_bytes(), name
        assert not rows(recompute, "recovery-summary.csv")
        assert not rows(recompute, "protection-transfers.csv")
        run(replica, "one-plus-one", trace_path)
        run(repeat, "one-plus-one", trace_path)
        for name in ("replica-summary.csv", "replica-attempts.csv", "replica-events.csv", "replica-transfers.csv"):
            assert (replica / name).read_bytes() == (repeat / name).read_bytes(), name
        summaries = rows(replica, "replica-summary.csv")
        assert len(summaries) == 4
        assert all(r["replica_requested"] == r["replica_admitted"] == "1" for r in summaries)
        assert all(r["terminal_state"] == "COMPLETED" for r in summaries)
        transfers = rows(replica, "replica-transfers.csv")
        assert sum(r["kind"] == "REPLICA_INPUT" for r in transfers) == 4
        for task in summaries:
            assert int(task["redundant_actual_wu"]) == int(task["primary_executed_wu"]) + int(task["replica_executed_wu"]) - int(task["total_work_units"])
            inputs = [r for r in transfers if r["task_id"] == task["task_id"] and r["kind"] == "REPLICA_INPUT"]
            assert len(inputs) == 1 and int(inputs[0]["declared_bytes"]) == int(task["input_bytes"])
        assert sum(r["business_result"] == "1" for r in transfers) == 4
        assert len(rows(replica, "replica-attempts.csv")) == 8
        assert not rows(replica, "protection-task-summary.csv")
        for directory in (recompute, replica):
            assert json.loads((directory / "protection-finalization.json").read_text())["quiescent"]
            assert all(int(r["peak_total_bytes"]) == int(r["allocation_failures"]) == 0
                       for r in rows(directory, "protection-node-storage-summary.csv"))
        # Reusing an output directory cannot leave a previous scheme's evidence behind.
        run(replica, "recompute", trace_path)
        assert not (replica / "replica-summary.csv").exists()
        run(recompute, "one-plus-one", trace_path)
        assert not (recompute / "recovery-summary.csv").exists()
        print(json.dumps(dict(baseline_smoke="passed", profiles=4, real_replica_inputs=4,
                              deterministic=True, recompute_no_fault_identical_to_off=True)))


if __name__ == "__main__":
    main()
