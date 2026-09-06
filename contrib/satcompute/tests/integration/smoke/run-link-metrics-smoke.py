#!/usr/bin/env python3
"""Small, deterministic checks for optional physical-link observations."""

import csv
import json
import math
import shlex
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[5]
TASK = "contrib/satcompute/tests/fixtures/task"
FILES = ("link-window-metrics.csv", "link-summary.csv", "network-link-window-metrics.csv")


def rows(directory, name):
    with (directory / name).open() as stream:
        return list(csv.DictReader(stream))


def run(directory, *, metrics=True, tasks=True, extra=(), expected=0):
    args = ["satcompute", "--simulationDuration=2.5", "--taskLogMode=silent",
            "--constellationConfig=contrib/satcompute/tests/fixtures/constellation/connected-16.csv",
            "--fixedDelay=0.001", "--networkUpdateInterval=1",
            f"--linkMetrics={int(metrics)}", "--linkMetricsInterval=1", f"--outputDir={directory}"]
    if tasks:
        args += [f"--computeProfile={TASK}/compute-profile-single.json",
                 f"--taskTrace={TASK}/task-single.json"]
    completed = subprocess.run([str(ROOT / "ns3"), "run", "--no-build",
                                shlex.join(args + list(extra))], cwd=ROOT,
                               text=True, capture_output=True)
    if completed.returncode != expected:
        raise AssertionError(completed.stdout + completed.stderr)
    return completed


def check_windows(directory):
    windows = rows(directory, FILES[0])
    summary = rows(directory, FILES[1])
    assert len(windows) == 3 * len(summary)
    by_link = {}
    for row in windows:
        key = (row["source_node_id"], row["destination_node_id"], row["output_interface"])
        by_link.setdefault(key, []).append(row)
        duration = float(row["window_end_s"]) - float(row["window_start_s"])
        busy = float(row["tx_busy_time_s"])
        assert -1e-12 <= busy <= duration + 1e-12
        assert math.isclose(float(row["utilization_percent"]), busy / duration * 100,
                            abs_tol=1e-9)
    for row in summary:
        key = (row["source_node_id"], row["destination_node_id"], row["output_interface"])
        samples = by_link[key]
        assert [(r["window_start_s"], r["window_end_s"]) for r in samples] == [
            ("0", "1"), ("1", "2"), ("2", "2.5")]
        for field in ("tx_started_bytes", "tx_started_packets", "drop_bytes", "drop_packets"):
            assert sum(int(r[field]) for r in samples) == int(row[field])
        assert math.isclose(sum(float(r["serialized_bits"]) for r in samples),
                            float(row["serialized_bits"]), abs_tol=1e-6)
        # Complete, drained runs serialize every started byte, including the PPP header.
        assert math.isclose(float(row["serialized_bits"]), 8 * int(row["tx_started_bytes"]),
                            abs_tol=1e-6)
    network = rows(directory, FILES[2])
    for row in network:
        samples = [r for r in windows if r["window_start_s"] == row["window_start_s"]]
        available = sum(float(r["available_time_s"]) for r in samples)
        busy = sum(float(r["available_tx_busy_time_s"]) for r in samples)
        assert math.isclose(float(row["mean_utilization_percent"]), busy / available * 100,
                            abs_tol=1e-9)
    return summary


def main():
    with tempfile.TemporaryDirectory(prefix="satcompute-link-metrics-") as temporary:
        root = Path(temporary)
        on, off = root / "on", root / "off"
        run(on)
        run(off, metrics=False)
        for filename in ("task-summary.csv", "task-events.csv", "transfer-summary.csv",
                         "network-flow-metrics.csv", "network-flow-details.csv",
                         "compute-node-summary.csv", "ecmp-route-events.csv",
                         "size-aware-reservation-events.csv", "capacity-aware-summary.json"):
            assert (on / filename).read_bytes() == (off / filename).read_bytes(), filename
        assert not any((off / name).exists() for name in FILES)
        summary = check_windows(on)
        assert any(float(row["peak_reserved_rate_bps"]) == 10_000_000_000 for row in summary)
        assert any(int(row["tx_started_bytes"]) == 0 for row in summary)

        run(root / "idle", tasks=False)
        assert all(float(row["utilization_percent"]) == 0 for row in check_windows(root / "idle"))
        # A queue rejection is observed as a drop, never as a physical send.
        run(root / "drops", extra=("--islQueueBytes=1", "--taskCompletionPolicy=report"))
        dropped = check_windows(root / "drops")
        assert sum(int(row["drop_packets"]) for row in dropped) > 0
        assert sum(int(row["tx_started_packets"]) for row in dropped) == 0

        # Failed links retain identity but have an explicit unavailable interval.
        fault = root / "fault.json"
        fault.write_text(json.dumps({"faults": [{"fault_id": 1, "node_id": 0,
            "fault_type": "satellite", "start_time_ns": 1_000_000_000,
            "notice_time_ns": None, "failure_probability": None, "duration_ns": None}]}))
        run(root / "fault", extra=("--faultMode=replay", f"--faultTrace={fault}"))
        check_windows(root / "fault")
        incident = [r for r in rows(root / "fault", FILES[0])
                    if "0" in (r["source_node_id"], r["destination_node_id"])
                    and float(r["window_start_s"]) >= 1]
        assert incident
        assert all(float(r["available_time_s"]) == 0 and
                   r["available_utilization_percent"] == "" for r in incident)

        marker = on / "user-note.txt"
        marker.write_text("keep")
        run(on, metrics=False)
        assert marker.read_text() == "keep"
        assert not any((on / name).exists() for name in FILES)
        run(root / "invalid", extra=("--linkMetricsInterval=0",), expected=2)
        run(root / "topology-only", tasks=False, extra=("--topologyOnly=1",), expected=2)
    print("SatCompute link metric smoke passed (on/off identical, idle, drops, windows, cleanup).")


if __name__ == "__main__":
    main()
