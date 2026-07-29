#!/usr/bin/env python3
"""Validate one generated unified satellite-computing scenario."""

from __future__ import annotations

import argparse
import math
import re
import sys
from pathlib import Path
from typing import Any

from ..topology.check_export import check_export
from ..topology.common.hash_utils import (
    REPOSITORY_ROOT,
    compact_json,
    compact_json_bytes,
    sha256_bytes,
    sha256_file,
)
from ..topology.common.satcompute_schema import (
    parse_nodes_payload,
    read_json,
)
from .compute_placement import even_plane_slot_placement
from .compute_profile import parse_compute_profile
from .configuration import (
    DYNAMIC_MODE,
    STATIC_MODE,
    DynamicSchedule,
    StaticSchedule,
    parse_config,
)


SCENARIO_MANIFEST_FILENAME = "scenario-manifest.json"
TOPOLOGY_DIRECTORY = "topology"
RESOURCES_DIRECTORY = "resources"
COMPUTE_PROFILE_FILENAME = "compute-profile.json"
TOPOLOGY_MANIFEST_RELATIVE_PATH = "topology/manifest.json"
EXPECTED_ROOT_ENTRIES = frozenset(
    (
        TOPOLOGY_DIRECTORY,
        RESOURCES_DIRECTORY,
        SCENARIO_MANIFEST_FILENAME,
    )
)
EXPECTED_RESOURCE_ENTRIES = frozenset((COMPUTE_PROFILE_FILENAME,))
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
MANIFEST_FIELDS = frozenset(
    (
        "schema_version",
        "scenario_name",
        "scenario_config",
        "scenario_config_sha256",
        "topology_mode",
        "topology_manifest_relative_path",
        "topology_manifest_sha256",
        "topology_aggregate_data_sha256",
        "snapshot_count",
        "first_time_s",
        "last_time_s",
        "orbit_sample_offset_s",
        "delay_mode",
        "fixed_delay_us",
        "link_bandwidth_kbps",
        "total_satellite_count",
        "compute_node_count",
        "compute_node_ratio",
        "placement_strategy",
        "compute_nodes_per_orbit",
        "selected_compute_node_ids",
        "compute_rate_work_units_per_second",
        "aggregate_compute_rate_work_units_per_second",
        "compute_profile_sha256",
        "satcompute_commit",
        "satcompute_worktree_clean",
        "python_version",
        "uv_version",
        "uv_lock_sha256",
        "aggregate_scenario_sha256",
    )
)


class ScenarioCheckError(ValueError):
    """Raised when a generated scenario violates its output contract."""


def scenario_aggregate_sha256(
    topology_manifest_sha256: str,
    topology_aggregate_data_sha256: str,
    compute_profile_sha256: str,
    scenario_config_sha256: str,
) -> str:
    """Hash the four non-recursive scenario content identities."""
    return sha256_bytes(
        compact_json_bytes(
            {
                "topology_manifest_sha256": topology_manifest_sha256,
                "topology_aggregate_data_sha256":
                    topology_aggregate_data_sha256,
                "compute_profile_sha256": compute_profile_sha256,
                "scenario_config_sha256": scenario_config_sha256,
            }
        )
    )


def _require_integer(value: Any, name: str, minimum: int = 0) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < minimum
    ):
        raise ScenarioCheckError(
            f"{name} must be an integer >= {minimum}"
        )
    return value


def _require_string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value:
        raise ScenarioCheckError(f"{name} must be a non-empty string")
    return value


def _require_integer_list(
    value: Any,
    name: str,
    *,
    minimum: int,
) -> list[int]:
    if not isinstance(value, list):
        raise ScenarioCheckError(f"{name} must be an array")
    return [
        _require_integer(item, f"{name}[{index}]", minimum)
        for index, item in enumerate(value)
    ]


def _require_sha256(value: Any, name: str) -> str:
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise ScenarioCheckError(f"{name} must be a lowercase SHA-256")
    return value


