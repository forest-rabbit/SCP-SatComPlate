#!/usr/bin/env python3
"""Read v0.3 topology traces for display without calculating satellite orbits."""

from __future__ import annotations

import bisect
import json
import math
from dataclasses import dataclass
from pathlib import Path

from ...generation.topology.common.manifest import read_json, validate_trace


class TraceVisualizationError(ValueError):
    """Raised when a topology trace cannot supply consistent display data."""


# Preserve the legacy exception import while callers adopt trace terminology.
ScenarioVisualizationError = TraceVisualizationError


@dataclass(frozen=True, order=True)
class PositionKm:
    """One manifest-listed Earth-fixed satellite position in kilometres."""

    node_id: int
    x_km: float
    y_km: float
    z_km: float

    @property
    def xyz_km(self) -> tuple[float, float, float]:
        return self.x_km, self.y_km, self.z_km


@dataclass(frozen=True)
class TraceSnapshot:
    """One paired position and active-ISL state from the trace manifest."""

    time_ns: int
    positions: tuple[PositionKm, ...]
    links: tuple[tuple[int, int], ...]


@dataclass(frozen=True)
class OrbitTrace:
    """Validated trace state consumed with last-snapshot-held semantics."""

    root: Path
    run_name: str
    node_count: int
    num_orbits: int
    satellites_per_orbit: int
    duration_ns: int
    compute_node_ids: tuple[int, ...]
    relay_node_ids: tuple[int, ...]
    snapshots: tuple[TraceSnapshot, ...]

    @property
    def duration_s(self) -> float:
        return self.duration_ns / 1_000_000_000

    @property
    def topology_mode(self) -> str:
        return "dynamic" if len(self.snapshots) > 1 else "static"

    def _validate_simulation_time(self, value: float) -> int:
        if (
            not isinstance(value, (int, float))
            or isinstance(value, bool)
            or not math.isfinite(value)
            or value < 0.0
            or value > self.duration_s
        ):
            raise TraceVisualizationError(
                f"simulation time must be in [0, {self.duration_s:g}]"
            )
        return round(float(value) * 1_000_000_000)

    def _snapshot_at(self, simulation_time_s: float) -> TraceSnapshot:
        time_ns = self._validate_simulation_time(simulation_time_s)
        index = bisect.bisect_right(
            tuple(snapshot.time_ns for snapshot in self.snapshots),
            time_ns,
        ) - 1
        if index < 0:
            raise TraceVisualizationError(
                "no topology snapshot exists at or before display time"
            )
        return self.snapshots[index]

    def positions_at(self, simulation_time_s: float) -> tuple[PositionKm, ...]:
        """Return only an exported position state; never propagate an orbit."""
        return self._snapshot_at(simulation_time_s).positions

    def links_at(self, simulation_time_s: float) -> tuple[tuple[int, int], ...]:
        """Return exported active ISLs using last-snapshot-held semantics."""
        return self._snapshot_at(simulation_time_s).links

    def source_snapshot_time_s(self, simulation_time_s: float) -> float:
        """Return the exported slice time currently displayed."""
        return self._snapshot_at(simulation_time_s).time_ns / 1_000_000_000

    def orbit_positions(
        self,
        positions: tuple[PositionKm, ...],
        orbit_index: int,
    ) -> tuple[tuple[float, float, float], ...]:
        """Select one Walker plane using canonical orbit-major satellite IDs."""
        if not 0 <= orbit_index < self.num_orbits:
            raise TraceVisualizationError("orbit_index is out of range")
        first = orbit_index * self.satellites_per_orbit
        last = first + self.satellites_per_orbit
        return tuple(position.xyz_km for position in positions[first:last])

    def frame_times(self, step_s: float) -> tuple[float, ...]:
        """Return inclusive display times; this never creates physical states."""
        if (
            not isinstance(step_s, (int, float))
            or isinstance(step_s, bool)
            or not math.isfinite(step_s)
            or step_s <= 0.0
        ):
            raise TraceVisualizationError(
                "frame step must be a finite positive number"
            )
        if len(self.snapshots) == 1:
            return (0.0,)
        duration = self.duration_s
        result = []
        time_s = 0.0
        while time_s < duration:
            result.append(time_s)
            time_s += float(step_s)
        if not result or result[-1] != duration:
            result.append(duration)
        return tuple(result)


# Renderer compatibility name; its data source is now always a topology trace.
OrbitScenario = OrbitTrace


def _require_number(value, field):
    if (
        not isinstance(value, (int, float))
        or isinstance(value, bool)
        or not math.isfinite(value)
    ):
        raise TraceVisualizationError(f"{field} must be a finite number")
    return float(value)


