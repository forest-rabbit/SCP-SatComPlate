#!/usr/bin/env python3
"""Tests for WGS72 and same-frame instantaneous orbit rings."""

from __future__ import annotations

import math
import unittest

from contrib.satcompute.tools.visualization.orbit.geometry import (
    EARTH_RADIUS_KM,
    OrbitGeometryError,
    fit_orbit_ring,
    sphere_point_km,
)


def norm(vector: tuple[float, float, float]) -> float:
    return math.sqrt(sum(value * value for value in vector))


def dot(
    left: tuple[float, float, float],
    right: tuple[float, float, float],
) -> float:
    return sum(a * b for a, b in zip(left, right))


class OrbitGeometryTest(unittest.TestCase):
    def test_wgs72_sphere_points_have_the_exact_radius(self) -> None:
        for latitude, longitude in ((0.0, 0.0), (0.5, 1.2), (-1.0, 2.5)):
            with self.subTest(latitude=latitude, longitude=longitude):
                self.assertAlmostEqual(
                    norm(sphere_point_km(latitude, longitude)),
                    EARTH_RADIUS_KM,
                    places=9,
                )

    def test_ring_uses_one_earth_fixed_plane_and_average_radius(self) -> None:
        radius = 7000.0
        basis_u = (1.0, 0.0, 0.0)
        basis_v = (0.0, math.sqrt(0.5), math.sqrt(0.5))
        positions = tuple(
            tuple(
                radius
                * (
                    math.cos(angle) * basis_u[axis]
                    + math.sin(angle) * basis_v[axis]
                )
                for axis in range(3)
            )
            for angle in (0.0, math.pi / 2.0, math.pi, 3.0 * math.pi / 2.0)
        )
        ring = fit_orbit_ring(positions, sample_count=9)

        self.assertAlmostEqual(ring.radius_km, radius, places=9)
        self.assertEqual(ring.points_km[0], ring.points_km[-1])
        for point in ring.points_km:
            self.assertAlmostEqual(norm(point), radius, places=9)
            self.assertAlmostEqual(dot(point, ring.normal), 0.0, places=9)

    def test_degenerate_positions_are_rejected(self) -> None:
        with self.assertRaisesRegex(OrbitGeometryError, "non-collinear"):
            fit_orbit_ring(((7000.0, 0.0, 0.0), (7100.0, 0.0, 0.0)))


if __name__ == "__main__":
    unittest.main()
