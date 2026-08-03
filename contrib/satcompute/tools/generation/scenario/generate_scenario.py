#!/usr/bin/env python3
"""Generate one canonical satellite-computing scenario directory."""

from __future__ import annotations

import argparse
import platform
import sys
from pathlib import Path
from typing import Any

from ..topology.check_export import check_export
from ..topology.common.atomic_output import (
    AtomicOutputError,
    atomic_output_directory,
    remove_temporary_path,
)
from ..topology.common.hash_utils import (
    REPOSITORY_ROOT,
    compact_json,
    compact_json_bytes,
    git_repository_state,
    sha256_bytes,
    sha256_file,
    uv_version,
)
from ..topology.dynamic.export_satcompute import (
    export_satcompute_topology,
)
from ..topology.dynamic.generate_dynamic_isls import (
    generate_dynamic_isl_output,
)
from ..topology.static.generate_static_topology import (
    generate_static_topology,
)
from .check_scenario import (
    COMPUTE_PROFILE_FILENAME,
    RESOURCES_DIRECTORY,
    SCENARIO_MANIFEST_FILENAME,
    TOPOLOGY_DIRECTORY,
    check_scenario,
    scenario_aggregate_sha256,
)
from .compute_placement import even_plane_slot_placement
from .compute_profile import (
    build_compute_profile,
    compute_profile_bytes,
)
from .configuration import (
    DYNAMIC_MODE,
    STATIC_MODE,
    DynamicSchedule,
    ScenarioConfig,
    StaticSchedule,
    load_config,
)


SCENARIO_ROOT = Path(__file__).resolve().parent
DEFAULT_CONFIG = (
    SCENARIO_ROOT / "config" / "synthetic-66-compute-22.json"
)
TOPOLOGY_CONFIG_FILENAME = ".topology-config.json"
DYNAMIC_SOURCE_DIRECTORY = ".dynamic-source"


class ScenarioGenerationError(RuntimeError):
    """Raised when a unified scenario cannot be generated safely."""


class ScenarioVisualizationError(ScenarioGenerationError):
    """Raised after scenario publication when optional display fails."""


def _run_optional_visualization(
    output_dir: Path,
    visualization_config_path: Path,
) -> bool:
    """Load graphics code only when a separate config explicitly enables it."""
    from ...visualization.orbit.configuration import load_config

    visualization_config = load_config(visualization_config_path)
    if not visualization_config.enabled:
        return False
    from ...visualization.orbit.viewer import run_viewer

    run_viewer(output_dir, visualization_config_path)
    return True


def _generate_topology(
    config: ScenarioConfig,
    topology_config_path: Path,
    temporary: Path,
) -> dict[str, Any]:
    topology_dir = temporary / TOPOLOGY_DIRECTORY
    topology = config.topology
    schedule = topology.schedule
    if topology.mode == STATIC_MODE:
        if not isinstance(schedule, StaticSchedule):
            raise ScenarioGenerationError(
                "static topology has a non-static schedule"
            )
        return generate_static_topology(
            topology_config_path,
            schedule.snapshot_time_s,
            topology.delay_mode,
            topology.fixed_delay_us,
            topology.link_bandwidth_kbps,
            topology_dir,
        )

    if topology.mode == DYNAMIC_MODE:
        if not isinstance(schedule, DynamicSchedule):
            raise ScenarioGenerationError(
                "dynamic topology has a non-dynamic schedule"
            )
        source_dir = temporary / DYNAMIC_SOURCE_DIRECTORY
        generate_dynamic_isl_output(
            topology_config_path,
            schedule.duration_s,
            schedule.step_s,
            source_dir,
            orbit_sample_offset_s=schedule.orbit_sample_offset_s,
        )
        manifest = export_satcompute_topology(
            source_dir,
            topology.delay_mode,
            topology.fixed_delay_us,
            topology.link_bandwidth_kbps,
            topology_dir,
        )
        remove_temporary_path(source_dir)
        return manifest

    raise ScenarioGenerationError(
        f"unsupported topology mode: {topology.mode}"
    )


