#!/usr/bin/env python3
"""Prepare the explicitly frozen 10 Gbps, three-scale pressure inputs."""

import argparse
import json
import shlex
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
MODULE = ROOT / "contrib/satcompute"
# Legacy load budgets, current native orbits and current deterministic task generator.
SCALES = {66: (6, 11, 1000, 600), 351: (27, 13, 600, 340), 720: (18, 40, 300, 165)}
RATE = 1_500_000
INPUT_BYTES = 81_750_000_000


def compute_ids(size):
    planes, per_plane, _, _ = SCALES[size]
    if size == 66:
        return list(range(size))
    counts = (4, 4, 5) if size == 351 else (13, 13, 14)
    return [plane * per_plane + slot * per_plane // counts[plane % 3]
            for plane in range(planes) for slot in range(counts[plane % 3])]


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def prepare(size, root):
    directory = root / str(size)
    if directory.exists():
        raise ValueError(f"refusing to overwrite existing inputs: {directory}")
    directory.mkdir(parents=True)
    _, _, duration, arrival_end = SCALES[size]
    constellation = MODULE / f"input/topology/constellations/synthetic-{size}.csv"
    arguments = ["satcompute", "--topologyOnly=1", "--simulationDuration=1",
                 f"--constellationConfig={constellation}", "--orbitStartOffset=0",
                 "--maxIslDistance=6171353", "--delayMode=fixed", "--fixedDelay=0.008",
                 "--islBandwidthBps=10000000000", f"--outputDir={directory / 'orbit'}"]
    subprocess.run([str(ROOT / "ns3"), "run", "--no-build", shlex.join(arguments)],
                   cwd=ROOT, check=True)
    nodes = directory / "orbit/topology/nodes_0s.json"
    ids = sorted(node["node_id"] for node in json.loads(nodes.read_text())["nodes"])
    if ids != list(range(size)):
        raise ValueError("native constellation IDs do not match the pressure matrix")
    profile = {"compute_nodes": [{"node_id": node, "compute_rate_work_units_per_second": RATE}
                                  for node in compute_ids(size)]}
    write_json(directory / "compute-profile.json", profile)
    command = [sys.executable, str(MODULE / "tools/generation/generate-task-workload.py"),
               f"--nodes-file={nodes}", f"--compute-profile={directory / 'compute-profile.json'}",
               "--task-count=1500", f"--total-input-bytes={INPUT_BYTES}", "--seed=20260726",
               "--arrival-start-ns=1000000000", f"--arrival-end-ns={arrival_end * 10**9}",
               "--arrival-mode=uniform", "--large-1gb-count=20", "--large-500mb-count=40",
               "--scenario-scale-bp=7500",
               f"--output-task-trace={directory / 'task-trace.json'}",
               f"--output-workload-summary={directory / 'workload-summary.json'}"]
    subprocess.run(command, cwd=ROOT, check=True)
    tasks = json.loads((directory / "task-trace.json").read_text())["tasks"]
    summary = json.loads((directory / "workload-summary.json").read_text())
    if len(tasks) != 1500 or sum(task["input_bytes"] for task in tasks) != INPUT_BYTES:
        raise ValueError("pressure input budget differs")
    if summary["large_1gb_count"] != 15 or summary["large_500mb_count"] != 30:
        raise ValueError("pressure large-task counts differ")
    early = sorted(tasks, key=lambda task: (task["arrival_time_ns"], task["task_id"]))[:20]
    write_json(directory / "task-smoke.json", {"tasks": early})
    write_json(directory / "pressure-input-summary.json", {
        "satellite_count": size, "compute_node_count": len(compute_ids(size)),
        "simulation_duration_s": duration, "arrival_window_s": [1, arrival_end],
        "link_bandwidth_bps": 10_000_000_000, "generator_seed": "20260726",
        "task_count": len(tasks), "smoke_task_count": len(early),
        "total_input_bytes": summary["total_input_bytes"],
        "total_output_bytes": summary["total_output_bytes"],
        "total_compute_work_units": summary["total_compute_work_units"],
        "historical_comparison": "new FNV-generated 10 Gbps baseline, not exact legacy replay",
        "generator_command": command,
    })
    print(f"Prepared {size} satellites / {len(compute_ids(size))} compute nodes", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--sizes", nargs="+", type=int, choices=SCALES, default=list(SCALES))
    args = parser.parse_args()
    root = args.output_root.resolve()
    for size in args.sizes:
        prepare(size, root)
    # Node placement changes across sizes; task size/work and arrival rank must not.
    traces = [json.loads((root / str(size) / "task-trace.json").read_text())["tasks"]
              for size in args.sizes]
    signatures = [[(task["task_id"], task["input_bytes"], task["output_bytes"],
                    task["compute_work_units"]) for task in trace] for trace in traces]
    if any(signature != signatures[0] for signature in signatures[1:]):
        raise ValueError("logical tasks differ across pressure scales")
    ranks = [[task["task_id"] for task in sorted(trace, key=lambda task: task["arrival_time_ns"])]
             for trace in traces]
    if any(rank != ranks[0] for rank in ranks[1:]):
        raise ValueError("arrival ordering differs across pressure scales")


if __name__ == "__main__":
    main()
