#!/usr/bin/env python3
"""Deterministic plus-grid candidate and dynamic ISL contracts."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

from ..common.configuration import ConstellationConfig, PLUS_GRID
from ..orbit.hypatia.mean_motion import WGS72_EARTH_RADIUS_KM
from ..orbit.hypatia.orbit_positions import (
    OrbitConstellation,
    SatellitePosition,
)


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


@dataclass(frozen=True)
class CandidateDegree:
    """Derived intra-plane, inter-plane, and total degree for one node."""

    intra_plane: int
    inter_plane: int

    @property
    def total(self) -> int:
        return self.intra_plane + self.inter_plane


@dataclass(frozen=True, order=True)
class EvaluatedIsl:
    """One candidate edge evaluated at a specific time."""

    edge: IslEdge
    distance_m: float = field(compare=False)
    active: bool = field(compare=False)
    reason: str = field(compare=False)

    def __post_init__(self) -> None:
        if not math.isfinite(self.distance_m) or self.distance_m < 0.0:
            raise DynamicIslError("ISL distance must be finite and non-negative")
        if not isinstance(self.active, bool):
            raise DynamicIslError("ISL active state must be boolean")
        expected_reason = ACTIVE if self.active else OVER_MAX_DISTANCE
        if self.reason != expected_reason:
            raise DynamicIslError(
                f"ISL reason must be {expected_reason} for active={self.active}"
            )


@dataclass(frozen=True)
class IslSnapshot:
    """Stable active/filtered ISLs and transitions for one time point."""

    time_s: int
    active_edges: tuple[EvaluatedIsl, ...]
    filtered_edges: tuple[EvaluatedIsl, ...]
    added_edges: tuple[IslEdge, ...]
    removed_edges: tuple[IslEdge, ...]

    @property
    def candidate_count(self) -> int:
        return len(self.active_edges) + len(self.filtered_edges)


def build_candidate_isls(
    config: ConstellationConfig,
) -> tuple[CandidateIsl, ...]:
    """Build the fixed canonical plus-grid candidate graph."""
    if config.isl_candidate_strategy != PLUS_GRID:
        raise DynamicIslError(
            "dynamic ISL candidates require isl_candidate_strategy=plus-grid"
        )
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


def candidate_degrees(
    candidates: tuple[CandidateIsl, ...],
    node_count: int,
) -> tuple[CandidateDegree, ...]:
    """Derive per-node degrees from the canonical candidate graph."""
    if (
        not isinstance(node_count, int)
        or isinstance(node_count, bool)
        or node_count <= 0
    ):
        raise DynamicIslError("node_count must be a positive integer")
    intra = [0] * node_count
    inter = [0] * node_count
    seen = set()
    for candidate in candidates:
        edge = candidate.edge
        if edge.node_count != node_count:
            raise DynamicIslError(
                "candidate node count differs from requested node count"
            )
        if edge in seen:
            raise DynamicIslError("candidate edges must be unique")
        seen.add(edge)
        target = intra if candidate.kind == INTRA_PLANE else inter
        target[edge.node1_id] += 1
        target[edge.node2_id] += 1
    return tuple(
        CandidateDegree(intra[node_id], inter[node_id])
        for node_id in range(node_count)
    )


def candidate_degree_profile(
    config: ConstellationConfig,
    candidates: tuple[CandidateIsl, ...],
) -> dict[str, int]:
    """Summarize actual candidate degrees for boundary and internal planes."""
    degrees = candidate_degrees(candidates, config.expected_satellite_count)
    boundary_orbits = {0, config.num_orbits - 1}
    boundary_node_ids = tuple(
        node_id
        for node_id in range(config.expected_satellite_count)
        if node_id // config.satellites_per_orbit in boundary_orbits
    )
    internal_node_ids = tuple(
        node_id
        for node_id in range(config.expected_satellite_count)
        if node_id // config.satellites_per_orbit not in boundary_orbits
    )

    def uniform_total(node_ids: tuple[int, ...], label: str) -> int:
        values = {degrees[node_id].total for node_id in node_ids}
        if len(values) != 1:
            raise DynamicIslError(
                f"{label} planes do not have a uniform candidate degree"
            )
        return next(iter(values))

    boundary_degree = uniform_total(boundary_node_ids, "boundary")
    internal_degree = (
        uniform_total(internal_node_ids, "internal")
        if internal_node_ids
        else boundary_degree
    )
    totals = tuple(degree.total for degree in degrees)
    return {
        "minimum_total_degree": min(totals),
        "maximum_total_degree": max(totals),
        "boundary_plane_total_degree": boundary_degree,
        "internal_plane_total_degree": internal_degree,
    }


def evaluate_candidate_isls(
    candidates: tuple[CandidateIsl, ...],
    positions: tuple[SatellitePosition, ...],
    max_distance_m: Any,
) -> tuple[EvaluatedIsl, ...]:
    """Evaluate every candidate using straight Cartesian distance."""
    maximum = _finite_non_negative(max_distance_m, "max_distance_m")
    if maximum == 0.0:
        raise DynamicIslError("max_distance_m must be positive")
    for node_id, position in enumerate(positions):
        if position.node_id != node_id:
            raise DynamicIslError("positions must be ordered by node_id")

    evaluated = []
    seen_edges = set()
    for candidate in candidates:
        edge = candidate.edge
        if edge in seen_edges:
            raise DynamicIslError("candidate edges must be unique")
        seen_edges.add(edge)
        if edge.node_count != len(positions):
            raise DynamicIslError(
                "candidate node count differs from position node count"
            )
        distance = math.dist(
            positions[edge.node1_id].xyz_m,
            positions[edge.node2_id].xyz_m,
        )
        active = distance <= maximum
        evaluated.append(
            EvaluatedIsl(
                edge=edge,
                distance_m=round(distance, 3),
                active=active,
                reason=ACTIVE if active else OVER_MAX_DISTANCE,
            )
        )
    return tuple(sorted(evaluated))


def build_isl_snapshot(
    time_s: int,
    evaluated_isls: tuple[EvaluatedIsl, ...],
    previous_active_edges: tuple[IslEdge, ...] | None,
) -> IslSnapshot:
    """Build one snapshot and exact set-difference transitions."""
    if (
        not isinstance(time_s, int)
        or isinstance(time_s, bool)
        or time_s < 0
    ):
        raise DynamicIslError("snapshot time_s must be a non-negative integer")
    if time_s == 0 and previous_active_edges is not None:
        raise DynamicIslError("t=0 must not have previous active edges")
    if time_s > 0 and previous_active_edges is None:
        raise DynamicIslError("t>0 requires previous active edges")

    ordered = tuple(sorted(evaluated_isls))
    if len({item.edge for item in ordered}) != len(ordered):
        raise DynamicIslError("evaluated ISLs must have unique edges")
    active = tuple(item for item in ordered if item.active)
    filtered = tuple(item for item in ordered if not item.active)
    current_edges = {item.edge for item in active}
    if previous_active_edges is None:
        added = ()
        removed = ()
    else:
        previous_edges = set(previous_active_edges)
        added = tuple(sorted(current_edges - previous_edges))
        removed = tuple(sorted(previous_edges - current_edges))
    return IslSnapshot(
        time_s=time_s,
        active_edges=active,
        filtered_edges=filtered,
        added_edges=added,
        removed_edges=removed,
    )


def generate_isl_snapshots(
    orbit: OrbitConstellation,
    candidates: tuple[CandidateIsl, ...],
    times_s: tuple[int, ...],
    max_distance_m: Any,
) -> tuple[IslSnapshot, ...]:
    """Evaluate a strictly increasing sequence beginning at t=0."""
    if (
        not times_s
        or times_s[0] != 0
        or any(
            not isinstance(time_s, int) or isinstance(time_s, bool)
            for time_s in times_s
        )
        or any(first >= second for first, second in zip(times_s, times_s[1:]))
    ):
        raise DynamicIslError(
            "snapshot times must be strictly increasing integers from 0"
        )
    snapshots = []
    previous_active_edges = None
    for time_s in times_s:
        evaluated = evaluate_candidate_isls(
            candidates,
            orbit.positions_at(time_s),
            max_distance_m,
        )
        snapshot = build_isl_snapshot(
            time_s,
            evaluated,
            previous_active_edges,
        )
        snapshots.append(snapshot)
        previous_active_edges = tuple(
            item.edge for item in snapshot.active_edges
        )
    return tuple(snapshots)


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
