#!/usr/bin/env python3
"""Export PR3 dynamic ISLs as canonical SatCompute snapshot pairs."""

from __future__ import annotations

import argparse
import json
import math
import platform
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ..check_export import check_export
from ..common.atomic_output import (
    AtomicOutputError,
    atomic_output_directory,
)
from ..common.configuration import ConstellationConfig, parse_config
from ..common.hash_utils import (
    REPOSITORY_ROOT,
    aggregate_data_sha256,
    compact_json,
    compact_json_bytes,
    git_repository_state,
    sha256_bytes,
    sha256_file,
    uv_version,
)
from ..common.satcompute_schema import (
    DELAY_ROUNDING,
    SPEED_OF_LIGHT_M_PER_S,
    SatComputeLink,
    SatComputeSchemaError,
    distance_delay_us,
    nodes_payload,
    read_json,
    satcompute_json_bytes,
    topology_payload,
    validate_delay_parameters,
    validate_link_bandwidth_kbps,
)
from .dynamic_isls import (
    build_candidate_isls,
    candidate_degree_profile,
)


CANDIDATE_FILENAME = "candidate-isls.json"
SOURCE_SNAPSHOT_FILENAME = "isl-snapshots.jsonl"
MANIFEST_FILENAME = "manifest.json"
EXPECTED_SOURCE_FILES = frozenset(
    (CANDIDATE_FILENAME, SOURCE_SNAPSHOT_FILENAME, MANIFEST_FILENAME)
)


class DynamicTopologyExportError(ValueError):
    """Raised when a PR3 source cannot be exported safely."""


@dataclass(frozen=True, order=True)
class SourceActiveIsl:
    """One validated active edge and its rounded PR3 distance."""

    node1_id: int
    node2_id: int
    distance_m: float


@dataclass(frozen=True)
class DynamicSource:
    """Validated PR3 manifest, configuration, and ordered snapshots."""

    directory: Path
    manifest: dict[str, Any]
    config: ConstellationConfig
    snapshots: tuple[tuple[int, tuple[SourceActiveIsl, ...]], ...]


def _require_integer(value: Any, name: str, minimum: int = 0) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < minimum
    ):
        raise DynamicTopologyExportError(
            f"{name} must be an integer >= {minimum}"
        )
    return value


