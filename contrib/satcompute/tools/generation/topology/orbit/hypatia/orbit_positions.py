#!/usr/bin/env python3
"""Reusable deterministic satellite-position propagation for SatCompute."""

from __future__ import annotations

import hashlib
import json
import math
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from ...common.configuration import ConstellationConfig
from .adapter import HypatiaAdapter
from .walker_tles import generate_walker_tles


@dataclass(frozen=True)
class SatellitePosition:
    """One satellite's Cartesian WGS72 position in metres."""

    node_id: int
    x_m: float
    y_m: float
    z_m: float

    @property
    def xyz_m(self) -> tuple[float, float, float]:
        return self.x_m, self.y_m, self.z_m

    def rounded_row(self) -> list[float | int]:
        return [
            self.node_id,
            round(self.x_m, 3),
            round(self.y_m, 3),
            round(self.z_m, 3),
        ]


@dataclass(frozen=True)
class OrbitConstellation:
    """Loaded TLE epoch and satellites with stable node ordering."""

    adapter: HypatiaAdapter
    epoch: Any
    satellites: tuple[Any, ...]

    @property
    def node_count(self) -> int:
        return len(self.satellites)

    def positions_at(self, time_s: float) -> tuple[SatellitePosition, ...]:
        time_value = _require_time(time_s)
        positions = []
        for node_id, satellite in enumerate(self.satellites):
            x_m, y_m, z_m = self.adapter.satellite_position_at(
                satellite,
                self.epoch,
                time_value,
            )
            if not all(math.isfinite(value) for value in (x_m, y_m, z_m)):
                raise ValueError(
                    f"satellite {node_id} has a non-finite position"
                )
            positions.append(
                SatellitePosition(node_id, x_m, y_m, z_m)
            )
        return tuple(positions)


def _require_time(time_s: float) -> float:
    if (
        not isinstance(time_s, (int, float))
        or isinstance(time_s, bool)
        or not math.isfinite(time_s)
        or time_s < 0.0
    ):
        raise ValueError("time_s must be a finite non-negative number")
    return float(time_s)


def load_tle_orbit_constellation(
    tle_path: Path,
    adapter: HypatiaAdapter,
    expected_node_count: int,
) -> OrbitConstellation:
    """Read one TLE file and enforce its expected satellite count."""
    constellation = adapter.read_tles(tle_path)
    satellites = tuple(constellation["satellites"])
    if len(satellites) != expected_node_count:
        raise ValueError(
            f"expected {expected_node_count} satellites, "
            f"found {len(satellites)}"
        )
    return OrbitConstellation(
        adapter=adapter,
        epoch=constellation["epoch"],
        satellites=satellites,
    )


def load_orbit_constellation(
    config: ConstellationConfig,
    adapter: HypatiaAdapter | None = None,
) -> OrbitConstellation:
    """Generate and load one constellation without retaining a TLE file."""
    selected_adapter = adapter or HypatiaAdapter()
    with tempfile.TemporaryDirectory(
        prefix="satcompute-orbit-constellation-"
    ) as temp:
        tle_path = Path(temp) / "tles.txt"
        generate_walker_tles(tle_path, config, selected_adapter)
        return load_tle_orbit_constellation(
            tle_path,
            selected_adapter,
            config.expected_satellite_count,
        )


def position_samples_payload(
    orbit: OrbitConstellation,
    sample_times_s: Iterable[float],
) -> dict[str, list[list[float | int]]]:
    """Return stable rounded position rows keyed by sample time."""
    payload: dict[str, list[list[float | int]]] = {}
    for time_s in sample_times_s:
        time_value = _require_time(time_s)
        key = _time_key(time_value)
        if key in payload:
            raise ValueError(f"duplicate position sample time: {time_value}")
        payload[key] = [
            position.rounded_row()
            for position in orbit.positions_at(time_value)
        ]
    if not payload:
        raise ValueError("at least one position sample time is required")
    return payload


def position_samples_sha256(
    orbit: OrbitConstellation,
    sample_times_s: Iterable[float],
    *,
    minimum_movement_m: float | None = None,
) -> str:
    """Hash stable rounded samples and optionally require satellite movement."""
    payload = position_samples_payload(orbit, sample_times_s)
    if minimum_movement_m is not None:
        _require_sample_movement(payload, minimum_movement_m)
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _time_key(time_s: float) -> str:
    return str(int(time_s)) if time_s.is_integer() else format(time_s, ".15g")


def _require_sample_movement(
    payload: dict[str, list[list[float | int]]],
    minimum_movement_m: float,
) -> None:
    if (
        not isinstance(minimum_movement_m, (int, float))
        or isinstance(minimum_movement_m, bool)
        or not math.isfinite(minimum_movement_m)
        or minimum_movement_m < 0.0
    ):
        raise ValueError(
            "minimum_movement_m must be a finite non-negative number"
        )
    snapshots = list(payload.values())
    if len(snapshots) < 2:
        raise ValueError("movement validation requires at least two samples")
    first = snapshots[0]
    last = snapshots[-1]
    if len(first) != len(last):
        raise ValueError("position samples have inconsistent node counts")
    for node_id, (start, end) in enumerate(zip(first, last)):
        if start[0] != node_id or end[0] != node_id:
            raise ValueError("position samples are not ordered by node_id")
        if math.dist(start[1:], end[1:]) <= minimum_movement_m:
            raise ValueError(
                f"satellite {node_id} did not move between position samples"
            )
