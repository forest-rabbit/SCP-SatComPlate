#!/usr/bin/env python3
"""Tests for deterministic WGS72 mean-motion derivation."""

from __future__ import annotations

import math
import unittest

from contrib.satcompute.tools.generation.topology.orbit.hypatia.mean_motion import (
    WGS72_EARTH_RADIUS_KM,
    WGS72_MU_KM3_S2,
    mean_motion_rev_per_day,
    orbital_period_minutes,
)


class MeanMotionTest(unittest.TestCase):
    def test_wgs72_constants_are_explicit(self) -> None:
        self.assertEqual(WGS72_EARTH_RADIUS_KM, 6378.135)
        self.assertEqual(WGS72_MU_KM3_S2, 398600.8)

    def test_780_km_contract(self) -> None:
        mean_motion = mean_motion_rev_per_day(780.0)
        self.assertAlmostEqual(mean_motion, 14.33517932, places=8)
        self.assertAlmostEqual(
            orbital_period_minutes(mean_motion),
            100.452179,
            places=6,
        )

    def test_invalid_inputs_fail(self) -> None:
        for altitude in (-1.0, math.inf, math.nan, True):
            with self.subTest(altitude=altitude):
                with self.assertRaises(ValueError):
                    mean_motion_rev_per_day(altitude)
        with self.assertRaises(ValueError):
            orbital_period_minutes(0.0)


if __name__ == "__main__":
    unittest.main()