def _read_dynamic_source(source_dir: Path) -> DynamicSource:
    if not source_dir.is_dir():
        raise DynamicTopologyExportError(
            f"source dynamic directory does not exist: {source_dir}"
        )
    entries = tuple(source_dir.iterdir())
    if any(not path.is_file() for path in entries):
        raise DynamicTopologyExportError(
            "source dynamic directory contains non-files"
        )
    names = frozenset(path.name for path in entries)
    if names != EXPECTED_SOURCE_FILES:
        raise DynamicTopologyExportError(
            f"source dynamic files are inconsistent: {sorted(names)}"
        )
    manifest = read_json(source_dir / MANIFEST_FILENAME)
    candidate = read_json(source_dir / CANDIDATE_FILENAME)
    if not isinstance(manifest, dict) or not isinstance(candidate, dict):
        raise DynamicTopologyExportError(
            "source candidate and manifest roots must be objects"
        )
    candidate_bytes = (source_dir / CANDIDATE_FILENAME).read_bytes()
    snapshot_bytes = (source_dir / SOURCE_SNAPSHOT_FILENAME).read_bytes()
    if manifest.get("candidate_file_sha256") != sha256_bytes(candidate_bytes):
        raise DynamicTopologyExportError(
            "source candidate SHA-256 is inconsistent"
        )
    if manifest.get("snapshot_jsonl_sha256") != sha256_bytes(snapshot_bytes):
        raise DynamicTopologyExportError(
            "source snapshot SHA-256 is inconsistent"
        )
    if manifest.get("aggregate_sha256") != sha256_bytes(
        candidate_bytes + snapshot_bytes
    ):
        raise DynamicTopologyExportError(
            "source dynamic aggregate SHA-256 is inconsistent"
        )
    try:
        config = parse_config(manifest["physical_config"])
    except (KeyError, ValueError) as error:
        raise DynamicTopologyExportError(
            f"source physical_config is invalid: {error}"
        ) from error
    candidates = build_candidate_isls(config)
    expected_profile = candidate_degree_profile(config, candidates)
    if candidate.get("node_count") != config.expected_satellite_count:
        raise DynamicTopologyExportError(
            "source candidate node_count is inconsistent"
        )
    if candidate.get("candidate_count") != len(candidates):
        raise DynamicTopologyExportError(
            "source candidate_count is inconsistent"
        )
    if manifest.get("candidate_count") != len(candidates):
        raise DynamicTopologyExportError(
            "source manifest candidate_count is inconsistent"
        )
    if manifest.get("candidate_degree_profile") != expected_profile:
        raise DynamicTopologyExportError(
            "source candidate degree profile is inconsistent"
        )

    try:
        decoded_snapshots = tuple(
            json.loads(line)
            for line in snapshot_bytes.decode("utf-8").splitlines()
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise DynamicTopologyExportError(
            f"cannot parse source dynamic snapshots: {error}"
        ) from error
    snapshot_count = _require_integer(
        manifest.get("snapshot_count"),
        "source snapshot_count",
        1,
    )
    if snapshot_count != len(decoded_snapshots):
        raise DynamicTopologyExportError(
            "source snapshot_count is inconsistent"
        )
    duration_s = _require_integer(
        manifest.get("duration_s"),
        "source duration_s",
    )
    step_s = _require_integer(
        manifest.get("step_s"),
        "source step_s",
        1,
    )
    orbit_sample_offset_s = _require_integer(
        manifest.get("orbit_sample_offset_s"),
        "source orbit_sample_offset_s",
    )
    if orbit_sample_offset_s > (1 << 63) - 1 - duration_s:
        raise DynamicTopologyExportError(
            "source orbit_sample_offset_s + duration_s exceeds INT64_MAX"
        )
    expected_times = tuple(range(0, duration_s + 1, step_s))
    if snapshot_count != len(expected_times):
        raise DynamicTopologyExportError(
            "source dynamic schedule is inconsistent"
        )
    parsed_snapshots = []
    for expected_time, snapshot in zip(expected_times, decoded_snapshots):
        if not isinstance(snapshot, dict):
            raise DynamicTopologyExportError(
                "source dynamic snapshot must be an object"
            )
        if snapshot.get("time_s") != expected_time:
            raise DynamicTopologyExportError(
                "source dynamic snapshot times are inconsistent"
            )
        active_items = snapshot.get("active_isls")
        if not isinstance(active_items, list):
            raise DynamicTopologyExportError(
                "source active_isls must be an array"
            )
        active = []
        seen = set()
        for item in active_items:
            if not isinstance(item, dict) or set(item) != {
                "node1_id",
                "node2_id",
                "distance_m",
            }:
                raise DynamicTopologyExportError(
                    "source active ISL fields are invalid"
                )
            node1_id = _require_integer(item["node1_id"], "node1_id")
            node2_id = _require_integer(item["node2_id"], "node2_id")
            distance_m = item["distance_m"]
            if (
                node1_id >= node2_id
                or node2_id >= config.expected_satellite_count
            ):
                raise DynamicTopologyExportError(
                    "source active ISL endpoints are invalid"
                )
            if (
                not isinstance(distance_m, (int, float))
                or isinstance(distance_m, bool)
                or not math.isfinite(distance_m)
                or distance_m < 0.0
            ):
                raise DynamicTopologyExportError(
                    "source active ISL distance is invalid"
                )
            key = (node1_id, node2_id)
            if key in seen:
                raise DynamicTopologyExportError(
                    "source active ISLs contain duplicates"
                )
            seen.add(key)
            active.append(
                SourceActiveIsl(node1_id, node2_id, float(distance_m))
            )
        active_tuple = tuple(active)
        if active_tuple != tuple(sorted(active_tuple)):
            raise DynamicTopologyExportError(
                "source active ISLs are not sorted"
            )
        if snapshot.get("active_count") != len(active_tuple):
            raise DynamicTopologyExportError(
                "source active_count is inconsistent"
            )
        parsed_snapshots.append((expected_time, active_tuple))
    if len(parsed_snapshots) != len(expected_times):
        raise DynamicTopologyExportError(
            "source dynamic schedule is incomplete"
        )
    return DynamicSource(
        directory=source_dir,
        manifest=manifest,
        config=config,
        snapshots=tuple(parsed_snapshots),
    )


def _snapshot_links(
    active_isls: tuple[SourceActiveIsl, ...],
    *,
    delay_mode: str,
    fixed_delay_us: int | None,
    link_bandwidth_kbps: int,
) -> tuple[SatComputeLink, ...]:
    return tuple(
        SatComputeLink(
            node1_id=item.node1_id,
            node2_id=item.node2_id,
            delay_us=(
                fixed_delay_us
                if delay_mode == "fixed"
                else distance_delay_us(item.distance_m)
            ),
            link_bandwidth_kbps=link_bandwidth_kbps,
        )
        for item in active_isls
    )


def _build_manifest(
    *,
    source: DynamicSource,
    active_counts: tuple[int, ...],
    delay_mode: str,
    fixed_delay_us: int | None,
    link_bandwidth_kbps: int,
    aggregate_sha256: str,
) -> dict[str, Any]:
    satcompute_commit, worktree_clean = git_repository_state()
    source_manifest = source.manifest
    times = tuple(time_s for time_s, _ in source.snapshots)
    return {
        "schema_version": source.config.schema_version,
        "generator": "satcompute-topology-export",
        "generation_mode": "dynamic",
        "satcompute_commit": satcompute_commit,
        "satcompute_worktree_clean": worktree_clean,
        "hypatia_repository": source_manifest["hypatia_repository"],
        "hypatia_commit": source_manifest["hypatia_commit"],
        "hypatia_integration_mode": source_manifest[
            "hypatia_integration_mode"
        ],
        "python_version": platform.python_version(),
        "uv_version": uv_version(),
        "uv_lock_sha256": sha256_file(REPOSITORY_ROOT / "uv.lock"),
        "physical_config": source.config.input_dict(),
        "isl_candidate_strategy": source.config.isl_candidate_strategy,
        "seam_enabled": source.config.seam_enabled,
        "candidate_count": source_manifest["candidate_count"],
        "candidate_degree_profile": source_manifest[
            "candidate_degree_profile"
        ],
        "source_dynamic_manifest_sha256": sha256_file(
            source.directory / MANIFEST_FILENAME
        ),
        "source_dynamic_aggregate_sha256": source_manifest[
            "aggregate_sha256"
        ],
        "duration_s": source_manifest["duration_s"],
        "step_s": source_manifest["step_s"],
        "orbit_sample_offset_s": source_manifest[
            "orbit_sample_offset_s"
        ],
        "snapshot_count": len(times),
        "first_time_s": times[0],
        "last_time_s": times[-1],
        "node_count": source.config.expected_satellite_count,
        "min_active_count": min(active_counts),
        "max_active_count": max(active_counts),
        "average_active_count": sum(active_counts) / len(active_counts),
        "delay_mode": delay_mode,
        "fixed_delay_us": fixed_delay_us,
        "speed_of_light_m_per_s": SPEED_OF_LIGHT_M_PER_S,
        "delay_rounding": DELAY_ROUNDING,
        "link_bandwidth_kbps": link_bandwidth_kbps,
        "aggregate_data_sha256": aggregate_sha256,
    }


def export_satcompute_topology(
    source_dir: Path,
    delay_mode: str,
    fixed_delay_us: int | None,
    link_bandwidth_kbps: int,
    output_dir: Path,
) -> dict[str, Any]:
    """Export, check, and atomically publish all PR3 snapshot times."""
    try:
        validate_delay_parameters(delay_mode, fixed_delay_us)
        bandwidth = validate_link_bandwidth_kbps(link_bandwidth_kbps)
    except SatComputeSchemaError as error:
        raise DynamicTopologyExportError(str(error)) from error
    source = _read_dynamic_source(source_dir)
    try:
        with atomic_output_directory(output_dir) as temporary:
            data_names = []
            active_counts = []
            nodes_bytes = satcompute_json_bytes(
                nodes_payload(source.config.expected_satellite_count)
            )
            for time_s, active_isls in source.snapshots:
                nodes_name = f"nodes_{time_s}s.json"
                topology_name = f"topology_{time_s}s.json"
                links = _snapshot_links(
                    active_isls,
                    delay_mode=delay_mode,
                    fixed_delay_us=fixed_delay_us,
                    link_bandwidth_kbps=bandwidth,
                )
                (temporary / nodes_name).write_bytes(nodes_bytes)
                (temporary / topology_name).write_bytes(
                    satcompute_json_bytes(
                        topology_payload(
                            links,
                            source.config.expected_satellite_count,
                        )
                    )
                )
                data_names.extend((nodes_name, topology_name))
                active_counts.append(len(links))
            aggregate_sha256 = aggregate_data_sha256(
                temporary,
                data_names,
            )
            manifest = _build_manifest(
                source=source,
                active_counts=tuple(active_counts),
                delay_mode=delay_mode,
                fixed_delay_us=fixed_delay_us,
                link_bandwidth_kbps=bandwidth,
                aggregate_sha256=aggregate_sha256,
            )
            (temporary / MANIFEST_FILENAME).write_bytes(
                compact_json_bytes(manifest)
            )
            check_export(
                temporary,
                expected_node_count=source.config.expected_satellite_count,
                source_dynamic_dir=source.directory,
            )
        return manifest
    except AtomicOutputError as error:
        raise DynamicTopologyExportError(str(error)) from error


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument(
        "--delay-mode",
        choices=("fixed", "distance"),
        required=True,
    )
    parser.add_argument("--fixed-delay-us", type=int)
    parser.add_argument("--link-bandwidth-kbps", type=int, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        manifest = export_satcompute_topology(
            arguments.source_dir.resolve(),
            arguments.delay_mode,
            arguments.fixed_delay_us,
            arguments.link_bandwidth_kbps,
            arguments.output_dir.absolute(),
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(compact_json(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
