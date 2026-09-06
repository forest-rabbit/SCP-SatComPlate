#!/usr/bin/env python3
"""Run the frozen pressure workload locally, preserving logs and raw metrics."""

import argparse
from collections import defaultdict, deque
import json
import shlex
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[5]
MODULE = ROOT / "contrib/satcompute"


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def preflight(directory, smoke):
    trace = directory / ("task-smoke.json" if smoke else "task-trace.json")
    tasks = json.loads(trace.read_text())["tasks"]
    profile = json.loads((directory / "compute-profile.json").read_text())["compute_nodes"]
    metadata = json.loads((directory / "pressure-input-summary.json").read_text())
    duration = 30 if smoke else metadata["simulation_duration_s"]
    links = json.loads((directory / "orbit/topology/links_0s.json").read_text())["links"]
    adjacency = defaultdict(set)
    for link in links:
        if link["active"]:
            a, b = link["node1_id"], link["node2_id"]
            adjacency[a].add(b)
            adjacency[b].add(a)
    distances = {}
    work = defaultdict(int)
    packet_count = packet_hops = total_bytes = 0
    for task in tasks:
        work[task["compute_node_id"]] += task["compute_work_units"]
        if task["arrival_time_ns"] >= duration * 10**9:
            raise ValueError("task arrives outside the pressure run")
        for source, destination, size in (
            (task["source_node_id"], task["compute_node_id"], task["input_bytes"]),
            (task["compute_node_id"], task["result_node_id"], task["output_bytes"]),
        ):
            if source not in distances:
                hops, queue = {source: 0}, deque([source])
                while queue:
                    node = queue.popleft()
                    for neighbor in adjacency[node]:
                        if neighbor not in hops:
                            hops[neighbor] = hops[node] + 1
                            queue.append(neighbor)
                distances[source] = hops
            if destination not in distances[source]:
                raise ValueError(f"initial topology has no path {source} -> {destination}")
            payload = 1024 if size <= 1 << 20 else 8192 if size <= 64 << 20 else 64000
            count = (size + payload - 1) // payload
            packet_count += count
            packet_hops += count * distances[source][destination]
            total_bytes += size
    return trace, duration, {
        "task_count": len(tasks), "transfer_count": 2 * len(tasks),
        "application_bytes": total_bytes, "expected_udp_packets": packet_count,
        "initial_shortest_path_packet_hops": packet_hops,
        "initial_active_undirected_links": sum(len(v) for v in adjacency.values()) // 2,
        "max_compute_service_load_s": max(work[n["node_id"]] /
            n["compute_rate_work_units_per_second"] for n in profile),
        "initial_paths_reachable": True,
        "note": "initial-hop estimate and service workload, not a queueing completion guarantee",
    }


def validate_result(output, expected):
    result = json.loads((output / "run-summary.json").read_text())
    checks = {
        "run_status": "COMPLETE", "task_count": expected["task_count"],
        "completed_task_count": expected["task_count"],
        "transfer_count": expected["transfer_count"],
        "received_application_bytes": expected["application_bytes"],
        "sent_application_bytes": expected["application_bytes"],
        "flow_monitor_tx_packets": expected["expected_udp_packets"],
        "flow_monitor_rx_packets": expected["expected_udp_packets"],
        "flow_monitor_lost_packets": 0,
    }
    for field, value in checks.items():
        if result[field] != value:
            raise ValueError(f"pressure check failed: {field}={result[field]}, expected {value}")
    if result["transfer"]["completed_transfer_count"] != expected["transfer_count"]:
        raise ValueError("incomplete pressure transfers")
    capacity = json.loads((output / "capacity-aware-summary.json").read_text())
    if any(capacity.values()):
        raise ValueError("capacity reservations did not drain")


def run(directory, label, smoke, metrics):
    output = directory / label
    if output.exists():
        raise ValueError(f"refusing to overwrite existing pressure output: {output}")
    trace, duration, expected = preflight(directory, smoke)
    output.mkdir(parents=True)
    write_json(output / "preflight.json", expected)
    size = json.loads((directory / "pressure-input-summary.json").read_text())["satellite_count"]
    arguments = ["satcompute", f"--simulationDuration={duration}",
        f"--constellationConfig={MODULE / f'input/topology/constellations/synthetic-{size}.csv'}",
        "--orbitStartOffset=0", "--maxIslDistance=6171353", "--delayMode=fixed",
        "--fixedDelay=0.008", "--networkUpdateInterval=20", "--islBandwidthBps=10000000000",
        "--islMtuBytes=65535", "--islQueueBytes=64000000", "--receiverRcvBufBytes=131072",
        "--routingMode=global-capacity-aware-hrw", "--transferChunkMode=size-aware",
        "--ecmpHashSeed=1", "--randomSeed=1", "--randomRun=1", "--faultMode=none",
        "--faultProbabilityAudit=0", "--taskCompletionPolicy=report", "--diagnosticMode=failure",
        "--taskLogMode=silent", f"--linkMetrics={int(metrics)}", "--linkMetricsInterval=1",
        f"--computeProfile={directory / 'compute-profile.json'}", f"--taskTrace={trace}",
        f"--outputDir={output}"]
    command = [str(ROOT / "ns3"), "run", "--no-build", shlex.join(arguments)]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    write_json(output / "execution.json", {"command": command, "git_commit": revision,
        "smoke": smoke, "link_metrics": metrics})
    print(f"Starting {size}-satellite {label}: {expected}", flush=True)
    start = time.monotonic()
    with (output / "run.log").open("w") as log:
        result = subprocess.run(["/usr/bin/time", "-v", "-o", str(output / "time.txt"),
                                 *command], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    elapsed = time.monotonic() - start
    write_json(output / "execution-result.json", {"exit_code": result.returncode,
                                                   "elapsed_wall_s": elapsed})
    if result.returncode:
        raise RuntimeError(f"pressure run failed; inspect {output / 'run.log'}")
    validate_result(output, expected)
    print(f"PASS: {size}-satellite {label}, {elapsed:.3f} wall seconds", flush=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-root", required=True, type=Path)
    parser.add_argument("--size", type=int, choices=(66, 351, 720), required=True)
    parser.add_argument("--stage", choices=("smoke", "full"), required=True)
    parser.add_argument("--label", help="new output subdirectory; existing results are never overwritten")
    args = parser.parse_args()
    directory = args.input_root.resolve() / str(args.size)
    smoke = args.stage == "smoke"
    label = args.label or args.stage
    if Path(label).name != label or label in (".", ".."):
        parser.error("label must be a single subdirectory name")
    if not smoke and not (directory / "smoke/verification.json").is_file():
        parser.error("run the small smoke gate for this scale before the full run")
    on = run(directory, label, smoke, True)
    if smoke:
        off = run(directory, label + "-off", True, False)
        filenames = ["task-summary.csv", "task-events.csv", "transfer-summary.csv",
                     "network-flow-metrics.csv", "network-flow-details.csv", "ecmp-route-events.csv",
                     "compute-node-summary.csv", "size-aware-reservation-events.csv",
                     "capacity-aware-summary.json"]
        for filename in filenames:
            if (on / filename).read_bytes() != (off / filename).read_bytes():
                raise ValueError(f"metrics changed simulation evidence: {filename}")
        write_json(on / "verification.json", {"metrics_on_off_identical": True,
            "compared_files": filenames, "crosses_network_update_s": 20})


if __name__ == "__main__":
    main()
