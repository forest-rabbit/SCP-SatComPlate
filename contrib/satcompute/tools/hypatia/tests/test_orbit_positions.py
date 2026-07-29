#!/usr/bin/env python3
"""Tests for reusable deterministic orbit position propagation."""

from __future__ import annotations

import math
import sys
import unittest
from dataclasses import replace
from pathlib import Path


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from configuration import WALKER_DELTA, load_config  # noqa: E402
from hypatia_adapter import HypatiaAdapter  # noqa: E402
from orbit_positions import (  # noqa: E402
    load_orbit_constellation,
    position_samples_sha256,
)
from smoke_positions import run_smoke  # noqa: E402


PRESET = TOOL_DIR / "config" / "synthetic-66.json"
EXPECTED_PR1_POSITIONS_SHA256 = (
    "01d0f2a672c32f88f780f691faef73d6f5d06c244be54d2ae21b25e1be5dba89"
)
EXPECTED_STAR_POSITIONS_SHA256 = (
    "01f0fb97feb26e63582649a65225c273cfa3357a474fc1511dc9eddc6b6f2ea9"
)
EXPECTED_DELTA_POSITIONS_SHA256 = (
    "7b6acc6e78476321eae0b9da8d344f6c1ec54daf3255bbc9e408ad2cd819a652"
)


class OrbitPositionsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.config = load_config(PRESET)
        cls.adapter = HypatiaAdapter()
        cls.first = load_orbit_constellation(cls.config, cls.adapter)
        cls.second = load_orbit_constellation(cls.config, cls.adapter)

    def test_repeated_loads_have_stable_finite_node_order(self) -> None:
        for time_s in (0.0, 60.0):
            first = self.first.positions_at(time_s)
            second = self.second.positions_at(time_s)
            self.assertEqual(first, second)
            self.assertEqual(
                tuple(position.node_id for position in first),
                tuple(range(66)),
            )
            for position in first:
                self.assertTrue(
                    all(math.isfinite(value) for value in position.xyz_m)
                )

    def test_invalid_times_are_rejected(self) -> None:
        for time_s in (-1.0, math.nan, math.inf, True):
            with self.subTest(time_s=time_s):
                with self.assertRaisesRegex(
                    ValueError,
                    "finite non-negative",
                ):
                    self.first.positions_at(time_s)

    def test_frozen_position_hashes_are_unchanged(self) -> None:
        self.assertEqual(
            run_smoke()["positions_sha256"],
            EXPECTED_PR1_POSITIONS_SHA256,
        )
        self.assertEqual(
            position_samples_sha256(self.first, (0.0, 60.0)),
            EXPECTED_STAR_POSITIONS_SHA256,
        )
        delta = load_orbit_constellation(
            replace(self.config, constellation_pattern=WALKER_DELTA),
            self.adapter,
        )
        self.assertEqual(
            position_samples_sha256(delta, (0.0, 60.0)),
            EXPECTED_DELTA_POSITIONS_SHA256,
        )


if __name__ == "__main__":
    unittest.main()
