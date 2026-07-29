#!/usr/bin/env python3
"""Derive a held-snapshot scenario from one checked 1-second reference."""

from __future__ import annotations

import argparse
import copy
import platform
import sys
from pathlib import Path
from typing import Any

from ...generation.scenario.check_scenario import (
    COMPUTE_PROFILE_FILENAME,
    RESOURCES_DIRECTORY,
    SCENARIO_MANIFEST_FILENAME,
    TOPOLOGY_DIRECTORY,
    check_scenario,
)
from ...generation.scenario.compute_placement import (
    even_plane_slot_placement,
)
from ...generation.scenario.configuration import (
    DYNAMIC_MODE,
    DynamicSchedule,
    parse_config,
)
from ...generation.scenario.generate_scenario import (
    build_scenario_manifest,
)
from ...generation.topology.check_export import check_export
from ...generation.topology.common.atomic_output import (
    AtomicOutputError,
    atomic_output_directory,
)
from ...generation.topology.common.hash_utils import (
    REPOSITORY_ROOT,
    aggregate_data_sha256,
    compact_json,
    compact_json_bytes,
    git_repository_state,
    sha256_file,
    uv_version,
)
from ...generation.topology.common.satcompute_schema import read_json


TOPOLOGY_MANIFEST_FILENAME = "manifest.json"


class ScenarioDownsampleError(ValueError):
    """Raised when a reference cannot produce a valid held scenario."""


def _require_interval(interval_s: Any, duration_s: int) -> int:
    if (
        not isinstance(interval_s, int)
        or isinstance(interval_s, bool)
        or interval_s <= 0
    ):
        raise ScenarioDownsampleError(
            "interval_s must be a positive integer"
        )
    if duration_s % interval_s != 0:
        raise ScenarioDownsampleError(
            "reference duration_s must be divisible by interval_s"
        )
    return interval_s


def _reject_overlapping_paths(reference_dir: Path, output_dir: Path) -> None:
    reference = reference_dir.resolve()
    output = output_dir.resolve()
    if (
        reference == output
        or reference in output.parents
        or output in reference.parents
    ):
        raise ScenarioDownsampleError(
            "reference and output directories must not overlap"
        )


def _copy_selected_topology(
    reference_topology: Path,
    output_topology: Path,
    selected_times: tuple[int, ...],
    interval_s: int,
) -> tuple[dict[str, Any], dict[str, Any]]:
    output_topology.mkdir()
    data_names = []
    active_counts = []
    for time_s in selected_times:
        for kind in ("nodes", "topology"):
            filename = f"{kind}_{time_s}s.json"
            source = reference_topology / filename
            if not source.is_file():
                raise ScenarioDownsampleError(
                    f"reference snapshot is missing: {filename}"
                )
            (output_topology / filename).write_bytes(source.read_bytes())
            data_names.append(filename)
        topology = read_json(output_topology / f"topology_{time_s}s.json")
        if not isinstance(topology, dict) or not isinstance(
            topology.get("links"),
            list,
        ):
            raise ScenarioDownsampleError(
                f"reference topology_{time_s}s.json is invalid"
            )
        active_counts.append(len(topology["links"]))

    source_manifest = read_json(
        reference_topology / TOPOLOGY_MANIFEST_FILENAME
    )
    if not isinstance(source_manifest, dict):
        raise ScenarioDownsampleError(
            "reference topology manifest must be an object"
        )
    manifest = dict(source_manifest)
    satcompute_commit, worktree_clean = git_repository_state()
    manifest.update(
        {
            "satcompute_commit": satcompute_commit,
            "satcompute_worktree_clean": worktree_clean,
            "python_version": platform.python_version(),
            "uv_version": uv_version(),
            "uv_lock_sha256": sha256_file(REPOSITORY_ROOT / "uv.lock"),
            "step_s": interval_s,
            "snapshot_count": len(selected_times),
            "first_time_s": selected_times[0],
            "last_time_s": selected_times[-1],
            "min_active_count": min(active_counts),
            "max_active_count": max(active_counts),
            "average_active_count":
                sum(active_counts) / len(active_counts),
            "aggregate_data_sha256": aggregate_data_sha256(
                output_topology,
                data_names,
            ),
        }
    )
    (output_topology / TOPOLOGY_MANIFEST_FILENAME).write_bytes(
        compact_json_bytes(manifest)
    )
    summary = check_export(
        output_topology,
        expected_node_count=manifest["node_count"],
    )
    return manifest, summary