def _check_directory_shape(root: Path) -> tuple[Path, Path, Path]:
    if not root.is_dir() or root.is_symlink():
        raise ScenarioCheckError(
            f"scenario directory does not exist or is unsafe: {root}"
        )
    entries = {path.name for path in root.iterdir()}
    if entries != EXPECTED_ROOT_ENTRIES:
        raise ScenarioCheckError(
            f"scenario root entries differ: "
            f"missing={sorted(EXPECTED_ROOT_ENTRIES - entries)}, "
            f"unknown={sorted(entries - EXPECTED_ROOT_ENTRIES)}"
        )
    topology_dir = root / TOPOLOGY_DIRECTORY
    resources_dir = root / RESOURCES_DIRECTORY
    manifest_path = root / SCENARIO_MANIFEST_FILENAME
    if (
        not topology_dir.is_dir()
        or topology_dir.is_symlink()
        or not resources_dir.is_dir()
        or resources_dir.is_symlink()
        or not manifest_path.is_file()
        or manifest_path.is_symlink()
    ):
        raise ScenarioCheckError("scenario output entry types are invalid")
    resource_entries = {path.name for path in resources_dir.iterdir()}
    if resource_entries != EXPECTED_RESOURCE_ENTRIES:
        raise ScenarioCheckError(
            f"resource entries differ: "
            f"missing={sorted(EXPECTED_RESOURCE_ENTRIES - resource_entries)}, "
            f"unknown={sorted(resource_entries - EXPECTED_RESOURCE_ENTRIES)}"
        )
    profile_path = resources_dir / COMPUTE_PROFILE_FILENAME
    if not profile_path.is_file() or profile_path.is_symlink():
        raise ScenarioCheckError("compute-profile.json must be a regular file")
    return topology_dir, profile_path, manifest_path


def _check_provenance(
    manifest: dict[str, Any],
    topology_manifest: dict[str, Any],
) -> None:
    commit = manifest["satcompute_commit"]
    if not isinstance(commit, str) or COMMIT_PATTERN.fullmatch(commit) is None:
        raise ScenarioCheckError(
            "satcompute_commit must be a full lowercase commit SHA"
        )
    if not isinstance(manifest["satcompute_worktree_clean"], bool):
        raise ScenarioCheckError(
            "satcompute_worktree_clean must be boolean"
        )
    for field in ("python_version", "uv_version"):
        _require_string(manifest[field], field)
    _require_sha256(manifest["uv_lock_sha256"], "uv_lock_sha256")
    for field in (
        "satcompute_commit",
        "satcompute_worktree_clean",
        "python_version",
        "uv_version",
        "uv_lock_sha256",
    ):
        if manifest[field] != topology_manifest.get(field):
            raise ScenarioCheckError(
                f"{field} differs from topology manifest"
            )
    if manifest["uv_lock_sha256"] != sha256_file(
        REPOSITORY_ROOT / "uv.lock"
    ):
        raise ScenarioCheckError("uv_lock_sha256 is inconsistent")


