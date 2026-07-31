#!/usr/bin/env python3
"""Generate one canonical SatCompute topology from an orbital sample time."""

from __future__ import annotations

import argparse
import platform
import sys
from pathlib import Path
from typing import Any

from ..check_export import check_export
from ..common.atomic_output import (
    AtomicOutputError,
    atomic_output_directory,
)
from ..common.configuration import ConstellationConfig, load_config
from ..common.hash_utils import (
    REPOSITORY_ROOT,
    aggregate_data_sha256,
    compact_json,
    compact_json_bytes,
    git_repository_state,
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
    satcompute_json_bytes,
    topology_payload,
    validate_delay_parameters,
    validate_link_bandwidth_kbps,
)
from ..dynamic.dynamic_isls import (
    CandidateIsl,
    EvaluatedIsl,
    build_candidate_isls,
    candidate_degree_profile,
    evaluate_candidate_isls,
    validate_clearance_limit,
)
from ..orbit.hypatia.adapter import HypatiaAdapter
from ..orbit.hypatia.orbit_positions import load_orbit_constellation


TOPOLOGY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = TOPOLOGY_ROOT / "config" / "synthetic-66.json"
NODES_FILENAME = "nodes_0s.json"
TOPOLOGY_FILENAME = "topology_0s.json"
MANIFEST_FILENAME = "manifest.json"


class StaticTopologyGenerationError(ValueError):
    """Raised when a static topology request is invalid."""


def _require_integer(
    value: Any,
    name: str,
    *,
    minimum: int,
    maximum: int,
) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or not minimum <= value <= maximum
    ):
        raise StaticTopologyGenerationError(
            f"{name} must be an integer in [{minimum}, {maximum}]"
        )
    return value


def _build_links(
    evaluated: tuple[EvaluatedIsl, ...],
    *,
    delay_mode: str,
    fixed_delay_us: int | None,
    link_bandwidth_kbps: int,
) -> tuple[SatComputeLink, ...]:
    return tuple(
        SatComputeLink(
            node1_id=item.edge.node1_id,
            node2_id=item.edge.node2_id,
            delay_us=(
                fixed_delay_us
                if delay_mode == "fixed"
                else distance_delay_us(item.distance_m)
            ),
            link_bandwidth_kbps=link_bandwidth_kbps,
        )
        for item in evaluated
        if item.active
    )


def _build_manifest(
    *,
    config: ConstellationConfig,
    adapter: HypatiaAdapter,
    candidates: tuple[CandidateIsl, ...],
    active_count: int,
    time_s: int,
    delay_mode: str,
    fixed_delay_us: int | None,
    link_bandwidth_kbps: int,
    aggregate_sha256: str,
) -> dict[str, Any]:
    satcompute_commit, worktree_clean = git_repository_state()
    return {
        "schema_version": config.schema_version,
        "generator": "satcompute-topology-export",
        "generation_mode": "static",
        "satcompute_commit": satcompute_commit,
        "satcompute_worktree_clean": worktree_clean,
        "hypatia_repository": adapter.repository,
        "hypatia_commit": adapter.commit,
        "hypatia_integration_mode": "vendored-minimal",
        "python_version": platform.python_version(),
        "uv_version": uv_version(),
        "uv_lock_sha256": sha256_file(REPOSITORY_ROOT / "uv.lock"),
        "physical_config": config.input_dict(),
        "isl_candidate_strategy": config.isl_candidate_strategy,
        "seam_enabled": config.seam_enabled,
        "candidate_count": len(candidates),
        "candidate_degree_profile": candidate_degree_profile(
            config,
            candidates,
        ),
        "source_dynamic_manifest_sha256": None,
        "source_dynamic_aggregate_sha256": None,
        "duration_s": time_s,
        "step_s": None,
        "orbit_sample_offset_s": None,
        "snapshot_count": 1,
        "first_time_s": 0,
        "last_time_s": 0,
        "node_count": config.expected_satellite_count,
        "min_active_count": active_count,
        "max_active_count": active_count,
        "average_active_count": float(active_count),
        "delay_mode": delay_mode,
        "fixed_delay_us": fixed_delay_us,
        "speed_of_light_m_per_s": SPEED_OF_LIGHT_M_PER_S,
        "delay_rounding": DELAY_ROUNDING,
        "link_bandwidth_kbps": link_bandwidth_kbps,
        "aggregate_data_sha256": aggregate_sha256,
    }


def generate_static_topology(
    config_path: Path,
    time_s: int,
    delay_mode: str,
    fixed_delay_us: int | None,
    link_bandwidth_kbps: int,
    output_dir: Path,
) -> dict[str, Any]:
    """Generate, check, and atomically publish one initial snapshot pair."""
    sample_time = _require_integer(
        time_s,
        "time_s",
        minimum=0,
        maximum=(1 << 63) - 1,
    )
    try:
        bandwidth = validate_link_bandwidth_kbps(link_bandwidth_kbps)
        validate_delay_parameters(delay_mode, fixed_delay_us)
    except SatComputeSchemaError as error:
        raise StaticTopologyGenerationError(str(error)) from error
    config = load_config(config_path)
    validate_clearance_limit(config)
    candidates = build_candidate_isls(config)

    try:
        with atomic_output_directory(output_dir) as temporary:
            adapter = HypatiaAdapter()
            orbit = load_orbit_constellation(config, adapter)
            evaluated = evaluate_candidate_isls(
                candidates,
                orbit.positions_at(sample_time),
                config.max_isl_distance_m,
            )
            links = _build_links(
                evaluated,
                delay_mode=delay_mode,
                fixed_delay_us=fixed_delay_us,
                link_bandwidth_kbps=bandwidth,
            )
            (temporary / NODES_FILENAME).write_bytes(
                satcompute_json_bytes(
                    nodes_payload(config.expected_satellite_count)
                )
            )
            (temporary / TOPOLOGY_FILENAME).write_bytes(
                satcompute_json_bytes(
                    topology_payload(
                        links,
                        config.expected_satellite_count,
                    )
                )
            )
            aggregate_sha256 = aggregate_data_sha256(
                temporary,
                (NODES_FILENAME, TOPOLOGY_FILENAME),
            )
            manifest = _build_manifest(
                config=config,
                adapter=adapter,
                candidates=candidates,
                active_count=len(links),
                time_s=sample_time,
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
                expected_node_count=config.expected_satellite_count,
            )
        return manifest
    except AtomicOutputError as error:
        raise StaticTopologyGenerationError(str(error)) from error


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--time-s", type=int, required=True)
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
        manifest = generate_static_topology(
            arguments.config.resolve(),
            arguments.time_s,
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
