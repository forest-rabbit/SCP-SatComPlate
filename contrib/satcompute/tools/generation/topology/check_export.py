#!/usr/bin/env python3
"""Validate exported canonical SatCompute topology snapshots."""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

from .common.hash_utils import (
    aggregate_data_sha256,
    compact_json,
    sha256_bytes,
    sha256_file,
)
from .common.satcompute_schema import (
    DELAY_ROUNDING,
    SPEED_OF_LIGHT_M_PER_S,
    SatComputeLink,
    distance_delay_us,
    parse_nodes_payload,
    parse_topology_payload,
    read_json,
)


MANIFEST_FILENAME = "manifest.json"
SNAPSHOT_PATTERN = re.compile(
    r"(?P<kind>nodes|topology)_(?P<time>0|[1-9][0-9]*)s[.]json"
)
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
REQUIRED_MANIFEST_FIELDS = frozenset(
    (
        "schema_version",
        "generator",
        "generation_mode",
        "satcompute_commit",
        "satcompute_worktree_clean",
        "hypatia_repository",
        "hypatia_commit",
        "hypatia_integration_mode",
        "python_version",
        "uv_version",
        "uv_lock_sha256",
        "physical_config",
        "isl_candidate_strategy",
        "seam_enabled",
        "candidate_count",
        "candidate_degree_profile",
        "source_dynamic_manifest_sha256",
        "source_dynamic_aggregate_sha256",
        "duration_s",
        "step_s",
        "snapshot_count",
        "first_time_s",
        "last_time_s",
        "node_count",
        "min_active_count",
        "max_active_count",
        "average_active_count",
        "delay_mode",
        "fixed_delay_us",
        "speed_of_light_m_per_s",
        "delay_rounding",
        "link_bandwidth_kbps",
        "aggregate_data_sha256",
    )
)


class ExportCheckError(ValueError):
    """Raised when an exported topology violates the canonical contract."""


def _require_integer(
    value: Any,
    name: str,
    *,
    minimum: int = 0,
) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < minimum
    ):
        raise ExportCheckError(
            f"{name} must be an integer >= {minimum}"
        )
    return value


def _require_string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value:
        raise ExportCheckError(f"{name} must be a non-empty string")
    return value


def _require_sha256(value: Any, name: str) -> str:
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise ExportCheckError(f"{name} must be a lowercase SHA-256")
    return value


def _scan_snapshot_files(
    input_dir: Path,
) -> tuple[tuple[int, ...], dict[int, Path], dict[int, Path]]:
    if not input_dir.is_dir():
        raise ExportCheckError(f"input directory does not exist: {input_dir}")
    nodes_by_time: dict[int, Path] = {}
    topology_by_time: dict[int, Path] = {}
    for entry in input_dir.iterdir():
        if not entry.is_file():
            raise ExportCheckError(f"unexpected output entry: {entry.name}")
        if entry.name == MANIFEST_FILENAME:
            continue
        match = SNAPSHOT_PATTERN.fullmatch(entry.name)
        if match is None:
            if entry.name.startswith(("nodes_", "topology_")):
                raise ExportCheckError(
                    f"malformed snapshot filename: {entry.name}"
                )
            raise ExportCheckError(f"unexpected output file: {entry.name}")
        time_s = int(match.group("time"))
        target = (
            nodes_by_time
            if match.group("kind") == "nodes"
            else topology_by_time
        )
        if time_s in target:
            raise ExportCheckError(
                f"duplicate {match.group('kind')} snapshot at {time_s}s"
            )
        target[time_s] = entry
    if not (input_dir / MANIFEST_FILENAME).is_file():
        raise ExportCheckError("manifest.json is missing")
    times = tuple(sorted(set(nodes_by_time) | set(topology_by_time)))
    if not times or times[0] != 0:
        raise ExportCheckError(
            "nodes_0s.json and topology_0s.json must exist"
        )
    for time_s in times:
        if time_s not in nodes_by_time or time_s not in topology_by_time:
            raise ExportCheckError(
                f"{time_s}s must contain paired nodes and topology files"
            )
    return times, nodes_by_time, topology_by_time


def _read_snapshots(
    times: tuple[int, ...],
    nodes_by_time: dict[int, Path],
    topology_by_time: dict[int, Path],
) -> tuple[tuple[int, ...], dict[int, tuple[SatComputeLink, ...]]]:
    initial_node_ids: tuple[int, ...] | None = None
    links_by_time = {}
    for time_s in times:
        node_ids = parse_nodes_payload(read_json(nodes_by_time[time_s]))
        if initial_node_ids is None:
            initial_node_ids = node_ids
        elif node_ids != initial_node_ids:
            raise ExportCheckError(
                f"node set changed at {time_s}s"
            )
        links_by_time[time_s] = parse_topology_payload(
            read_json(topology_by_time[time_s]),
            node_ids,
        )
    if initial_node_ids is None:
        raise ExportCheckError("no node snapshots were parsed")
    return initial_node_ids, links_by_time


