#!/usr/bin/env python3
"""End-to-end tests for the synthetic-66 resolved manifest."""

from __future__ import annotations

import json
import hashlib
import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tools.generation.topology.orbit.hypatia.resolve_constellation import (
    MANIFEST_FILENAME,
    TLE_FILENAME,
    resolve_constellation,
)


TOPOLOGY_ROOT = Path(__file__).resolve().parents[1]
HYPATIA_ROOT = TOPOLOGY_ROOT / "orbit" / "hypatia"
PRESET = TOPOLOGY_ROOT / "config" / "synthetic-66.json"
EXPECTED_TLE_SHA256 = (
    "c3b0fd1118f00694274f4c3ce95d72f8e9942d67c6001199208fbb7a09e0d51f"
)
EXPECTED_POSITIONS_SHA256 = (
    "01f0fb97feb26e63582649a65225c273cfa3357a474fc1511dc9eddc6b6f2ea9"
)
UV_LOCK = TOPOLOGY_ROOT.parents[4] / "uv.lock"


class ResolveConstellationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary_directory = tempfile.TemporaryDirectory(
            prefix="satcompute-resolved-constellation-"
        )
        root = Path(cls.temporary_directory.name)
        cls.first_dir = root / "first"
        cls.second_dir = root / "second"
        cls.first = resolve_constellation(PRESET, cls.first_dir)
        cls.second = resolve_constellation(PRESET, cls.second_dir)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary_directory.cleanup()

    def test_resolved_physical_contract(self) -> None:
        manifest = self.first
        self.assertEqual(manifest["constellation_name"], "synthetic-66")
        self.assertEqual(manifest["constellation_pattern"], "walker-star")
        self.assertEqual(manifest["expected_satellite_count"], 66)
        self.assertEqual(manifest["raan_span_deg"], 180.0)
        self.assertEqual(
            manifest["raan_sequence_deg"],
            [0.0, 30.0, 60.0, 90.0, 120.0, 150.0],
        )
        self.assertEqual(manifest["phase_scheme"], "alternating-half-slot")
        self.assertEqual(manifest["isl_candidate_strategy"], "plus-grid")
        self.assertAlmostEqual(
            manifest["phase_offset_deg"],
            180.0 / 11.0,
        )
        self.assertFalse(manifest["seam_enabled"])
        self.assertEqual(manifest["max_isl_distance_m"], 6174589)
        self.assertEqual(manifest["candidate_count"], 121)
        self.assertEqual(
            manifest["candidate_degree_profile"],
            {
                "minimum_total_degree": 3,
                "maximum_total_degree": 4,
                "boundary_plane_total_degree": 3,
                "internal_plane_total_degree": 4,
            },
        )
        self.assertEqual(manifest["eccentricity"], 0.0000001)
        self.assertEqual(manifest["epoch_utc"], "2000-01-01T00:00:00Z")
        self.assertAlmostEqual(
            manifest["mean_motion_rev_per_day"],
            14.33517932,
            places=8,
        )

    def test_provenance_and_hash_contract(self) -> None:
        manifest = self.first
        self.assertEqual(
            manifest["hypatia_repository"],
            "https://github.com/snkas/hypatia.git",
        )
        self.assertEqual(
            manifest["hypatia_commit"],
            "0ac531c313eba2335f6344b46347140c3a0d4230",
        )
        self.assertEqual(
            manifest["hypatia_integration_mode"],
            "vendored-minimal",
        )
        origin = (
            HYPATIA_ROOT / "vendor" / "hypatia_minimal" / "ORIGIN.json"
        )
        self.assertEqual(
            manifest["hypatia_vendor_manifest_sha256"],
            hashlib.sha256(origin.read_bytes()).hexdigest(),
        )
        self.assertEqual(manifest["python_version"], "3.10.12")
        self.assertEqual(manifest["uv_version"], "0.11.25")
        self.assertEqual(manifest["tle_sha256"], EXPECTED_TLE_SHA256)
        self.assertEqual(manifest["position_sample_times_s"], [0.0, 60.0])
        self.assertEqual(
            manifest["positions_sha256"],
            EXPECTED_POSITIONS_SHA256,
        )
        self.assertEqual(
            manifest["uv_lock_sha256"],
            hashlib.sha256(UV_LOCK.read_bytes()).hexdigest(),
        )

    def test_repeated_outputs_are_identical(self) -> None:
        self.assertEqual(self.first, self.second)
        self.assertEqual(
            (self.first_dir / TLE_FILENAME).read_bytes(),
            (self.second_dir / TLE_FILENAME).read_bytes(),
        )
        self.assertEqual(
            (self.first_dir / MANIFEST_FILENAME).read_bytes(),
            (self.second_dir / MANIFEST_FILENAME).read_bytes(),
        )

    def test_pr2_outputs_only_tle_and_manifest(self) -> None:
        self.assertEqual(
            sorted(path.name for path in self.first_dir.iterdir()),
            [MANIFEST_FILENAME, TLE_FILENAME],
        )
        manifest = json.loads(
            (self.first_dir / MANIFEST_FILENAME).read_text(encoding="utf-8")
        )
        self.assertEqual(manifest, self.first)


if __name__ == "__main__":
    unittest.main()
