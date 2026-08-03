#!/usr/bin/env python3
"""Read canonical SatCompute scenarios for display without changing them."""

from __future__ import annotations

import bisect
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Protocol

from ...generation.scenario.check_scenario import check_scenario
from ...generation.scenario.compute_profile import parse_compute_profile
from ...generation.scenario.configuration import (
    DYNAMIC_MODE,
    DynamicSchedule,
    ScenarioConfig,
    StaticSchedule,
    parse_config as parse_scenario_config,
)
from ...generation.topology.common.configuration import ConstellationConfig
from ...generation.topology.common.satcompute_schema import (
    SatComputeLink,
    parse_nodes_payload,
    parse_topology_payload,
    read_json,
)
from ...generation.topology.orbit.hypatia.orbit_positions import (
    OrbitConstellation,
    SatellitePosition,
    load_orbit_constellation,
)


SNAPSHOT_PATTERN = re.compile(r"^topology_(0|[1-9][0-9]*)s\.json$")


class ScenarioVisualizationError(ValueError):
    """Raised when a scenario cannot supply consistent display data."""


class PositionProvider(Protocol):
    """Narrow interface implemented by OrbitConstellation and test doubles."""

    @property
    def node_count(self) -> int:
        ...

    def positions_at(
        self,
        time_s: float,
    ) -> tuple[SatellitePosition, ...]:
        ...


@dataclass(frozen=True, order=True)
class PositionKm:
    """One Earth-fixed satellite position in kilometres."""

    node_id: int
    x_km: float
    y_km: float
    z_km: float

    @property
    def xyz_km(self) -> tuple[float, float, float]:
        return self.x_km, self.y_km, self.z_km


@dataclass(frozen=True)
class LinkSnapshot:
    """Active undirected ISLs at one canonical topology snapshot."""

    time_s: int
    links: tuple[tuple[int, int], ...]


@dataclass(frozen=True)
class OrbitScenario:
    """Validated scenario inputs and lazy orbit propagation for rendering."""

    root: Path
    config: ScenarioConfig
    orbit: PositionProvider
    compute_node_ids: tuple[int, ...]
    relay_node_ids: tuple[int, ...]
    link_snapshots: tuple[LinkSnapshot, ...]

    @property
    def node_count(self) -> int:
        return self.config.total_satellite_count

    @property
    def num_orbits(self) -> int:
        return self.config.constellation.num_orbits

    @property
    def satellites_per_orbit(self) -> int:
        return self.config.constellation.satellites_per_orbit

    @property
    def duration_s(self) -> int:
        schedule = self.config.topology.schedule
        if isinstance(schedule, DynamicSchedule):
            return schedule.duration_s
        return 0

    @property
    def topology_mode(self) -> str:
        return self.config.topology.mode

    def physical_time_s(self, simulation_time_s: float) -> float:
        """Map display time to the exact orbit propagation time."""
        simulation_time = self._validate_simulation_time(simulation_time_s)
        schedule = self.config.topology.schedule
        if isinstance(schedule, DynamicSchedule):
            return schedule.orbit_sample_offset_s + simulation_time
        if not isinstance(schedule, StaticSchedule):
            raise ScenarioVisualizationError("unknown scenario schedule type")
        return float(schedule.snapshot_time_s)

    def positions_at(
        self,
        simulation_time_s: float,
    ) -> tuple[PositionKm, ...]:
        """Propagate real orbit positions and convert metres to kilometres."""
        physical_time = self.physical_time_s(simulation_time_s)
        positions = self.orbit.positions_at(physical_time)
        if len(positions) != self.node_count:
            raise ScenarioVisualizationError(
                "orbit position count differs from scenario node count"
            )
        converted = []
        for expected_id, position in enumerate(positions):
            if position.node_id != expected_id:
                raise ScenarioVisualizationError(
                    "orbit positions must be ordered exactly as node IDs"
                )
            xyz_km = tuple(value / 1000.0 for value in position.xyz_m)
            if not all(math.isfinite(value) for value in xyz_km):
                raise ScenarioVisualizationError(
                    f"satellite {expected_id} has a non-finite position"
                )
            converted.append(PositionKm(expected_id, *xyz_km))
        return tuple(converted)

    def orbit_positions(
        self,
        positions: tuple[PositionKm, ...],
        orbit_index: int,
    ) -> tuple[tuple[float, float, float], ...]:
        """Select one Walker plane using canonical orbit-major node IDs."""
        if not 0 <= orbit_index < self.num_orbits:
            raise ScenarioVisualizationError("orbit_index is out of range")
        first = orbit_index * self.satellites_per_orbit
        last = first + self.satellites_per_orbit
        return tuple(position.xyz_km for position in positions[first:last])

    def links_at(self, simulation_time_s: float) -> tuple[tuple[int, int], ...]:
        """Return the active ISLs using last-snapshot-held semantics."""
        simulation_time = self._validate_simulation_time(simulation_time_s)
        times = tuple(snapshot.time_s for snapshot in self.link_snapshots)
        index = bisect.bisect_right(times, simulation_time) - 1
        if index < 0:
            raise ScenarioVisualizationError(
                "no topology snapshot exists at or before display time"
            )
        return self.link_snapshots[index].links

    def frame_times(self, step_s: float) -> tuple[float, ...]:
        """Return an inclusive full-timeline sequence without frame objects."""
        if (
            not isinstance(step_s, (int, float))
            or isinstance(step_s, bool)
            or not math.isfinite(step_s)
            or step_s <= 0.0
        ):
            raise ScenarioVisualizationError(
                "frame step must be a finite positive number"
            )
        if self.topology_mode != DYNAMIC_MODE:
            return (0.0,)
        duration = float(self.duration_s)
        result = []
        time_s = 0.0
        while time_s < duration:
            result.append(time_s)
            time_s += float(step_s)
        if not result or result[-1] != duration:
            result.append(duration)
        return tuple(result)

    def _validate_simulation_time(self, value: float) -> float:
        if (
            not isinstance(value, (int, float))
            or isinstance(value, bool)
            or not math.isfinite(value)
            or value < 0.0
            or value > self.duration_s
        ):
            raise ScenarioVisualizationError(
                f"simulation time must be in [0, {self.duration_s}]"
            )
        return float(value)