def _validate_manifest(
    manifest: Any,
    *,
    times: tuple[int, ...],
    node_ids: tuple[int, ...],
    links_by_time: dict[int, tuple[SatComputeLink, ...]],
    aggregate_sha256: str,
) -> None:
    if not isinstance(manifest, dict):
        raise ExportCheckError("manifest root must be an object")
    missing = sorted(REQUIRED_MANIFEST_FIELDS - set(manifest))
    if missing:
        raise ExportCheckError(f"manifest fields are missing: {missing}")
    if manifest["schema_version"] != "0.1":
        raise ExportCheckError("manifest schema_version must be 0.1")
    if manifest["generator"] != "satcompute-topology-export":
        raise ExportCheckError(
            "manifest generator must be satcompute-topology-export"
        )
    generation_mode = manifest["generation_mode"]
    if generation_mode not in ("static", "dynamic"):
        raise ExportCheckError(
            "manifest generation_mode must be static or dynamic"
        )
    _require_string(manifest["satcompute_commit"], "satcompute_commit")
    if (
        len(manifest["satcompute_commit"]) != 40
        or any(
            character not in "0123456789abcdef"
            for character in manifest["satcompute_commit"]
        )
    ):
        raise ExportCheckError(
            "satcompute_commit must be a full lowercase commit SHA"
        )
    if not isinstance(manifest["satcompute_worktree_clean"], bool):
        raise ExportCheckError("satcompute_worktree_clean must be boolean")
    for field in (
        "hypatia_repository",
        "hypatia_commit",
        "hypatia_integration_mode",
        "python_version",
        "uv_version",
        "isl_candidate_strategy",
    ):
        _require_string(manifest[field], field)
    _require_sha256(manifest["uv_lock_sha256"], "uv_lock_sha256")
    if not isinstance(manifest["physical_config"], dict):
        raise ExportCheckError("physical_config must be an object")
    if manifest["isl_candidate_strategy"] != "plus-grid":
        raise ExportCheckError(
            "isl_candidate_strategy must be plus-grid"
        )
    if not isinstance(manifest["seam_enabled"], bool):
        raise ExportCheckError("seam_enabled must be boolean")
    candidate_count = _require_integer(
        manifest["candidate_count"],
        "candidate_count",
        minimum=1,
    )
    degree_profile = manifest["candidate_degree_profile"]
    expected_degree_fields = {
        "minimum_total_degree",
        "maximum_total_degree",
        "boundary_plane_total_degree",
        "internal_plane_total_degree",
    }
    if (
        not isinstance(degree_profile, dict)
        or set(degree_profile) != expected_degree_fields
    ):
        raise ExportCheckError("candidate_degree_profile fields are invalid")
    for field in expected_degree_fields:
        _require_integer(
            degree_profile[field],
            f"candidate_degree_profile.{field}",
        )

    snapshot_count = _require_integer(
        manifest["snapshot_count"],
        "snapshot_count",
        minimum=1,
    )
    if snapshot_count != len(times):
        raise ExportCheckError("manifest snapshot_count is inconsistent")
    if manifest["first_time_s"] != times[0]:
        raise ExportCheckError("manifest first_time_s is inconsistent")
    if manifest["last_time_s"] != times[-1]:
        raise ExportCheckError("manifest last_time_s is inconsistent")
    if manifest["node_count"] != len(node_ids):
        raise ExportCheckError("manifest node_count is inconsistent")

    active_counts = [len(links_by_time[time_s]) for time_s in times]
    if candidate_count < max(active_counts):
        raise ExportCheckError(
            "candidate_count is smaller than an active link count"
        )
    if manifest["min_active_count"] != min(active_counts):
        raise ExportCheckError("manifest min_active_count is inconsistent")
    if manifest["max_active_count"] != max(active_counts):
        raise ExportCheckError("manifest max_active_count is inconsistent")
    expected_average = sum(active_counts) / len(active_counts)
    average = manifest["average_active_count"]
    if (
        not isinstance(average, (int, float))
        or isinstance(average, bool)
        or not math.isfinite(average)
        or float(average) != expected_average
    ):
        raise ExportCheckError(
            "manifest average_active_count is inconsistent"
        )

    duration_s = _require_integer(manifest["duration_s"], "duration_s")
    if generation_mode == "static":
        if manifest["step_s"] is not None:
            raise ExportCheckError("static manifest step_s must be null")
        if duration_s != times[-1] or len(times) != 1:
            raise ExportCheckError("static manifest schedule is inconsistent")
        if (
            manifest["source_dynamic_manifest_sha256"] is not None
            or manifest["source_dynamic_aggregate_sha256"] is not None
        ):
            raise ExportCheckError(
                "static source dynamic hashes must be null"
            )
    else:
        step_s = _require_integer(
            manifest["step_s"],
            "step_s",
            minimum=1,
        )
        if (
            duration_s != times[-1]
            or tuple(range(0, duration_s + 1, step_s)) != times
        ):
            raise ExportCheckError("dynamic manifest schedule is inconsistent")
        _require_sha256(
            manifest["source_dynamic_manifest_sha256"],
            "source_dynamic_manifest_sha256",
        )
        _require_sha256(
            manifest["source_dynamic_aggregate_sha256"],
            "source_dynamic_aggregate_sha256",
        )

    delay_mode = manifest["delay_mode"]
    if delay_mode not in ("fixed", "distance"):
        raise ExportCheckError("delay_mode must be fixed or distance")
    if manifest["speed_of_light_m_per_s"] != SPEED_OF_LIGHT_M_PER_S:
        raise ExportCheckError("speed_of_light_m_per_s is inconsistent")
    if manifest["delay_rounding"] != DELAY_ROUNDING:
        raise ExportCheckError("delay_rounding is inconsistent")
    bandwidth = _require_integer(
        manifest["link_bandwidth_kbps"],
        "link_bandwidth_kbps",
        minimum=1,
    )
    if delay_mode == "fixed":
        fixed_delay = _require_integer(
            manifest["fixed_delay_us"],
            "fixed_delay_us",
        )
    else:
        if manifest["fixed_delay_us"] is not None:
            raise ExportCheckError(
                "distance mode fixed_delay_us must be null"
            )
        fixed_delay = None
    for links in links_by_time.values():
        for link in links:
            if link.link_bandwidth_kbps != bandwidth:
                raise ExportCheckError(
                    "snapshot link bandwidth differs from manifest"
                )
            if fixed_delay is not None and link.delay_us != fixed_delay:
                raise ExportCheckError(
                    "fixed snapshot delay differs from manifest"
                )
    if manifest["aggregate_data_sha256"] != aggregate_sha256:
        raise ExportCheckError("aggregate_data_sha256 is inconsistent")


