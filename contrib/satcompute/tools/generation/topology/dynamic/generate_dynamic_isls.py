#!/usr/bin/env python3
"""Generate deterministic range-gated dynamic ISL audit snapshots."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import sys
from pathlib import Path
from typing import Any

from ..common.configuration import ConstellationConfig, load_config
from ..orbit.hypatia.adapter import HypatiaAdapter
from ..orbit.hypatia.orbit_positions import load_orbit_constellation
from ..orbit.hypatia.resolve_constellation import (
    REPOSITORY_ROOT,
    sha256_file,
    uv_version,
)
from .dynamic_isls import (
    MINIMUM_ISL_RAY_ALTITUDE_M,
    STRATEGY,
    CandidateIsl,
    IslEdge,
    IslSnapshot,
    build_candidate_isls,
    candidate_degree_profile,
    clearance_limited_max_distance_m,
    generate_isl_snapshots,
    validate_clearance_limit,
)


TOOL_DIR = Path(__file__).resolve().parent
DEFAULT_CONFIG = TOOL_DIR.parent / "config" / "synthetic-66.json"
CANDIDATE_FILENAME = "candidate-isls.json"
SNAPSHOT_FILENAME = "isl-snapshots.jsonl"
MANIFEST_FILENAME = "manifest.json"
EXPECTED_OUTPUT_FILES = (
    CANDIDATE_FILENAME,
    SNAPSHOT_FILENAME,
    MANIFEST_FILENAME,
)


class DynamicIslGenerationError(RuntimeError):
    """Raised when dynamic ISL output cannot be generated safely."""


def generate_dynamic_isl_output(
    config_path: Path,
    duration_s: int,
    step_s: int,
    output_dir: Path,
) -> dict[str, Any]:
    """Generate all dynamic ISL files and atomically publish the directory."""
    times_s = validate_schedule(duration_s, step_s)
    config = load_config(config_path)
    clearance_floor_m = validate_clearance_limit(config)
    candidates = build_candidate_isls(config)
    degree_profile = candidate_degree_profile(config, candidates)
    temporary_dir = _temporary_output_dir(output_dir)
    _require_output_target_is_safe(output_dir)
    _remove_temporary_path(temporary_dir)

    adapter = HypatiaAdapter()
    orbit = load_orbit_constellation(config, adapter)
    snapshots = generate_isl_snapshots(
        orbit,
        candidates,
        times_s,
        config.max_isl_distance_m,
    )
    _validate_snapshot_sequence(candidates, snapshots, times_s)

    try:
        temporary_dir.mkdir(parents=True)
        candidate_payload = _candidate_payload(config, candidates)
        candidate_bytes = _json_bytes(candidate_payload)
        candidate_path = temporary_dir / CANDIDATE_FILENAME
        candidate_path.write_bytes(candidate_bytes)

        snapshot_payloads = [
            _snapshot_payload(snapshot) for snapshot in snapshots
        ]
        snapshot_lines = [
            _compact_json(payload).encode("utf-8")
            for payload in snapshot_payloads
        ]
        snapshot_bytes = b"".join(line + b"\n" for line in snapshot_lines)
        snapshot_path = temporary_dir / SNAPSHOT_FILENAME
        snapshot_path.write_bytes(snapshot_bytes)

        candidate_sha256 = _sha256_bytes(candidate_bytes)
        snapshot_sha256 = _sha256_bytes(snapshot_bytes)
        aggregate_sha256 = _sha256_bytes(candidate_bytes + snapshot_bytes)
        manifest = _manifest_payload(
            config=config,
            adapter=adapter,
            duration_s=duration_s,
            step_s=step_s,
            snapshots=snapshots,
            candidate_count=len(candidates),
            degree_profile=degree_profile,
            clearance_floor_m=clearance_floor_m,
            candidate_sha256=candidate_sha256,
            snapshot_sha256=snapshot_sha256,
            first_snapshot_sha256=_sha256_bytes(snapshot_lines[0]),
            last_snapshot_sha256=_sha256_bytes(snapshot_lines[-1]),
            aggregate_sha256=aggregate_sha256,
        )
        (temporary_dir / MANIFEST_FILENAME).write_bytes(
            _json_bytes(manifest)
        )
        _validate_written_output(
            temporary_dir,
            expected_snapshot_count=len(snapshots),
        )
        temporary_dir.replace(output_dir)
        return manifest
    except Exception:
        _remove_temporary_path(temporary_dir)
        raise


def validate_schedule(duration_s: int, step_s: int) -> tuple[int, ...]:
    """Validate the inclusive integer schedule and return all sample times."""
    if (
        not isinstance(duration_s, int)
        or isinstance(duration_s, bool)
        or duration_s < 0
    ):
        raise DynamicIslGenerationError(
            "duration_s must be a non-negative integer"
        )
    if (
        not isinstance(step_s, int)
        or isinstance(step_s, bool)
        or step_s <= 0
    ):
        raise DynamicIslGenerationError("step_s must be a positive integer")
    if duration_s % step_s != 0:
        raise DynamicIslGenerationError(
            "duration_s must be evenly divisible by step_s"
        )
    return tuple(range(0, duration_s + 1, step_s))


def _candidate_payload(
    config: ConstellationConfig,
    candidates: tuple[CandidateIsl, ...],
) -> dict[str, Any]:
    return {
        "schema_version": config.schema_version,
        "strategy": STRATEGY,
        "isl_candidate_strategy": config.isl_candidate_strategy,
        "node_count": config.expected_satellite_count,
        "num_orbits": config.num_orbits,
        "satellites_per_orbit": config.satellites_per_orbit,
        "seam_enabled": config.seam_enabled,
        "candidate_count": len(candidates),
        "candidate_isls": [
            {
                "node1_id": candidate.edge.node1_id,
                "node2_id": candidate.edge.node2_id,
                "kind": candidate.kind,
            }
            for candidate in candidates
        ],
    }


def _snapshot_payload(snapshot: IslSnapshot) -> dict[str, Any]:
    return {
        "time_s": snapshot.time_s,
        "candidate_count": snapshot.candidate_count,
        "active_count": len(snapshot.active_edges),
        "filtered_count": len(snapshot.filtered_edges),
        "active_isls": [
            {
                "node1_id": evaluated.edge.node1_id,
                "node2_id": evaluated.edge.node2_id,
                "distance_m": evaluated.distance_m,
            }
            for evaluated in snapshot.active_edges
        ],
        "filtered_isls": [
            {
                "node1_id": evaluated.edge.node1_id,
                "node2_id": evaluated.edge.node2_id,
                "distance_m": evaluated.distance_m,
                "reason": evaluated.reason,
            }
            for evaluated in snapshot.filtered_edges
        ],
        "added_edges": [
            _edge_payload(edge) for edge in snapshot.added_edges
        ],
        "removed_edges": [
            _edge_payload(edge) for edge in snapshot.removed_edges
        ],
    }


def _edge_payload(edge: IslEdge) -> dict[str, int]:
    return {
        "node1_id": edge.node1_id,
        "node2_id": edge.node2_id,
    }


def _manifest_payload(
    *,
    config: ConstellationConfig,
    adapter: HypatiaAdapter,
    duration_s: int,
    step_s: int,
    snapshots: tuple[IslSnapshot, ...],
    candidate_count: int,
    degree_profile: dict[str, int],
    clearance_floor_m: int,
    candidate_sha256: str,
    snapshot_sha256: str,
    first_snapshot_sha256: str,
    last_snapshot_sha256: str,
    aggregate_sha256: str,
) -> dict[str, Any]:
    active_counts = [len(snapshot.active_edges) for snapshot in snapshots]
    return {
        "schema_version": config.schema_version,
        "strategy": STRATEGY,
        "isl_candidate_strategy": config.isl_candidate_strategy,
        "constellation_name": config.constellation_name,
        "constellation_pattern": config.constellation_pattern,
        "physical_config": config.input_dict(),
        "hypatia_repository": adapter.repository,
        "hypatia_commit": adapter.commit,
        "hypatia_integration_mode": "vendored-minimal",
        "python_version": platform.python_version(),
        "uv_version": uv_version(),
        "uv_lock_sha256": sha256_file(REPOSITORY_ROOT / "uv.lock"),
        "duration_s": duration_s,
        "step_s": step_s,
        "snapshot_count": len(snapshots),
        "first_time_s": snapshots[0].time_s,
        "last_time_s": snapshots[-1].time_s,
        "minimum_isl_ray_altitude_m": MINIMUM_ISL_RAY_ALTITUDE_M,
        "configured_max_isl_distance_m": config.max_isl_distance_m,
        "clearance_limited_max_distance_m": round(
            clearance_limited_max_distance_m(config.altitude_km),
            3,
        ),
        "clearance_limited_max_distance_floor_m": clearance_floor_m,
        "candidate_count": candidate_count,
        "candidate_degree_profile": degree_profile,
        "min_active_count": min(active_counts),
        "max_active_count": max(active_counts),
        "average_active_count": sum(active_counts) / len(active_counts),
        "total_added_transitions": sum(
            len(snapshot.added_edges) for snapshot in snapshots
        ),
        "total_removed_transitions": sum(
            len(snapshot.removed_edges) for snapshot in snapshots
        ),
        "candidate_file_sha256": candidate_sha256,
        "snapshot_jsonl_sha256": snapshot_sha256,
        "first_snapshot_sha256": first_snapshot_sha256,
        "last_snapshot_sha256": last_snapshot_sha256,
        "aggregate_sha256": aggregate_sha256,
    }


def _validate_snapshot_sequence(
    candidates: tuple[CandidateIsl, ...],
    snapshots: tuple[IslSnapshot, ...],
    times_s: tuple[int, ...],
) -> None:
    if len(snapshots) != len(times_s):
        raise DynamicIslGenerationError("snapshot count differs from schedule")
    candidate_edges = {candidate.edge for candidate in candidates}
    previous_active_edges = None
    for expected_time, snapshot in zip(times_s, snapshots):
        if snapshot.time_s != expected_time:
            raise DynamicIslGenerationError("snapshot time differs from schedule")
        evaluated_edges = {
            item.edge
            for item in snapshot.active_edges + snapshot.filtered_edges
        }
        if (
            snapshot.candidate_count != len(candidates)
            or evaluated_edges != candidate_edges
        ):
            raise DynamicIslGenerationError(
                "snapshot candidates are internally inconsistent"
            )
        current_active_edges = {
            item.edge for item in snapshot.active_edges
        }
        if previous_active_edges is None:
            expected_added = set()
            expected_removed = set()
        else:
            expected_added = current_active_edges - previous_active_edges
            expected_removed = previous_active_edges - current_active_edges
        if (
            set(snapshot.added_edges) != expected_added
            or set(snapshot.removed_edges) != expected_removed
        ):
            raise DynamicIslGenerationError(
                "snapshot transitions are internally inconsistent"
            )
        previous_active_edges = current_active_edges


def _validate_written_output(
    output_dir: Path,
    *,
    expected_snapshot_count: int,
) -> None:
    names = tuple(sorted(path.name for path in output_dir.iterdir()))
    if names != EXPECTED_OUTPUT_FILES:
        raise DynamicIslGenerationError(
            f"unexpected dynamic ISL output files: {names}"
        )
    json.loads(
        (output_dir / CANDIDATE_FILENAME).read_text(encoding="utf-8")
    )
    json.loads((output_dir / MANIFEST_FILENAME).read_text(encoding="utf-8"))
    snapshot_lines = (
        output_dir / SNAPSHOT_FILENAME
    ).read_text(encoding="utf-8").splitlines()
    if len(snapshot_lines) != expected_snapshot_count:
        raise DynamicIslGenerationError(
            "written snapshot count differs from manifest"
        )
    for line in snapshot_lines:
        json.loads(line)


def _temporary_output_dir(output_dir: Path) -> Path:
    return output_dir.with_name(f"{output_dir.name}.tmp")


def _require_output_target_is_safe(output_dir: Path) -> None:
    if output_dir.is_symlink() or (output_dir.exists() and not output_dir.is_dir()):
        raise DynamicIslGenerationError(
            f"output path must be a directory: {output_dir}"
        )
    if output_dir.exists() and any(output_dir.iterdir()):
        raise DynamicIslGenerationError(
            f"refusing to overwrite non-empty output directory: {output_dir}"
        )
    output_dir.parent.mkdir(parents=True, exist_ok=True)


def _remove_temporary_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def _compact_json(payload: dict[str, Any]) -> str:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def _json_bytes(payload: dict[str, Any]) -> bytes:
    return (_compact_json(payload) + "\n").encode("utf-8")


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--duration-s", type=int, required=True)
    parser.add_argument("--step-s", type=int, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        manifest = generate_dynamic_isl_output(
            arguments.config.resolve(),
            arguments.duration_s,
            arguments.step_s,
            arguments.output_dir.resolve(),
        )
    except (OSError, ValueError, DynamicIslGenerationError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(_compact_json(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
