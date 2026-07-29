#!/usr/bin/env python3
"""Tests for pattern-aware Walker TLE generation."""

from __future__ import annotations

import hashlib
import math
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from configuration import WALKER_DELTA, load_config  # noqa: E402
from hypatia_adapter import HypatiaAdapter  # noqa: E402
from walker_tles import generate_walker_tles, walker_slots  # noqa: E402


PRESET = TOOL_DIR / "config" / "synthetic-66.json"
EXPECTED_STAR_RAAN = (0.0, 30.0, 60.0, 90.0, 120.0, 150.0)
EXPECTED_DELTA_RAAN = (0.0, 60.0, 120.0, 180.0, 240.0, 300.0)


class WalkerTleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.config = load_config(PRESET)
        cls.adapter = HypatiaAdapter()

    def test_star_and_delta_raan_contracts(self) -> None:
        star_slots = walker_slots(self.config)
        delta_slots = walker_slots(
            replace(self.config, constellation_pattern=WALKER_DELTA)
        )
        star_raan = tuple(
            star_slots[orbit * self.config.satellites_per_orbit].raan_deg
            for orbit in range(self.config.num_orbits)
        )
        delta_raan = tuple(
            delta_slots[orbit * self.config.satellites_per_orbit].raan_deg
            for orbit in range(self.config.num_orbits)
        )
        self.assertEqual(star_raan, EXPECTED_STAR_RAAN)
        self.assertEqual(delta_raan, EXPECTED_DELTA_RAAN)

    def test_phase_and_node_id_contracts(self) -> None:
        slots = walker_slots(self.config)
        self.assertEqual(
            (
                slots[0].node_id,
                slots[10].node_id,
                slots[11].node_id,
                slots[65].node_id,
            ),
            (0, 10, 11, 65),
        )
        self.assertEqual(
            (
                slots[0].orbit_index,
                slots[0].slot_index,
                slots[10].orbit_index,
                slots[10].slot_index,
                slots[11].orbit_index,
                slots[11].slot_index,
                slots[65].orbit_index,
                slots[65].slot_index,
            ),
            (0, 0, 0, 10, 1, 0, 5, 10),
        )
        self.assertEqual(slots[0].mean_anomaly_deg, 0.0)
        self.assertAlmostEqual(slots[11].mean_anomaly_deg, 180.0 / 11.0)

        aligned = walker_slots(replace(self.config, phase_diff=False))
        for orbit in range(self.config.num_orbits):
            self.assertEqual(
                aligned[orbit * self.config.satellites_per_orbit]
                .mean_anomaly_deg,
                0.0,
            )

    def test_star_tles_are_deterministic_and_propagatable(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-star-tles-"
        ) as temp:
            first_path = Path(temp) / "first.tle"
            second_path = Path(temp) / "second.tle"
            generate_walker_tles(first_path, self.config, self.adapter)
            generate_walker_tles(second_path, self.config, self.adapter)
            first = first_path.read_bytes()
            second = second_path.read_bytes()
            self.assertEqual(first, second)
            self.assertEqual(
                hashlib.sha256(first).hexdigest(),
                hashlib.sha256(second).hexdigest(),
            )

            constellation = self.adapter.read_tles(first_path)
            satellites = constellation["satellites"]
            self.assertEqual(len(satellites), 66)
            self._assert_raan(satellites, EXPECTED_STAR_RAAN)
            for satellite in satellites:
                start = self.adapter.satellite_position_at(
                    satellite,
                    constellation["epoch"],
                    0.0,
                )
                end = self.adapter.satellite_position_at(
                    satellite,
                    constellation["epoch"],
                    60.0,
                )
                self.assertTrue(all(math.isfinite(value) for value in start))
                self.assertTrue(all(math.isfinite(value) for value in end))
                self.assertGreater(math.dist(start, end), 1.0)

    def test_delta_tles_use_frozen_hypatia_formula(self) -> None:
        delta = replace(self.config, constellation_pattern=WALKER_DELTA)
        with tempfile.TemporaryDirectory(
            prefix="satcompute-delta-tles-"
        ) as temp:
            path = Path(temp) / "delta.tle"
            generate_walker_tles(path, delta, self.adapter)
            constellation = self.adapter.read_tles(path)
            self.assertEqual(len(constellation["satellites"]), 66)
            self._assert_raan(
                constellation["satellites"],
                EXPECTED_DELTA_RAAN,
            )

    def _assert_raan(
        self,
        satellites: list,
        expected: tuple[float, ...],
    ) -> None:
        for orbit, expected_raan in enumerate(expected):
            satellite = satellites[
                orbit * self.config.satellites_per_orbit
            ]
            actual_raan = math.degrees(float(satellite._raan)) % 360.0
            self.assertAlmostEqual(actual_raan, expected_raan, places=3)


if __name__ == "__main__":
    unittest.main()
