#!/usr/bin/env python3
"""Compare one checked held topology trace with its 1-second reference."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

from ...generation.topology.common.manifest import read_json
from .ecmp_candidates import compare_ecmp_traces
from .edge_state import compare_edge_traces, load_edge_trace


class IntervalComparisonError(ValueError):
    """Raised when two topology manifests do not form a comparison pair."""


def _file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare_traces(
    reference_dir: Path,
    held_dir: Path,
) -> dict[str, Any]:
    """Compare topology and ECMP states at every reference second."""
    reference_root = Path(reference_dir)
    held_root = Path(held_dir)
    reference_trace = load_edge_trace(reference_root)
    held_trace = load_edge_trace(held_root)
    reference_manifest_path = reference_root / "manifest.json"
    held_manifest_path = held_root / "manifest.json"
    reference_manifest = read_json(reference_manifest_path)
    held_manifest = read_json(held_manifest_path)
    if not isinstance(reference_manifest, dict) or not isinstance(
        held_manifest,
        dict,
    ):
        raise IntervalComparisonError(
            "topology manifests must be objects"
        )
    for field in (
        "constellation_config_sha256",
        "simulation_duration_ns",
        "state_semantics",
        "coordinate_frame",
        "coordinate_units",
        "topology",
        "randomness",
    ):
        if reference_manifest[field] != held_manifest[field]:
            raise IntervalComparisonError(
                f"reference and held manifest {field} values differ"
            )
    if reference_manifest["trace_interval_ns"] != 1_000_000_000:
        raise IntervalComparisonError(
            "reference trace interval must be 1 second"
        )
    held_interval_ns = held_manifest["trace_interval_ns"]
    if held_interval_ns % 1_000_000_000 != 0:
        raise IntervalComparisonError(
            "held trace interval must be an integer number of seconds"
        )
    interval_s = held_interval_ns // 1_000_000_000
    return {
        "run_name": reference_manifest["run_name"],
        "node_count": reference_trace.node_count,
        "duration_s": reference_trace.duration_s,
        "reference_manifest_sha256": _file_sha256(reference_manifest_path),
        "held_manifest_sha256": _file_sha256(held_manifest_path),
        "interval_s": interval_s,
        "edge_state": compare_edge_traces(
            reference_trace,
            held_trace,
        ),
        "ecmp_candidates": compare_ecmp_traces(
            reference_trace,
            held_trace,
        ),
    }


# Keep the ns-3.33 Python API name while callers move to trace terminology.
compare_scenarios = compare_traces


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, required=True)
    parser.add_argument("--held-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        result = compare_traces(
            arguments.reference_dir.absolute(),
            arguments.held_dir.absolute(),
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
