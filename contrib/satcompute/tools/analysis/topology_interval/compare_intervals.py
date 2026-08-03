#!/usr/bin/env python3
"""Compare one checked held scenario with its 1-second reference."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

from ...generation.topology.common.hash_utils import compact_json
from ...generation.topology.common.satcompute_schema import read_json
from .ecmp_candidates import compare_ecmp_traces
from .edge_state import compare_edge_traces, load_edge_trace


class IntervalComparisonError(ValueError):
    """Raised when two scenario manifests do not form a comparison pair."""


def compare_scenarios(
    reference_dir: Path,
    held_dir: Path,
) -> dict[str, Any]:
    """Compare topology and ECMP states at every reference second."""
    reference_root = Path(reference_dir)
    held_root = Path(held_dir)
    reference_trace = load_edge_trace(reference_root)
    held_trace = load_edge_trace(held_root)
    reference_manifest = read_json(
        reference_root / "scenario-manifest.json"
    )
    held_manifest = read_json(held_root / "scenario-manifest.json")
    if not isinstance(reference_manifest, dict) or not isinstance(
        held_manifest,
        dict,
    ):
        raise IntervalComparisonError(
            "scenario manifests must be objects"
        )
    reference_sha256 = reference_manifest["aggregate_scenario_sha256"]
    if (
        held_manifest["reference_scenario_sha256"] != reference_sha256
        or held_manifest["downsample_interval_s"] is None
    ):
        raise IntervalComparisonError(
            "held scenario does not identify the supplied reference"
        )
    reference_schedule = reference_manifest[
        "scenario_config"
    ]["topology"]["schedule"]
    held_schedule = held_manifest[
        "scenario_config"
    ]["topology"]["schedule"]
    for field in (
        "start_time_s",
        "orbit_sample_offset_s",
        "duration_s",
    ):
        if reference_schedule[field] != held_schedule[field]:
            raise IntervalComparisonError(
                f"reference and held schedule {field} values differ"
            )
    if reference_schedule["step_s"] != 1:
        raise IntervalComparisonError(
            "reference schedule step_s must be 1"
        )
    interval_s = held_manifest["downsample_interval_s"]
    if held_schedule["step_s"] != interval_s:
        raise IntervalComparisonError(
            "held schedule step differs from downsample provenance"
        )
    return {
        "scenario_name": reference_manifest["scenario_name"],
        "node_count": reference_trace.node_count,
        "duration_s": reference_trace.duration_s,
        "orbit_sample_offset_s":
            reference_schedule["orbit_sample_offset_s"],
        "reference_scenario_sha256": reference_sha256,
        "held_scenario_sha256":
            held_manifest["aggregate_scenario_sha256"],
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


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, required=True)
    parser.add_argument("--held-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        result = compare_scenarios(
            arguments.reference_dir.absolute(),
            arguments.held_dir.absolute(),
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(compact_json(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