def build_scenario_manifest(
    config: ScenarioConfig,
    output_root: Path,
    topology_manifest: dict[str, Any],
    topology_summary: dict[str, Any],
    compute_profile_sha256: str,
    placement_node_ids: tuple[int, ...],
    compute_nodes_per_orbit: tuple[int, ...],
    *,
    reference_scenario_sha256: str | None = None,
    downsample_interval_s: int | None = None,
) -> dict[str, Any]:
    canonical_config = config.input_dict()
    config_sha256 = sha256_bytes(compact_json_bytes(canonical_config))
    topology_manifest_path = (
        Path(TOPOLOGY_DIRECTORY) / "manifest.json"
    )
    topology_manifest_sha256 = sha256_file(
        output_root / topology_manifest_path
    )
    topology_aggregate = topology_summary["aggregate_data_sha256"]
    aggregate_scenario = scenario_aggregate_sha256(
        topology_manifest_sha256,
        topology_aggregate,
        compute_profile_sha256,
        config_sha256,
    )
    satcompute_commit, worktree_clean = git_repository_state()
    compute_rate = config.compute.compute_rate_work_units_per_second
    compute_count = len(placement_node_ids)
    schedule = config.topology.schedule
    orbit_sample_offset_s = (
        schedule.orbit_sample_offset_s
        if isinstance(schedule, DynamicSchedule)
        else None
    )
    return {
        "schema_version": config.schema_version,
        "scenario_name": config.scenario_name,
        "scenario_config": canonical_config,
        "scenario_config_sha256": config_sha256,
        "topology_mode": config.topology.mode,
        "topology_manifest_relative_path":
            topology_manifest_path.as_posix(),
        "topology_manifest_sha256": topology_manifest_sha256,
        "topology_aggregate_data_sha256": topology_aggregate,
        "snapshot_count": topology_summary["snapshot_count"],
        "first_time_s": topology_manifest["first_time_s"],
        "last_time_s": topology_manifest["last_time_s"],
        "orbit_sample_offset_s": orbit_sample_offset_s,
        "delay_mode": config.topology.delay_mode,
        "fixed_delay_us": config.topology.fixed_delay_us,
        "link_bandwidth_kbps": config.topology.link_bandwidth_kbps,
        "total_satellite_count": config.total_satellite_count,
        "compute_node_count": compute_count,
        "compute_node_ratio":
            compute_count / config.total_satellite_count,
        "placement_strategy": config.compute.placement_strategy,
        "compute_nodes_per_orbit": list(compute_nodes_per_orbit),
        "selected_compute_node_ids": list(placement_node_ids),
        "compute_rate_work_units_per_second": compute_rate,
        "aggregate_compute_rate_work_units_per_second":
            compute_count * compute_rate,
        "compute_profile_sha256": compute_profile_sha256,
        "satcompute_commit": satcompute_commit,
        "satcompute_worktree_clean": worktree_clean,
        "python_version": platform.python_version(),
        "uv_version": uv_version(),
        "uv_lock_sha256": sha256_file(REPOSITORY_ROOT / "uv.lock"),
        "reference_scenario_sha256": reference_scenario_sha256,
        "downsample_interval_s": downsample_interval_s,
        "aggregate_scenario_sha256": aggregate_scenario,
    }


def generate_scenario(
    config_path: Path,
    output_dir: Path,
    visualization_config_path: Path | None = None,
) -> dict[str, Any]:
    """Generate, validate, and atomically publish one complete scenario."""
    config = load_config(Path(config_path))
    placement = even_plane_slot_placement(
        config.constellation.num_orbits,
        config.constellation.satellites_per_orbit,
        config.compute.compute_node_count,
    )
    try:
        with atomic_output_directory(Path(output_dir)) as temporary:
            topology_config_path = temporary / TOPOLOGY_CONFIG_FILENAME
            topology_config_path.write_bytes(
                compact_json_bytes(config.constellation.input_dict())
            )
            topology_manifest = _generate_topology(
                config,
                topology_config_path,
                temporary,
            )
            topology_config_path.unlink()

            resources_dir = temporary / RESOURCES_DIRECTORY
            resources_dir.mkdir()
            profile = build_compute_profile(
                placement.selected_node_ids,
                config.compute.compute_rate_work_units_per_second,
            )
            profile_path = resources_dir / COMPUTE_PROFILE_FILENAME
            profile_path.write_bytes(compute_profile_bytes(profile))
            profile_sha256 = sha256_file(profile_path)

            topology_summary = check_export(
                temporary / TOPOLOGY_DIRECTORY,
                expected_node_count=config.total_satellite_count,
            )
            scenario_manifest = build_scenario_manifest(
                config,
                temporary,
                topology_manifest,
                topology_summary,
                profile_sha256,
                placement.selected_node_ids,
                placement.compute_nodes_per_orbit,
            )
            (temporary / SCENARIO_MANIFEST_FILENAME).write_bytes(
                compact_json_bytes(scenario_manifest)
            )
            check_scenario(temporary)
    except AtomicOutputError as error:
        raise ScenarioGenerationError(str(error)) from error
    if visualization_config_path is not None:
        try:
            _run_optional_visualization(
                Path(output_dir).resolve(),
                Path(visualization_config_path).resolve(),
            )
        except (OSError, ValueError, RuntimeError) as error:
            raise ScenarioVisualizationError(
                "scenario was published successfully, but optional "
                f"visualization failed: {error}"
            ) from error
    return scenario_manifest


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--visualization-config",
        type=Path,
        help=(
            "after atomic scenario publication, optionally launch the "
            "separate orbit viewer"
        ),
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        manifest = generate_scenario(
            arguments.config.resolve(),
            arguments.output_dir.absolute(),
            (
                None
                if arguments.visualization_config is None
                else arguments.visualization_config.resolve()
            ),
        )
    except (OSError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(compact_json(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
