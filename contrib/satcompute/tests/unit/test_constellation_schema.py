"""Lightweight contract check for the native ns-3.48 LEO shell CSV."""

from __future__ import annotations

import csv
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
EXAMPLE_PATH = (
    REPOSITORY_ROOT
    / "contrib/satcompute/input/topology/constellations/synthetic-66.csv"
)
EXPECTED_HEADER = [
    "altitudeKm",
    "inclinationDegrees",
    "numberOfPlanes",
    "numberOfSatellitesPerPlane",
    "phasingFactor",
    "raanSpanDeg",
]


class ConstellationCsvTest(unittest.TestCase):
    """The selected fixture stays compatible with the native helper format."""

    def test_example_contains_one_66_satellite_shell(self) -> None:
        with EXAMPLE_PATH.open(encoding="utf-8", newline="") as source:
            rows = [
                row
                for row in csv.reader(source)
                if row and not row[0].lstrip().startswith("#")
            ]

        self.assertEqual(rows[0], EXPECTED_HEADER)
        self.assertEqual(len(rows), 2)
        altitude, inclination, planes, satellites, phasing, raan_span = rows[1]
        self.assertEqual(float(altitude), 780.0)
        self.assertEqual(float(inclination), 86.4)
        self.assertEqual(int(planes) * int(satellites), 66)
        self.assertEqual(float(phasing), 1.0)
        self.assertEqual(float(raan_span), 180.0)


if __name__ == "__main__":
    unittest.main()
