#!/usr/bin/env python3
"""Deterministic plus-grid candidate and dynamic ISL contracts."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

from configuration import ConstellationConfig
from mean_motion import WGS72_EARTH_RADIUS_KM


STRATEGY = "plus-grid-range-gated"
INTRA_PLANE = "intra-plane"
INTER_PLANE = "inter-plane"
ACTIVE = "ACTIVE"
OVER_MAX_DISTANCE = "OVER_MAX_DISTANCE"
MINIMUM_ISL_RAY_ALTITUDE_M = 80000
WGS72_EARTH_RADIUS_M = WGS72_EARTH_RADIUS_KM * 1000.0


class DynamicIslError(ValueError):
    """Raised when a dynamic ISL contract is invalid."""


@dataclass(frozen=True, order=True)
class IslEdge:
    """Canonical undirected edge with validated constellation endpoints."""

    node1_id: int
    node2_id: int
    node_count: int = field(compare=False, repr=False)

    def __post_init__(self) -> None:
        for value, name in (
            (self.node1_id, "node1_id"),
            (self.node2_id, "node2_id"),
            (self.node_count, "node_count"),
        ):
            if not isinstance(value, int) or isinstance(value, bool):
                raise DynamicIslError(f"{name} must be an integer")
        if self.node_count <= 0:
            raise DynamicIslError("node_count must be positive")
        if self.node1_id < 0 or self.node2_id < 0:
            raise DynamicIslError("ISL node IDs must be non-negative")
        if self.node1_id >= self.node_count or self.node2_id >= self.node_count:
            raise DynamicIslError("ISL endpoint exceeds constellation node count")
        if self.node1_id >= self.node2_id:
            raise DynamicIslError(
                "ISL edge must be canonical with node1_id < node2_id"
            )

    @classmethod
    def canonical(
        cls,
        node_a: int,
        node_b: int,
        node_count: int,
    ) -> "IslEdge":
        """Construct one sorted undirected edge."""
        return cls(min(node_a, node_b), max(node_a, node_b), node_count)


@dataclass(frozen=True, order=True)
class CandidateIsl:
    """One fixed plus-grid candidate edge and its construction kind."""

    edge: IslEdge
    kind: str = field(compare=False)

    def __post_init__(self) -> None:
        if self.kind not in (INTRA_PLANE, INTER_PLANE):
            raise DynamicIslError(f"unknown candidate ISL kind: {self.kind}")


def build_candidate_isls(
    config: ConstellationConfig,
) -> tuple[CandidateIsl, ...]:
    """Build the fixed canonical plus-grid candidate graph."""
    node_count = config.expected_satellite_count
    candidates: dict[tuple[int, int], CandidateIsl] = {}

    def add_candidate(node_a: int, node_b: int, kind: str) -> None:
        if node_a == node_b:
            return
        edge = IslEdge.canonical(node_a, node_b, node_count)
        key = edge.node1_id, edge.node2_id
        existing = candidates.get(key)
        if existing is not None:
            if existing.kind != kind:
                raise DynamicIslError(
                    f"candidate edge {key} has conflicting kinds"
                )
            return
        candidates[key] = CandidateIsl(edge, kind)

    satellites_per_orbit = config.satellites_per_orbit
    for orbit in range(config.num_orbits):
        orbit_start = orbit * satellites_per_orbit
        for slot in range(satellites_per_orbit):
            add_candidate(
                orbit_start + slot,
                orbit_start + (slot + 1) % satellites_per_orbit,
                INTRA_PLANE,
            )

    for orbit in range(config.num_orbits - 1):
        first_start = orbit * satellites_per_orbit
        second_start = (orbit + 1) * satellites_per_orbit
        for slot in range(satellites_per_orbit):
            add_candidate(
                first_start + slot,
                second_start + slot,
                INTER_PLANE,
            )

    if config.seam_enabled and config.num_orbits > 1:
        last_start = (config.num_orbits - 1) * satellites_per_orbit
        for slot in range(satellites_per_orbit):
            add_candidate(
                last_start + slot,
                slot,
                INTER_PLANE,
            )

    return tuple(candidates[key] for key in sorted(candidates))


def clearance_limited_max_distance_m(
    altitude_km: Any,
    minimum_ray_altitude_m: Any = MINIMUM_ISL_RAY_ALTITUDE_M,
) -> float:
    """Return the longest chord that remains above a ray-altitude limit."""
    altitude = _finite_non_negative(altitude_km, "altitude_km") * 1000.0
    minimum_ray_altitude = _finite_non_negative(
        minimum_ray_altitude_m,
        "minimum_ray_altitude_m",
    )
    orbital_radius_m = WGS72_EARTH_RADIUS_M + altitude
    clearance_radius_m = WGS72_EARTH_RADIUS_M + minimum_ray_altitude
    if orbital_radius_m <= clearance_radius_m:
        return 0.0
    return 2.0 * math.sqrt(
        orbital_radius_m**2 - clearance_radius_m**2
    )


def validate_clearance_limit(config: ConstellationConfig) -> int:
    """Validate the configured range against the conservative clearance chord."""
    clearance_limit_m = math.floor(
        clearance_limited_max_distance_m(config.altitude_km)
    )
    if config.max_isl_distance_m > clearance_limit_m:
        raise DynamicIslError(
            f"max_isl_distance_m={config.max_isl_distance_m} exceeds "
            f"the {MINIMUM_ISL_RAY_ALTITUDE_M} m clearance limit "
            f"{clearance_limit_m} m"
        )
    return clearance_limit_m


def _finite_non_negative(value: Any, name: str) -> float:
    if (
        not isinstance(value, (int, float))
        or isinstance(value, bool)
        or not math.isfinite(value)
        or value < 0.0
    ):
        raise DynamicIslError(
            f"{name} must be a finite non-negative number"
        )
    return float(value)