def _load_positions(path, expected_node_count):
    payload = read_json(path)
    nodes = payload.get("nodes") if isinstance(payload, dict) else None
    if not isinstance(nodes, list) or len(nodes) != expected_node_count:
        raise TraceVisualizationError(f"{path.name} node count differs")
    positions = []
    for expected_id, node in enumerate(nodes):
        node_id = node.get("node_id") if isinstance(node, dict) else None
        if (
            not isinstance(node_id, int)
            or isinstance(node_id, bool)
            or node_id != expected_id
        ):
            raise TraceVisualizationError(
                f"{path.name} satellite IDs must be dense and canonical"
            )
        positions.append(
            PositionKm(
                expected_id,
                _require_number(node.get("x_m"), "x_m") / 1000.0,
                _require_number(node.get("y_m"), "y_m") / 1000.0,
                _require_number(node.get("z_m"), "z_m") / 1000.0,
            )
        )
    return tuple(positions)


def _load_links(path, expected_node_count):
    payload = read_json(path)
    links = payload.get("links") if isinstance(payload, dict) else None
    if not isinstance(links, list):
        raise TraceVisualizationError(f"{path.name} links must be an array")
    result = []
    seen = set()
    for link in links:
        if not isinstance(link, dict):
            raise TraceVisualizationError(f"{path.name} link must be an object")
        endpoint1 = link.get("node1_id")
        endpoint2 = link.get("node2_id")
        if (
            not isinstance(endpoint1, int)
            or isinstance(endpoint1, bool)
            or not isinstance(endpoint2, int)
            or isinstance(endpoint2, bool)
            or not 0 <= endpoint1 < endpoint2 < expected_node_count
        ):
            raise TraceVisualizationError(f"{path.name} link endpoint is invalid")
        edge = (endpoint1, endpoint2)
        if edge in seen:
            raise TraceVisualizationError(f"{path.name} contains a duplicate link")
        seen.add(edge)
        result.append(edge)
    return tuple(sorted(result))


def _load_compute_node_ids(path, expected_node_count):
    if path is None:
        return ()
    source = Path(path).resolve()
    try:
        payload = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise TraceVisualizationError(
            f"cannot read compute profile {source}: {error}"
        ) from error
    if not isinstance(payload, dict) or set(payload) != {
        "schema_version",
        "compute_nodes",
    }:
        raise TraceVisualizationError("compute profile root fields differ")
    if payload["schema_version"] != "0.1" or not isinstance(
        payload["compute_nodes"], list
    ):
        raise TraceVisualizationError("compute profile schema differs")
    node_ids = []
    for entry in payload["compute_nodes"]:
        if not isinstance(entry, dict) or set(entry) != {
            "node_id",
            "compute_rate_work_units_per_second",
        }:
            raise TraceVisualizationError("compute node fields differ")
        node_id = entry["node_id"]
        rate = entry["compute_rate_work_units_per_second"]
        if (
            not isinstance(node_id, int)
            or isinstance(node_id, bool)
            or not 0 <= node_id < expected_node_count
            or not isinstance(rate, int)
            or isinstance(rate, bool)
            or rate <= 0
        ):
            raise TraceVisualizationError("compute node value is invalid")
        node_ids.append(node_id)
    if not node_ids or len(node_ids) != len(set(node_ids)):
        raise TraceVisualizationError("compute node IDs must be non-empty and unique")
    return tuple(sorted(node_ids))


def load_trace(root: Path, *, compute_profile: Path | None = None) -> OrbitTrace:
    """Load only manifest-listed XYZ/link slices and an optional compute profile."""
    trace_root = Path(root).resolve()
    validation = validate_trace(trace_root)
    manifest = read_json(trace_root / "manifest.json")
    constellation = manifest["constellation"]
    node_count = validation["satellite_count"]
    try:
        num_orbits = constellation["num_orbits"]
        satellites_per_orbit = constellation["satellites_per_orbit"]
    except (KeyError, TypeError) as error:
        raise TraceVisualizationError(
            "manifest constellation geometry is missing"
        ) from error
    if (
        not isinstance(num_orbits, int)
        or isinstance(num_orbits, bool)
        or not isinstance(satellites_per_orbit, int)
        or isinstance(satellites_per_orbit, bool)
        or num_orbits <= 0
        or satellites_per_orbit <= 0
        or num_orbits * satellites_per_orbit != node_count
    ):
        raise TraceVisualizationError("manifest constellation geometry differs")

    snapshots = []
    for record in manifest["slices"]:
        snapshots.append(
            TraceSnapshot(
                record["simulation_time_ns"],
                _load_positions(trace_root / record["nodes_file"], node_count),
                _load_links(trace_root / record["topology_file"], node_count),
            )
        )
    compute_node_ids = _load_compute_node_ids(compute_profile, node_count)
    compute_set = frozenset(compute_node_ids)
    relay_node_ids = tuple(
        node_id for node_id in range(node_count) if node_id not in compute_set
    )
    return OrbitTrace(
        root=trace_root,
        run_name=manifest["run_name"],
        node_count=node_count,
        num_orbits=num_orbits,
        satellites_per_orbit=satellites_per_orbit,
        duration_ns=manifest["simulation_duration_ns"],
        compute_node_ids=compute_node_ids,
        relay_node_ids=relay_node_ids,
        snapshots=tuple(snapshots),
    )


# Preserve the old Python function name without accepting old scenario inputs.
load_scenario = load_trace
