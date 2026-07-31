#!/usr/bin/env python3
"""Tests for pattern-aware Walker TLE generation."""

from __future__ import annotations

import hashlib
import math
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from contrib.satcompute.tools.generation.topology.common.configuration import (
    WALKER_DELTA,
    load_config,
)
from contrib.satcompute.tools.generation.topology.orbit.hypatia.adapter import (
    HypatiaAdapter,
)
from contrib.satcompute.tools.generation.topology.orbit.hypatia.orbit_positions import (
    load_tle_orbit_constellation,
    position_samples_sha256,
)
from contrib.satcompute.tools.generation.topology.orbit.hypatia.vendor.hypatia_minimal.tle_generator import (
    tle_checksum,
)
from contrib.satcompute.tools.generation.topology.orbit.hypatia.walker_tles import (
    generate_walker_tles,
    walker_slots,
)
from contrib.satcompute.tests.support.paths import TOPOLOGY_GENERATION_ROOT


TOPOLOGY_ROOT = TOPOLOGY_GENERATION_ROOT
PRESET = TOPOLOGY_ROOT / "config" / "synthetic-66.json"
EXPECTED_STAR_RAAN = (0.0, 30.0, 60.0, 90.0, 120.0, 150.0)
EXPECTED_DELTA_RAAN = (0.0, 60.0, 120.0, 180.0, 240.0, 300.0)
EXPECTED_STAR_TLE_SHA256 = (
    "c3b0fd1118f00694274f4c3ce95d72f8e9942d67c6001199208fbb7a09e0d51f"
)
EXPECTED_STAR_POSITIONS_SHA256 = (
    "01f0fb97feb26e63582649a65225c273cfa3357a474fc1511dc9eddc6b6f2ea9"
)
EXPECTED_DELTA_TLE_SHA256 = (
    "3981469338b8eca57426ffb1b5f7a137d89374610c7b12445ab6b6304f710c0f"
)
EXPECTED_DELTA_POSITIONS_SHA256 = (
    "7b6acc6e78476321eae0b9da8d344f6c1ec54daf3255bbc9e408ad2cd819a652"
)


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
                EXPECTED_STAR_TLE_SHA256,
            )

            orbit = load_tle_orbit_constellation(
                first_path,
                self.adapter,
                66,
            )
            satellites = orbit.satellites
            self.assertEqual(len(satellites), 66)
            self.assertEqual(
                position_samples_sha256(orbit, (0.0, 60.0)),
                EXPECTED_STAR_POSITIONS_SHA256,
            )
            self._assert_raan(satellites, EXPECTED_STAR_RAAN)
            for satellite in satellites:
                start = self.adapter.satellite_position_at(
                    satellite,
                    orbit.epoch,
                    0.0,
                )
                end = self.adapter.satellite_position_at(
                    satellite,
                    orbit.epoch,
                    60.0,
                )
                self.assertTrue(all(math.isfinite(value) for value in start))
                self.assertTrue(all(math.isfinite(value) for value in end))
                self.assertGreater(math.dist(start, end), 1.0)

    def test_delta_tles_use_hypatia_legacy_formula(self) -> None:
        delta = replace(self.config, constellation_pattern=WALKER_DELTA)
        with tempfile.TemporaryDirectory(
            prefix="satcompute-delta-tles-"
        ) as temp:
            path = Path(temp) / "delta.tle"
            generate_walker_tles(path, delta, self.adapter)
            orbit = load_tle_orbit_constellation(path, self.adapter, 66)
            self.assertEqual(orbit.node_count, 66)
            self.assertEqual(
                hashlib.sha256(path.read_bytes()).hexdigest(),
                EXPECTED_DELTA_TLE_SHA256,
            )
            self.assertEqual(
                position_samples_sha256(orbit, (0.0, 60.0)),
                EXPECTED_DELTA_POSITIONS_SHA256,
            )
            self._assert_raan(
                orbit.satellites,
                EXPECTED_DELTA_RAAN,
            )

    def test_names_node_ids_and_tle_checksums_are_valid(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-tle-format-"
        ) as temp:
            path = Path(temp) / "star.tle"
            generate_walker_tles(path, self.config, self.adapter)
            lines = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines[0], "6 11")
            for node_id in range(66):
                name, line1, line2 = lines[1 + node_id * 3:4 + node_id * 3]
                self.assertEqual(name, f"synthetic-66 {node_id}")
                for line in (line1, line2):
                    self.assertEqual(len(line), 69)
                    self.assertEqual(tle_checksum(line[:68]), int(line[68]))

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
