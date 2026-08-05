#!/usr/bin/env python3
"""Deterministic compute-node placement across orbital planes and slots."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


class ComputePlacementError(ValueError):
    """Raised when compute placement inputs or results are invalid."""


@dataclass(frozen=True)
class ComputePlacement:
    """One deterministic placement result."""

    compute_nodes_per_orbit: tuple[int, ...]
    selected_node_ids: tuple[int, ...]


def _require_positive_integer(value: Any, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise ComputePlacementError(f"{name} must be a positive integer")
    return value


def even_plane_slot_placement(
    num_orbits: int,
    satellites_per_orbit: int,
    compute_node_count: int,
) -> ComputePlacement:
    """Place K nodes using balanced plane counts and layered slot midpoints."""
    planes = _require_positive_integer(num_orbits, "num_orbits")
    slots_per_plane = _require_positive_integer(
        satellites_per_orbit,
        "satellites_per_orbit",
    )
    count = _require_positive_integer(
        compute_node_count,
        "compute_node_count",
    )
    satellite_count = planes * slots_per_plane
    if count > satellite_count:
        raise ComputePlacementError(
            "compute_node_count must not exceed the satellite count"
        )

    # Differences between cumulative floors distribute K nodes across P
    # planes deterministically, with every plane count differing by at most 1.
    counts = tuple(
        ((orbit + 1) * count) // planes
        - (orbit * count) // planes
        for orbit in range(planes)
    )
    selected = []
    for orbit, orbit_count in enumerate(counts):
        for index in range(orbit_count):
            # Integer-only form of floor((index + 0.5) * S / k) avoids
            # platform-dependent floating-point placement.
            slot = (
                (2 * index + 1) * slots_per_plane
            ) // (2 * orbit_count)
            selected.append(orbit * slots_per_plane + slot)

    selected_ids = tuple(selected)
    if sum(counts) != count or len(selected_ids) != count:
        raise ComputePlacementError("compute placement count is inconsistent")
    if max(counts) - min(counts) > 1:
        raise ComputePlacementError("compute placement is not plane-balanced")
    if (
        selected_ids != tuple(sorted(selected_ids))
        or len(set(selected_ids)) != count
        or any(node_id < 0 or node_id >= satellite_count
               for node_id in selected_ids)
    ):
        raise ComputePlacementError("compute placement node IDs are invalid")
    return ComputePlacement(counts, selected_ids)
