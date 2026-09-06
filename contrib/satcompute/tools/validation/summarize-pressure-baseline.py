#!/usr/bin/env python3
"""Validate and summarize real link observations; never infer spare admission from utilization."""

import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def read_rows(path):
    with path.open() as stream:
        yield from csv.DictReader(stream)


def percentile(values, quantile):
    values = sorted(values)
    index = (len(values) - 1) * quantile
    lower = math.floor(index)
    upper = math.ceil(index)
    return values[lower] + (values[upper] - values[lower]) * (index - lower)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def summarize(directory):
    run = json.loads((directory / "run-summary.json").read_text())
    tasks = list(read_rows(directory / "task-summary.csv"))
    complete = [task for task in tasks if task["final_state"] == "COMPLETED"]
    require(bool(complete), "no completed tasks to summarize")
    active_start = min(int(task["arrival_time_ns"]) for task in tasks) / 1e9
    active_end = max(int(task["result_transfer_complete_time_ns"]) for task in complete) / 1e9
    delays = [int(task["end_to_end_completion_delay_ns"]) / 1e9 for task in complete]
    summary_rows = list(read_rows(directory / "link-summary.csv"))
    network = list(read_rows(directory / "network-link-window-metrics.csv"))
    available = sum(float(row["available_link_time_s"]) for row in network)
    busy = sum(float(row["available_busy_time_s"]) for row in network)
    active = [row for row in network if float(row["window_end_s"]) > active_start
              and float(row["window_start_s"]) < active_end]
    active_available = sum(float(row["available_link_time_s"]) for row in active)
    active_busy = sum(float(row["available_busy_time_s"]) for row in active)
    require(available > 0 and active_available > 0, "no available links")

    by_link = {(row["source_node_id"], row["destination_node_id"], row["output_interface"]):
               {"bytes": 0, "packets": 0, "busy": 0.0, "bits": 0.0, "drops": 0,
                "last_end": 0.0, "high_load_window_s": 0.0}
               for row in summary_rows}
    window_count = 0
    for row in read_rows(directory / "link-window-metrics.csv"):
        key = (row["source_node_id"], row["destination_node_id"], row["output_interface"])
        require(key in by_link, "window references an unknown link")
        totals = by_link[key]
        start, end = float(row["window_start_s"]), float(row["window_end_s"])
        duration = end - start
        require(duration > 0 and math.isclose(start, totals["last_end"], abs_tol=1e-9),
                "link windows have a gap or overlap")
        utilization = float(row["utilization_percent"])
        require(0 <= utilization <= 100 + 1e-9, "physical utilization outside 0..100 percent")
        require(math.isclose(utilization, float(row["tx_busy_time_s"]) / duration * 100,
                             rel_tol=1e-9, abs_tol=1e-9), "busy/utilization mismatch")
        require(float(row["mean_link_capacity_bps"]) == 10_000_000_000,
                "pressure link capacity differs from 10 Gbps")
        require(int(row["max_queue_bytes"]) <= run["isl_queue_bytes"], "queue exceeds its limit")
        totals["last_end"] = end
        totals["bytes"] += int(row["tx_started_bytes"])
        totals["packets"] += int(row["tx_started_packets"])
        totals["busy"] += float(row["tx_busy_time_s"])
        totals["bits"] += float(row["serialized_bits"])
        totals["drops"] += int(row["drop_packets"])
        if utilization >= 80:
            totals["high_load_window_s"] += duration
        window_count += 1
    for row in summary_rows:
        key = (row["source_node_id"], row["destination_node_id"], row["output_interface"])
        totals = by_link[key]
        require(math.isclose(totals["last_end"], run["simulation_duration_s"]), "missing final window")
        require(totals["bytes"] == int(row["tx_started_bytes"]), "link byte totals differ")
        require(totals["packets"] == int(row["tx_started_packets"]), "link packet totals differ")
        require(totals["drops"] == int(row["drop_packets"]), "link drop totals differ")
        require(math.isclose(totals["busy"], float(row["tx_busy_time_s"]),
                             rel_tol=1e-9, abs_tol=1e-9), "link busy totals differ")
        require(math.isclose(totals["bits"], float(row["serialized_bits"]),
                             rel_tol=1e-9, abs_tol=0.01), "link bit totals differ")
        require(math.isclose(totals["bits"], totals["bytes"] * 8,
                             rel_tol=1e-9, abs_tol=0.01), "pressure run did not drain serialization")
    total_link_bits = sum(float(row["serialized_bits"]) for row in summary_rows)
    require(math.isclose(total_link_bits, sum(float(row["serialized_bits"]) for row in network),
                         rel_tol=1e-9, abs_tol=0.01), "network/link bit totals differ")
    require(math.isclose(busy, sum(float(row["available_tx_busy_time_s"]) for row in summary_rows),
                         rel_tol=1e-9, abs_tol=1e-9), "network/link busy totals differ")
    hottest = max(summary_rows, key=lambda row: float(row["utilization_percent"]))
    compute = list(read_rows(directory / "compute-node-summary.csv"))
    flow = next(read_rows(directory / "network-flow-metrics.csv"))
    peak_rss = None
    time_path = directory / "time.txt"
    if time_path.exists():
        for line in time_path.read_text().splitlines():
            if "Maximum resident set size (kbytes):" in line:
                peak_rss = int(line.rsplit(":", 1)[1])
    elapsed = json.loads((directory / "execution-result.json").read_text())["elapsed_wall_s"]
    result = {
        "run_status": run["run_status"], "task_count": len(tasks), "completed_tasks": len(complete),
        "completed_transfers": run["transfer"]["completed_transfer_count"],
        "application_bytes": run["received_application_bytes"],
        "udp_packets": run["flow_monitor_rx_packets"], "lost_packets": run["flow_monitor_lost_packets"],
        "flow_monitor_throughput_mbps": float(flow["throughput_mbps"]),
        "flow_monitor_measurement_duration_s": float(flow["measurement_duration_s"]),
        "mean_delay_ms": float(flow["mean_delay_ms"]),
        "task_delay_mean_s": statistics.mean(delays), "task_delay_p95_s": percentile(delays, 0.95),
        "task_delay_p99_s": percentile(delays, 0.99), "task_delay_max_s": max(delays),
        "last_task_completion_s": active_end,
        "directed_link_count": len(summary_rows), "link_window_rows": window_count,
        "network_mean_utilization_percent": busy / available * 100,
        "active_window_cover_s": [float(active[0]["window_start_s"]), float(active[-1]["window_end_s"])],
        "active_window_mean_utilization_percent": active_busy / active_available * 100,
        "max_link_window_utilization_percent": max(float(row["peak_window_utilization_percent"])
                                                    for row in summary_rows),
        "hottest_link": {"source": int(hottest["source_node_id"]),
                         "destination": int(hottest["destination_node_id"]),
                         "mean_utilization_percent": float(hottest["utilization_percent"])},
        "link_queue_drops": sum(int(row["drop_packets"]) for row in summary_rows),
        "max_queue_bytes": max(int(row["max_queue_bytes"]) for row in summary_rows),
        "max_high_load_window_s_per_link": max(row["high_load_window_s"] for row in by_link.values()),
        "mean_compute_utilization_percent": statistics.mean(float(row["utilization_percent"]) for row in compute),
        "max_compute_utilization_percent": max(float(row["utilization_percent"]) for row in compute),
        "max_compute_queue_length": max(int(row["max_queue_length"]) for row in compute),
        "elapsed_wall_s": elapsed, "simulation_wall_s": run["wall_clock_s"], "peak_rss_kib": peak_rss,
        "link_metric_file_bytes": sum((directory / name).stat().st_size for name in (
            "link-window-metrics.csv", "network-link-window-metrics.csv", "link-summary.csv")),
        "audit": "window totals, network sums, serialization, capacity and queue limits passed",
        "notes": ["active period includes the complete metric windows overlapping task activity",
                  "80% is a descriptive high-load cutoff, not evidence of congestion or a backup trigger",
                  "utilization is not capacity-aware residual admission bandwidth"],
    }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", required=True, type=Path)
    args = parser.parse_args()
    result = summarize(args.run_dir)
    (args.run_dir / "pressure-summary.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