def _load_json(path: Path, name: str) -> dict:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ScenarioVisualizationError(
            f"cannot read {name} {path}: {error}"
        ) from error
    if not isinstance(payload, dict):
        raise ScenarioVisualizationError(f"{name} must be a JSON object")
    return payload


def _snapshot_times(topology_dir: Path) -> tuple[int, ...]:
    times = []
    for path in topology_dir.iterdir():
        match = SNAPSHOT_PATTERN.fullmatch(path.name)
        if match is not None:
            times.append(int(match.group(1)))
    result = tuple(sorted(times))
    if not result or result[0] != 0:
        raise ScenarioVisualizationError(
            "topology snapshots must begin with topology_0s.json"
        )
    return result


def _load_link_snapshots(
    topology_dir: Path,
    expected_node_ids: tuple[int, ...],
) -> tuple[LinkSnapshot, ...]:
    snapshots = []
    for time_s in _snapshot_times(topology_dir):
        nodes_path = topology_dir / f"nodes_{time_s}s.json"
        topology_path = topology_dir / f"topology_{time_s}s.json"
        node_ids = parse_nodes_payload(read_json(nodes_path))
        if node_ids != expected_node_ids:
            raise ScenarioVisualizationError(
                f"{nodes_path.name} node IDs differ from the scenario"
            )
        parsed_links: tuple[SatComputeLink, ...] = parse_topology_payload(
            read_json(topology_path),
            node_ids,
        )
        snapshots.append(
            LinkSnapshot(
                time_s,
                tuple(
                    (link.node1_id, link.node2_id)
                    for link in parsed_links
                ),
            )
        )
    return tuple(snapshots)


def load_scenario(
    root: Path,
    *,
    orbit_loader: Callable[[ConstellationConfig], PositionProvider] = (
        load_orbit_constellation
    ),
    validate: bool = True,
) -> OrbitScenario:
    """Load one canonical scenario and its exact compute/link roles."""
    scenario_root = Path(root).resolve()
    if validate:
        check_scenario(scenario_root)
    manifest = _load_json(
        scenario_root / "scenario-manifest.json",
        "scenario manifest",
    )
    try:
        config = parse_scenario_config(manifest["scenario_config"])
    except KeyError as error:
        raise ScenarioVisualizationError(
            "scenario manifest has no scenario_config"
        ) from error
    expected_node_ids = tuple(range(config.total_satellite_count))
    profile = _load_json(
        scenario_root / "resources" / "compute-profile.json",
        "compute profile",
    )
    compute_nodes = parse_compute_profile(
        profile,
        valid_node_ids=expected_node_ids,
    )
    compute_node_ids = tuple(node.node_id for node in compute_nodes)
    compute_set = frozenset(compute_node_ids)
    relay_node_ids = tuple(
        node_id for node_id in expected_node_ids if node_id not in compute_set
    )
    orbit: OrbitConstellation | PositionProvider = orbit_loader(
        config.constellation
    )
    if orbit.node_count != config.total_satellite_count:
        raise ScenarioVisualizationError(
            "orbit node count differs from scenario configuration"
        )
    link_snapshots = _load_link_snapshots(
        scenario_root / "topology",
        expected_node_ids,
    )
    schedule = config.topology.schedule
    expected_last = (
        schedule.duration_s if isinstance(schedule, DynamicSchedule) else 0
    )
    if link_snapshots[-1].time_s != expected_last:
        raise ScenarioVisualizationError(
            "topology snapshots do not cover the full scenario timeline"
        )
    return OrbitScenario(
        root=scenario_root,
        config=config,
        orbit=orbit,
        compute_node_ids=compute_node_ids,
        relay_node_ids=relay_node_ids,
        link_snapshots=link_snapshots,
    )
