#!/usr/bin/env python3
"""Downsample a checked 1-second v0.3 topology trace by exact file copying."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile

from ...generation.topology.common.manifest import read_json, validate_trace


class TraceDownsampleError(ValueError):
    """Raised when a reference trace cannot be downsampled safely."""


def _require_interval(interval_s, duration_ns):
    if (
        not isinstance(interval_s, int)
        or isinstance(interval_s, bool)
        or interval_s <= 0
    ):
        raise TraceDownsampleError("interval_s must be a positive integer")
    interval_ns = interval_s * 1_000_000_000
    if duration_ns % interval_ns != 0:
        raise TraceDownsampleError(
            "simulation duration must be divisible by interval_s"
        )
    return interval_ns


def _reject_overlapping_paths(reference_dir, output_dir):
    reference = reference_dir.resolve()
    output = output_dir.resolve()
    if (
        reference == output
        or reference in output.parents
        or output in reference.parents
    ):
        raise TraceDownsampleError(
            "reference and output directories must not overlap"
        )


def _manifest_sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def downsample_trace(reference_dir, interval_s, output_dir):
    """Copy selected slice pairs and write a self-contained v0.3 manifest."""
    reference = Path(reference_dir)
    output = Path(output_dir)
    _reject_overlapping_paths(reference, output)
    validate_trace(reference)
    manifest_path = reference / "manifest.json"
    manifest = read_json(manifest_path)
    if manifest["trace_interval_ns"] != 1_000_000_000:
        raise TraceDownsampleError(
            "reference topology trace interval must be exactly 1 second"
        )
    interval_ns = _require_interval(
        interval_s,
        manifest["simulation_duration_ns"],
    )
    records_by_time = {
        record["simulation_time_ns"]: record for record in manifest["slices"]
    }
    selected_times = tuple(
        range(0, manifest["simulation_duration_ns"] + 1, interval_ns)
    )
    missing = [
        time_ns for time_ns in selected_times if time_ns not in records_by_time
    ]
    if missing:
        raise TraceDownsampleError(
            f"reference trace is missing selected time {missing[0]} ns"
        )
    if output.exists():
        raise TraceDownsampleError(f"output directory already exists: {output}")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(
        tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent)
    )
    try:
        selected_records = []
        for time_ns in selected_times:
            record = records_by_time[time_ns]
            for field in ("nodes_file", "topology_file"):
                shutil.copyfile(
                    reference / record[field],
                    temporary / record[field],
                )
            selected_records.append({**record})
        output_manifest = {
            **manifest,
            "run_name": f"{manifest['run_name']}-held-{interval_s}s",
            "trace_interval_ns": interval_ns,
            "slice_count": len(selected_records),
            "slices": selected_records,
        }
        (temporary / "manifest.json").write_text(
            json.dumps(output_manifest, indent=2) + "\n",
            encoding="utf-8",
        )
        validation = validate_trace(temporary)
        temporary.replace(output)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    return {
        "reference_manifest_sha256": _manifest_sha256(manifest_path),
        "output_manifest": str(output / "manifest.json"),
        "interval_s": interval_s,
        "satellite_count": validation["satellite_count"],
        "slice_count": validation["slice_count"],
    }


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", required=True, type=Path)
    parser.add_argument("--interval-s", required=True, type=int)
    parser.add_argument("--output-dir", required=True, type=Path)
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    try:
        result = downsample_trace(
            arguments.reference_dir,
            arguments.interval_s,
            arguments.output_dir,
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
