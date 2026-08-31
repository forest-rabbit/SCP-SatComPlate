#!/usr/bin/env python3
"""Run the real F2-only platform across fixed ns-3 run numbers."""

import argparse
import csv
import json
import math
from pathlib import Path
import shlex
import statistics
import subprocess
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
CONSTELLATION = Path(
    "contrib/satcompute/input/topology/constellations/synthetic-66.csv"
)
COMPUTE_PROFILE = Path(
    "contrib/satcompute/input/topology/resources/workload/"
    "xw-66sat-static-2g-all-compute-profile.json"
)
TASK_TRACE = Path(
    "contrib/satcompute/input/examples/leo-66-1000s-f2/task-trace.json"
)


def positive_int(value):
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def read_json(path):
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def write_json(path, value):
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def distribution(values):
    return {
        "min": min(values),
        "mean": statistics.mean(values),
        "population_variance": statistics.pvariance(values),
        "max": max(values),
    }


def run_platform(ns3_path, random_run, orbit_start_offset, temporary_root):
    run_directory = temporary_root / f"run-{random_run}"
    trace_path = run_directory / "fault-trace.json"
    arguments = [
        "satcompute",
        "--simulationDuration=1000",
        "--randomSeed=1",
        f"--randomRun={random_run}",
        f"--constellationConfig={CONSTELLATION}",
        f"--orbitStartOffset={orbit_start_offset}",
        "--maxIslDistance=6171353",
        "--delayMode=fixed",
        "--fixedDelay=0.008",
        "--networkUpdateInterval=20",
        "--islBandwidthBps=2000000000",
        "--routingMode=global-capacity-aware-hrw",
        f"--computeProfile={COMPUTE_PROFILE}",
        f"--taskTrace={TASK_TRACE}",
        "--taskCompletionPolicy=report",
        "--faultMode=generate",
        "--faultEnableF1=0",
        "--faultEnableF2=1",
        "--faultEnableF3=0",
        f"--faultTrace={trace_path}",
        f"--outputDir={run_directory}",
        "--taskLogMode=silent",
    ]
    result = subprocess.run(
        [str(ns3_path), "run", "--no-build", shlex.join(arguments)],
        cwd=REPOSITORY_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"randomRun={random_run} failed: {result.stderr.strip()}"
        )
    trace = read_json(trace_path)
    if trace.get("schema_version") != 2 or not isinstance(trace.get("faults"), list):
        raise RuntimeError(f"randomRun={random_run} produced an invalid trace")
    occurred = [fault for fault in trace["faults"] if fault["fault_occurred"]]
    risk_only = [fault for fault in trace["faults"] if not fault["fault_occurred"]]
    return {
        "random_run": random_run,
        "occurred_fault_count": len(occurred),
        "risk_only_episode_count": len(risk_only),
        "occurred_node_ids": ";".join(str(fault["node_id"]) for fault in occurred),
        "start_times_ns": ";".join(str(fault["start_time_ns"]) for fault in occurred),
    }


def main():
    parser = argparse.ArgumentParser(
        description="Run fixed-seed F2-only Monte Carlo through the real platform"
    )
    parser.add_argument("--run-count", type=positive_int, default=30)
    parser.add_argument(
        "--calibration-summary",
        required=True,
        type=Path,
        help="Spatial calibration summary produced by the orbit-only tool",
    )
    parser.add_argument("--outputDir", required=True, type=Path)
    parser.add_argument("--ns3", type=Path, default=REPOSITORY_ROOT / "ns3")
    args = parser.parse_args()

    ns3_path = args.ns3.resolve()
    if not ns3_path.is_file():
        raise ValueError(f"ns-3 launcher does not exist: {ns3_path}")
    calibration_path = args.calibration_summary
    if not calibration_path.is_absolute():
        calibration_path = REPOSITORY_ROOT / calibration_path
    calibration = read_json(calibration_path.resolve())
    selected = calibration["selected"]
    expected_mean = selected["functional_target_mean_fault_count"]
    orbit_start_offset = selected["start_offset_s"]
    maximum_effective_intensity = selected[
        "candidate_maximum_effective_failure_intensity_per_s"
    ]
    configured_maximum_effective_intensity = calibration["seu_mapping"][
        "configured_maximum_effective_failure_intensity_per_s"
    ]
    if not math.isclose(
        maximum_effective_intensity,
        configured_maximum_effective_intensity,
        rel_tol=1e-12,
        abs_tol=0.0,
    ):
        raise ValueError(
            "calibration candidate and configured maximum F2 intensity differ"
        )
    output_directory = args.outputDir.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="satcompute-f2-monte-carlo-") as directory:
        temporary_root = Path(directory)
        rows = [
            run_platform(
                ns3_path,
                random_run,
                orbit_start_offset,
                temporary_root,
            )
            for random_run in range(1, args.run_count + 1)
        ]

    csv_path = output_directory / "n4b-f2-monte-carlo.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=tuple(rows[0]),
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(rows)
    fault_counts = [row["occurred_fault_count"] for row in rows]
    risk_only_counts = [row["risk_only_episode_count"] for row in rows]
    observed_mean = statistics.mean(fault_counts)
    standard_error = (
        math.sqrt(statistics.variance(fault_counts) / args.run_count)
        if args.run_count > 1
        else 0.0
    )
    summary = {
        "calibration_scope": (
            "real F2-only platform runs with 66 satellites, 1000 seconds, "
            "8 validation tasks, and no F1/F3"
        ),
        "random_seed": 1,
        "random_run_first": 1,
        "random_run_last": args.run_count,
        "run_count": args.run_count,
        "orbit_start_offset_s": orbit_start_offset,
        "spatial_model": "two_piece_gaussian_hotspot",
        "reference_seu_intensity_per_s": calibration["seu_mapping"][
            "configured_reference_seu_intensity_per_s"
        ],
        "seu_to_compute_failure_probability": calibration["seu_mapping"][
            "seu_to_compute_failure_probability"
        ],
        "maximum_effective_failure_intensity_per_s": (
            maximum_effective_intensity
        ),
        "analytical_target_mean_fault_count": expected_mean,
        "observed_fault_count": distribution(fault_counts),
        "observed_mean_standard_error": standard_error,
        "approximate_95_percent_mean_interval": [
            max(0.0, observed_mean - 1.96 * standard_error),
            observed_mean + 1.96 * standard_error,
        ],
        "observed_risk_only_episode_count": distribution(risk_only_counts),
        "zero_fault_run_count": sum(value == 0 for value in fault_counts),
        "relative_mean_error": (
            (observed_mean - expected_mean) / expected_mean
            if not math.isclose(expected_mean, 0.0)
            else None
        ),
    }
    write_json(output_directory / "n4b-f2-monte-carlo-summary.json", summary)
    print(
        "PASS: F2 Monte Carlo completed "
        f"({args.run_count} runs, mean faults={observed_mean:.6f})"
    )


if __name__ == "__main__":
    main()