def downsample_scenario(
    reference_dir: Path,
    interval_s: int,
    output_dir: Path,
) -> dict[str, Any]:
    """Copy selected reference snapshots and rebuild all dependent hashes."""
    reference = Path(reference_dir)
    output = Path(output_dir)
    _reject_overlapping_paths(reference, output)
    check_scenario(reference)
    reference_manifest = read_json(
        reference / SCENARIO_MANIFEST_FILENAME
    )
    if not isinstance(reference_manifest, dict):
        raise ScenarioDownsampleError(
            "reference scenario manifest must be an object"
        )
    if (
        reference_manifest["reference_scenario_sha256"] is not None
        or reference_manifest["downsample_interval_s"] is not None
    ):
        raise ScenarioDownsampleError(
            "input must be an original reference scenario"
        )
    config_payload = copy.deepcopy(reference_manifest["scenario_config"])
    config = parse_config(config_payload)
    schedule = config.topology.schedule
    if config.topology.mode != DYNAMIC_MODE or not isinstance(
        schedule,
        DynamicSchedule,
    ):
        raise ScenarioDownsampleError(
            "reference scenario must use dynamic topology"
        )
    if schedule.step_s != 1:
        raise ScenarioDownsampleError(
            "reference scenario schedule step_s must be 1"
        )
    interval = _require_interval(interval_s, schedule.duration_s)
    selected_times = tuple(
        range(0, schedule.duration_s + 1, interval)
    )
    config_payload["topology"]["schedule"]["step_s"] = interval
    downsampled_config = parse_config(config_payload)
    placement = even_plane_slot_placement(
        downsampled_config.constellation.num_orbits,
        downsampled_config.constellation.satellites_per_orbit,
        downsampled_config.compute.compute_node_count,
    )

    try:
        with atomic_output_directory(output) as temporary:
            topology_manifest, topology_summary = _copy_selected_topology(
                reference / TOPOLOGY_DIRECTORY,
                temporary / TOPOLOGY_DIRECTORY,
                selected_times,
                interval,
            )
            resources = temporary / RESOURCES_DIRECTORY
            resources.mkdir()
            reference_profile = (
                reference
                / RESOURCES_DIRECTORY
                / COMPUTE_PROFILE_FILENAME
            )
            profile = resources / COMPUTE_PROFILE_FILENAME
            profile.write_bytes(reference_profile.read_bytes())
            profile_sha256 = sha256_file(profile)

            manifest = build_scenario_manifest(
                downsampled_config,
                temporary,
                topology_manifest,
                topology_summary,
                profile_sha256,
                placement.selected_node_ids,
                placement.compute_nodes_per_orbit,
                reference_scenario_sha256=reference_manifest[
                    "aggregate_scenario_sha256"
                ],
                downsample_interval_s=interval,
            )
            (temporary / SCENARIO_MANIFEST_FILENAME).write_bytes(
                compact_json_bytes(manifest)
            )
            check_scenario(temporary)
        return manifest
    except AtomicOutputError as error:
        raise ScenarioDownsampleError(str(error)) from error


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, required=True)
    parser.add_argument("--interval-s", type=int, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        manifest = downsample_scenario(
            arguments.reference_dir.absolute(),
            arguments.interval_s,
            arguments.output_dir.absolute(),
        )
    except (OSError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(compact_json(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
