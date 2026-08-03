#!/usr/bin/env python3
"""Tests for scenario roles, orbit time mapping, and held ISLs."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tools.visualization.orbit.scenario_reader import (
    load_scenario,
)
from contrib.satcompute.tests.unit.visualization.orbit._helpers import (
    FakeOrbit,
    write_fixture,
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
