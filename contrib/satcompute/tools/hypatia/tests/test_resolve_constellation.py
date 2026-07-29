#!/usr/bin/env python3
"""End-to-end tests for the synthetic-66 resolved manifest."""

from __future__ import annotations

import json
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from resolve_constellation import (  # noqa: E402
    MANIFEST_FILENAME,
    TLE_FILENAME,
    resolve_constellation,
)


PRESET = TOOL_DIR / "config" / "synthetic-66.json"
EXPECTED_TLE_SHA256 = (
    "c3b0fd1118f00694274f4c3ce95d72f8e9942d67c6001199208fbb7a09e0d51f"
)
EXPECTED_POSITIONS_SHA256 = (
    "01f0fb97feb26e63582649a65225c273cfa3357a474fc1511dc9eddc6b6f2ea9"
)
EXPECTED_UV_LOCK_SHA256 = (
    "1e4fdeda462596bff581dd9fe60208407d4fd1b4396e9ece59a71ff911e725fc"
)


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
        self.assertAlmostEqual(
            manifest["phase_offset_deg"],
            180.0 / 11.0,
        )
        self.assertFalse(manifest["seam_enabled"])
        self.assertEqual(manifest["max_isl_distance_m"], 6174589)
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
            TOOL_DIR / "vendor" / "hypatia_minimal" / "ORIGIN.json"
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
            EXPECTED_UV_LOCK_SHA256,
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
