#!/usr/bin/env python3
"""Tests for scenario roles, orbit time mapping, and held ISLs."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tools.generation.topology.orbit.hypatia.orbit_positions import (
    SatellitePosition,
)
from contrib.satcompute.tools.visualization.orbit.scenario_reader import (
    load_scenario,
)


class FakeOrbit:
    def __init__(self, node_count: int) -> None:
        self.node_count = node_count
        self.requested_times: list[float] = []

    def positions_at(self, time_s: float) -> tuple[SatellitePosition, ...]:
        self.requested_times.append(time_s)
        return tuple(
            SatellitePosition(
                node_id,
                7_000_000.0 + node_id * 1000.0,
                time_s * 1000.0,
                node_id * 10_000.0,
            )
            for node_id in range(self.node_count)
        )


def scenario_config(mode: str) -> dict:
    schedule = (
        {
            "start_time_s": 0,
            "orbit_sample_offset_s": 100,
            "duration_s": 20,
            "step_s": 20,
        }
        if mode == "dynamic"
        else {"snapshot_time_s": 17}
    )
    return {
        "schema_version": "0.1",
        "scenario_name": f"visualization-{mode}",
        "constellation": {
            "constellation_name": "visualization-fixture",
            "constellation_pattern": "walker-star",
            "num_orbits": 2,
            "satellites_per_orbit": 2,
            "altitude_km": 780.0,
            "inclination_deg": 86.4,
            "phase_diff": True,
        },
        "topology": {
            "mode": mode,
            "schedule": schedule,
            "isl_candidate_strategy": "plus-grid",
            "seam_enabled": False,
            "max_isl_distance_m": 6_174_589,
            "delay_mode": "fixed",
            "fixed_delay_us": 8000,
            "link_bandwidth_kbps": 2_000_000,
        },
        "compute": {
            "compute_node_count": 2,
            "placement_strategy": "even-plane-slot",
            "compute_rate_work_units_per_second": 1_500_000,
        },
    }


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload) + "\n", encoding="utf-8")


def write_fixture(root: Path, mode: str) -> None:
    config = scenario_config(mode)
    write_json(
        root / "scenario-manifest.json",
        {"scenario_config": config},
    )
    write_json(
        root / "resources" / "compute-profile.json",
        {
            "schema_version": "0.1",
            "compute_nodes": [
                {
                    "node_id": 1,
                    "compute_rate_work_units_per_second": 1_500_000,
                },
                {
                    "node_id": 3,
                    "compute_rate_work_units_per_second": 1_500_000,
                },
            ],
        },
    )
    nodes = {
        "nodes": [
            {"node_id": node_id, "node_type": "sat"}
            for node_id in range(4)
        ]
    }
    times = (0, 20) if mode == "dynamic" else (0,)
    for time_s in times:
        write_json(root / "topology" / f"nodes_{time_s}s.json", nodes)
        endpoints = (0, 1) if time_s == 0 else (2, 3)
        write_json(
            root / "topology" / f"topology_{time_s}s.json",
            {
                "links": [
                    {
                        "node1_id": endpoints[0],
                        "node2_id": endpoints[1],
                        "type": "sat",
                        "delay": 8000,
                        "link_bandwidth": 2_000_000,
                    }
                ]
            },
        )


class OrbitScenarioReaderTest(unittest.TestCase):
    def test_dynamic_time_roles_frames_and_held_links(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-visualization-scenario-"
        ) as temp:
            root = Path(temp)
            write_fixture(root, "dynamic")
            orbit = FakeOrbit(4)
            scenario = load_scenario(
                root,
                orbit_loader=lambda config: orbit,
                validate=False,
            )

            self.assertEqual(scenario.compute_node_ids, (1, 3))
            self.assertEqual(scenario.relay_node_ids, (0, 2))
            self.assertEqual(scenario.frame_times(10), (0.0, 10.0, 20.0))
            self.assertEqual(scenario.links_at(0), ((0, 1),))
            self.assertEqual(scenario.links_at(19.999), ((0, 1),))
            self.assertEqual(scenario.links_at(20), ((2, 3),))
            positions = scenario.positions_at(7)
            self.assertEqual(orbit.requested_times, [107.0])
            self.assertEqual(positions[0].xyz_km, (7000.0, 107.0, 0.0))
            self.assertEqual(
                scenario.orbit_positions(positions, 1),
                tuple(position.xyz_km for position in positions[2:]),
            )

    def test_static_scenario_has_one_frame_at_snapshot_time(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-visualization-static-"
        ) as temp:
            root = Path(temp)
            write_fixture(root, "static")
            orbit = FakeOrbit(4)
            scenario = load_scenario(
                root,
                orbit_loader=lambda config: orbit,
                validate=False,
            )

            self.assertEqual(scenario.duration_s, 0)
            self.assertEqual(scenario.frame_times(1), (0.0,))
            scenario.positions_at(0)
            self.assertEqual(orbit.requested_times, [17.0])
            self.assertEqual(scenario.links_at(0), ((0, 1),))


if __name__ == "__main__":
    unittest.main()