def check_scenario(input_dir: Path) -> dict[str, Any]:
    """Validate one scenario directory and return a compact summary."""
    root = Path(input_dir)
    topology_dir, profile_path, manifest_path = _check_directory_shape(root)
    manifest = read_json(manifest_path)
    if not isinstance(manifest, dict):
        raise ScenarioCheckError("scenario manifest root must be an object")
    actual_fields = frozenset(manifest)
    if actual_fields != MANIFEST_FIELDS:
        raise ScenarioCheckError(
            f"scenario manifest fields differ: "
            f"missing={sorted(MANIFEST_FIELDS - actual_fields)}, "
            f"unknown={sorted(actual_fields - MANIFEST_FIELDS)}"
        )
    if manifest["schema_version"] != "0.1":
        raise ScenarioCheckError("schema_version must be 0.1")

    try:
        config = parse_config(manifest["scenario_config"])
    except ValueError as error:
        raise ScenarioCheckError(
            f"scenario_config is invalid: {error}"
        ) from error
    if manifest["scenario_config"] != config.input_dict():
        raise ScenarioCheckError("scenario_config is not canonical")
    if manifest["scenario_name"] != config.scenario_name:
        raise ScenarioCheckError("scenario_name differs from config")
    config_sha256 = sha256_bytes(
        compact_json_bytes(config.input_dict())
    )
    if manifest["scenario_config_sha256"] != config_sha256:
        raise ScenarioCheckError("scenario_config_sha256 is inconsistent")

    _require_integer(manifest["snapshot_count"], "snapshot_count", 1)
    _require_integer(manifest["first_time_s"], "first_time_s")
    _require_integer(manifest["last_time_s"], "last_time_s")
    _require_integer(
        manifest["link_bandwidth_kbps"],
        "link_bandwidth_kbps",
        1,
    )
    if manifest["fixed_delay_us"] is not None:
        _require_integer(manifest["fixed_delay_us"], "fixed_delay_us")
    _require_integer(
        manifest["total_satellite_count"],
        "total_satellite_count",
        1,
    )
    _require_integer(
        manifest["compute_node_count"],
        "compute_node_count",
        1,
    )
    _require_integer(
        manifest["compute_rate_work_units_per_second"],
        "compute_rate_work_units_per_second",
        1,
    )
    _require_integer(
        manifest["aggregate_compute_rate_work_units_per_second"],
        "aggregate_compute_rate_work_units_per_second",
        1,
    )
    compute_nodes_per_orbit = _require_integer_list(
        manifest["compute_nodes_per_orbit"],
        "compute_nodes_per_orbit",
        minimum=0,
    )
    selected_compute_node_ids = _require_integer_list(
        manifest["selected_compute_node_ids"],
        "selected_compute_node_ids",
        minimum=0,
    )

    topology_summary = check_export(
        topology_dir,
        expected_node_count=config.total_satellite_count,
    )
    topology_manifest_path = topology_dir / "manifest.json"
    topology_manifest = read_json(topology_manifest_path)
    if not isinstance(topology_manifest, dict):
        raise ScenarioCheckError("topology manifest must be an object")
    if (
        manifest["topology_manifest_relative_path"]
        != TOPOLOGY_MANIFEST_RELATIVE_PATH
    ):
        raise ScenarioCheckError(
            "topology_manifest_relative_path is inconsistent"
        )
    topology_manifest_sha256 = sha256_file(topology_manifest_path)
    if manifest["topology_manifest_sha256"] != topology_manifest_sha256:
        raise ScenarioCheckError(
            "topology_manifest_sha256 is inconsistent"
        )
    topology_aggregate = topology_summary["aggregate_data_sha256"]
    if (
        manifest["topology_aggregate_data_sha256"]
        != topology_aggregate
    ):
        raise ScenarioCheckError(
            "topology_aggregate_data_sha256 is inconsistent"
        )
    if topology_manifest.get("aggregate_data_sha256") != topology_aggregate:
        raise ScenarioCheckError(
            "topology aggregate differs from its manifest"
        )

    schedule = config.topology.schedule
    if (
        manifest["topology_mode"] != config.topology.mode
        or topology_manifest.get("generation_mode") != config.topology.mode
    ):
        raise ScenarioCheckError("topology mode is inconsistent")
    if isinstance(schedule, StaticSchedule):
        if (
            config.topology.mode != STATIC_MODE
            or topology_manifest.get("duration_s")
            != schedule.snapshot_time_s
            or topology_manifest.get("orbit_sample_offset_s") is not None
            or manifest["orbit_sample_offset_s"] is not None
        ):
            raise ScenarioCheckError("static sample time is inconsistent")
    elif isinstance(schedule, DynamicSchedule):
        _require_integer(
            manifest["orbit_sample_offset_s"],
            "orbit_sample_offset_s",
        )
        if (
            config.topology.mode != DYNAMIC_MODE
            or topology_manifest.get("duration_s") != schedule.duration_s
            or topology_manifest.get("step_s") != schedule.step_s
            or topology_manifest.get("orbit_sample_offset_s")
            != schedule.orbit_sample_offset_s
            or manifest["orbit_sample_offset_s"]
            != schedule.orbit_sample_offset_s
        ):
            raise ScenarioCheckError("dynamic schedule is inconsistent")
    else:
        raise ScenarioCheckError("scenario schedule type is invalid")
    for field in ("snapshot_count", "first_time_s", "last_time_s"):
        if manifest[field] != topology_manifest.get(field):
            raise ScenarioCheckError(
                f"{field} differs from topology manifest"
            )
    if manifest["snapshot_count"] != topology_summary["snapshot_count"]:
        raise ScenarioCheckError("snapshot_count is inconsistent")
    for field in (
        "delay_mode",
        "fixed_delay_us",
        "link_bandwidth_kbps",
    ):
        if (
            manifest[field] != getattr(config.topology, field)
            or manifest[field] != topology_manifest.get(field)
        ):
            raise ScenarioCheckError(f"{field} is inconsistent")

    node_ids = parse_nodes_payload(
        read_json(topology_dir / "nodes_0s.json")
    )
    profile_nodes = parse_compute_profile(
        read_json(profile_path),
        valid_node_ids=node_ids,
    )
    profile_sha256 = sha256_file(profile_path)
    if manifest["compute_profile_sha256"] != profile_sha256:
        raise ScenarioCheckError("compute_profile_sha256 is inconsistent")
    placement = even_plane_slot_placement(
        config.constellation.num_orbits,
        config.constellation.satellites_per_orbit,
        config.compute.compute_node_count,
    )
    selected_ids = tuple(node.node_id for node in profile_nodes)
    rates = tuple(
        node.compute_rate_work_units_per_second
        for node in profile_nodes
    )
    if selected_ids != placement.selected_node_ids:
        raise ScenarioCheckError(
            "compute profile differs from even-plane-slot placement"
        )
    if any(
        rate != config.compute.compute_rate_work_units_per_second
        for rate in rates
    ):
        raise ScenarioCheckError("compute profile rate differs from config")
    if manifest["total_satellite_count"] != len(node_ids):
        raise ScenarioCheckError("total_satellite_count is inconsistent")
    if manifest["compute_node_count"] != len(profile_nodes):
        raise ScenarioCheckError("compute_node_count is inconsistent")
    expected_ratio = len(profile_nodes) / len(node_ids)
    ratio = manifest["compute_node_ratio"]
    if (
        not isinstance(ratio, (int, float))
        or isinstance(ratio, bool)
        or not math.isfinite(ratio)
        or float(ratio) != expected_ratio
    ):
        raise ScenarioCheckError("compute_node_ratio is inconsistent")
    if (
        manifest["placement_strategy"]
        != config.compute.placement_strategy
    ):
        raise ScenarioCheckError("placement_strategy is inconsistent")
    if (
        compute_nodes_per_orbit
        != list(placement.compute_nodes_per_orbit)
    ):
        raise ScenarioCheckError(
            "compute_nodes_per_orbit is inconsistent"
        )
    if (
        selected_compute_node_ids
        != list(placement.selected_node_ids)
    ):
        raise ScenarioCheckError(
            "selected_compute_node_ids is inconsistent"
        )
    expected_rate = config.compute.compute_rate_work_units_per_second
    if manifest["compute_rate_work_units_per_second"] != expected_rate:
        raise ScenarioCheckError(
            "compute_rate_work_units_per_second is inconsistent"
        )
    aggregate_rate = len(profile_nodes) * expected_rate
    if (
        manifest["aggregate_compute_rate_work_units_per_second"]
        != aggregate_rate
    ):
        raise ScenarioCheckError(
            "aggregate compute rate is inconsistent"
        )

    for field in (
        "scenario_config_sha256",
        "topology_manifest_sha256",
        "topology_aggregate_data_sha256",
        "compute_profile_sha256",
        "aggregate_scenario_sha256",
    ):
        _require_sha256(manifest[field], field)
    expected_aggregate = scenario_aggregate_sha256(
        topology_manifest_sha256,
        topology_aggregate,
        profile_sha256,
        config_sha256,
    )
    if manifest["aggregate_scenario_sha256"] != expected_aggregate:
        raise ScenarioCheckError(
            "aggregate_scenario_sha256 is inconsistent"
        )
    _check_provenance(manifest, topology_manifest)
    return {
        "scenario_name": config.scenario_name,
        "topology_mode": config.topology.mode,
        "snapshot_count": topology_summary["snapshot_count"],
        "total_satellite_count": len(node_ids),
        "compute_node_count": len(profile_nodes),
        "aggregate_scenario_sha256": expected_aggregate,
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        summary = check_scenario(arguments.input_dir.absolute())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(compact_json(summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