def _load_source_dynamic(
    source_dir: Path,
) -> tuple[dict[str, Any], tuple[dict[str, Any], ...]]:
    expected_names = {
        "candidate-isls.json",
        "isl-snapshots.jsonl",
        "manifest.json",
    }
    if not source_dir.is_dir():
        raise ExportCheckError(
            f"source dynamic directory does not exist: {source_dir}"
        )
    entries = tuple(source_dir.iterdir())
    if any(not path.is_file() for path in entries):
        raise ExportCheckError("source dynamic directory contains non-files")
    names = {path.name for path in entries}
    if names != expected_names:
        raise ExportCheckError(
            f"source dynamic files are inconsistent: {sorted(names)}"
        )
    manifest = read_json(source_dir / "manifest.json")
    if not isinstance(manifest, dict):
        raise ExportCheckError("source dynamic manifest must be an object")
    candidate_bytes = (source_dir / "candidate-isls.json").read_bytes()
    snapshot_bytes = (source_dir / "isl-snapshots.jsonl").read_bytes()
    if manifest.get("candidate_file_sha256") != sha256_bytes(candidate_bytes):
        raise ExportCheckError("source candidate SHA-256 is inconsistent")
    if manifest.get("snapshot_jsonl_sha256") != sha256_bytes(snapshot_bytes):
        raise ExportCheckError("source snapshot SHA-256 is inconsistent")
    if manifest.get("aggregate_sha256") != sha256_bytes(
        candidate_bytes + snapshot_bytes
    ):
        raise ExportCheckError("source dynamic aggregate SHA-256 is inconsistent")
    try:
        snapshots = tuple(
            json.loads(line)
            for line in snapshot_bytes.decode("utf-8").splitlines()
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ExportCheckError(
            f"cannot parse source dynamic snapshots: {error}"
        ) from error
    if manifest.get("snapshot_count") != len(snapshots):
        raise ExportCheckError("source dynamic snapshot_count is inconsistent")
    return manifest, snapshots


def _validate_source_dynamic(
    source_dir: Path,
    export_manifest: dict[str, Any],
    times: tuple[int, ...],
    links_by_time: dict[int, tuple[SatComputeLink, ...]],
) -> None:
    source_manifest, snapshots = _load_source_dynamic(source_dir)
    if export_manifest["source_dynamic_manifest_sha256"] != sha256_file(
        source_dir / "manifest.json"
    ):
        raise ExportCheckError("source dynamic manifest SHA-256 is inconsistent")
    if export_manifest["source_dynamic_aggregate_sha256"] != source_manifest.get(
        "aggregate_sha256"
    ):
        raise ExportCheckError("source dynamic aggregate reference is inconsistent")
    for field in (
        "physical_config",
        "isl_candidate_strategy",
        "candidate_count",
        "candidate_degree_profile",
        "hypatia_repository",
        "hypatia_commit",
        "hypatia_integration_mode",
    ):
        if export_manifest[field] != source_manifest.get(field):
            raise ExportCheckError(
                f"export manifest {field} differs from dynamic source"
            )
    if len(snapshots) != len(times):
        raise ExportCheckError("source dynamic snapshot count is inconsistent")
    delay_mode = export_manifest["delay_mode"]
    for time_s, source_snapshot in zip(times, snapshots):
        if not isinstance(source_snapshot, dict):
            raise ExportCheckError("source dynamic snapshot must be an object")
        if source_snapshot.get("time_s") != time_s:
            raise ExportCheckError("source dynamic times differ from export")
        active_isls = source_snapshot.get("active_isls")
        if not isinstance(active_isls, list):
            raise ExportCheckError("source active_isls must be an array")
        source_edges = []
        source_distances = {}
        for item in active_isls:
            if not isinstance(item, dict):
                raise ExportCheckError("source active ISL must be an object")
            node1_id = item.get("node1_id")
            node2_id = item.get("node2_id")
            distance_m = item.get("distance_m")
            if (
                not isinstance(node1_id, int)
                or isinstance(node1_id, bool)
                or not isinstance(node2_id, int)
                or isinstance(node2_id, bool)
                or node1_id >= node2_id
            ):
                raise ExportCheckError("source active ISL endpoints are invalid")
            key = (node1_id, node2_id)
            if key in source_distances:
                raise ExportCheckError("source active ISLs contain duplicates")
            source_edges.append(key)
            source_distances[key] = distance_m
        if source_edges != sorted(source_edges):
            raise ExportCheckError("source active ISLs are not sorted")
        exported = links_by_time[time_s]
        exported_edges = [
            (link.node1_id, link.node2_id) for link in exported
        ]
        if exported_edges != source_edges:
            raise ExportCheckError(
                "exported active endpoints differ from dynamic source"
            )
        if delay_mode == "distance":
            for link in exported:
                key = (link.node1_id, link.node2_id)
                if link.delay_us != distance_delay_us(source_distances[key]):
                    raise ExportCheckError(
                        "distance-mode delay differs from dynamic source"
                    )


def check_export(
    input_dir: Path,
    *,
    expected_node_count: int | None = None,
    source_dynamic_dir: Path | None = None,
) -> dict[str, Any]:
    """Validate one export directory and return a compact audit summary."""
    root = Path(input_dir)
    times, nodes_by_time, topology_by_time = _scan_snapshot_files(root)
    node_ids, links_by_time = _read_snapshots(
        times,
        nodes_by_time,
        topology_by_time,
    )
    if expected_node_count is not None:
        expected = _require_integer(
            expected_node_count,
            "expected_node_count",
            minimum=1,
        )
        if len(node_ids) != expected:
            raise ExportCheckError(
                f"expected {expected} nodes, found {len(node_ids)}"
            )
    data_paths = tuple(
        path.name
        for time_s in times
        for path in (nodes_by_time[time_s], topology_by_time[time_s])
    )
    aggregate_sha256 = aggregate_data_sha256(root, data_paths)
    manifest = read_json(root / MANIFEST_FILENAME)
    _validate_manifest(
        manifest,
        times=times,
        node_ids=node_ids,
        links_by_time=links_by_time,
        aggregate_sha256=aggregate_sha256,
    )
    if source_dynamic_dir is not None:
        if manifest["generation_mode"] != "dynamic":
            raise ExportCheckError(
                "source_dynamic_dir is valid only for dynamic exports"
            )
        _validate_source_dynamic(
            Path(source_dynamic_dir),
            manifest,
            times,
            links_by_time,
        )
    active_counts = [len(links_by_time[time_s]) for time_s in times]
    return {
        "generation_mode": manifest["generation_mode"],
        "snapshot_count": len(times),
        "node_count": len(node_ids),
        "min_active_count": min(active_counts),
        "max_active_count": max(active_counts),
        "average_active_count": sum(active_counts) / len(active_counts),
        "aggregate_data_sha256": aggregate_sha256,
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--expected-node-count", type=int)
    parser.add_argument("--source-dynamic-dir", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        summary = check_export(
            arguments.input_dir.absolute(),
            expected_node_count=arguments.expected_node_count,
            source_dynamic_dir=(
                arguments.source_dynamic_dir.absolute()
                if arguments.source_dynamic_dir is not None
                else None
            ),
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(compact_json(summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
