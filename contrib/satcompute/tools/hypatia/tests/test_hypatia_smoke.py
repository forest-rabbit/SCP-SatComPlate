#!/usr/bin/env python3
"""Regression tests for the vendored minimal Hypatia smoke path."""

from __future__ import annotations

import json
import math
import sys
import tempfile
import unittest
from pathlib import Path


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from hypatia_adapter import (  # noqa: E402
    DEFAULT_ORIGIN,
    VENDOR_DIR,
    HypatiaAdapter,
    HypatiaAdapterError,
)
from smoke_positions import run_smoke  # noqa: E402
from vendor.hypatia_minimal import coordinates, tle_generator, tle_reader  # noqa: E402


EXPECTED_REPOSITORY = "https://github.com/snkas/hypatia.git"
EXPECTED_COMMIT = "0ac531c313eba2335f6344b46347140c3a0d4230"
EXPECTED_POSITIONS_SHA256 = (
    "01d0f2a672c32f88f780f691faef73d6f5d06c244be54d2ae21b25e1be5dba89"
)


class HypatiaSmokeTest(unittest.TestCase):
    """Verify the local, narrowly vendored Hypatia position path."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.first = run_smoke()
        cls.second = run_smoke()
        adapter = HypatiaAdapter()
        with tempfile.TemporaryDirectory(
            prefix="satcompute-valid-tle-"
        ) as temp:
            path = Path(temp) / "valid.tle"
            adapter.generate_tles(
                path,
                constellation_name="satcompute-smoke",
                num_orbits=2,
                satellites_per_orbit=3,
                phase_diff=True,
                inclination_deg=53.0,
                eccentricity=0.0000001,
                argument_of_perigee_deg=0.0,
                mean_motion_rev_per_day=15.19,
            )
            cls.valid_tle_text = path.read_text(encoding="utf-8")

    def test_environment_and_constellation_contract(self) -> None:
        self.assertEqual(self.first["upstream_commit"], EXPECTED_COMMIT)
        self.assertEqual(self.first["python_version"], "3.10.12")
        self.assertEqual(self.first["uv_version"].split()[1], "0.11.25")
        self.assertEqual(self.first["satellite_count"], 6)
        self.assertEqual(self.first["times_s"], [0.0, 60.0])

    def test_position_output_is_deterministic(self) -> None:
        self.assertEqual(
            self.first["positions_sha256"],
            EXPECTED_POSITIONS_SHA256,
        )
        self.assertEqual(
            self.first["positions_sha256"],
            self.second["positions_sha256"],
        )

    def test_adapter_uses_only_local_vendor_modules(self) -> None:
        adapter = HypatiaAdapter()
        self.assertEqual(adapter.repository, EXPECTED_REPOSITORY)
        self.assertEqual(adapter.commit, EXPECTED_COMMIT)
        self.assertEqual(adapter.component, "satgenpy")
        self.assertFalse(hasattr(adapter, "checkout"))
        for module in (coordinates, tle_generator, tle_reader):
            with self.subTest(module=module.__name__):
                module_path = Path(module.__file__).resolve()
                self.assertTrue(module_path.is_relative_to(VENDOR_DIR))
        self.assertNotIn("satgen", sys.modules)
        self.assertFalse(any(name.startswith("satgen.") for name in sys.modules))

    def test_vendor_provenance_and_license_are_complete(self) -> None:
        origin = json.loads(DEFAULT_ORIGIN.read_text(encoding="utf-8"))
        self.assertEqual(origin["repository"], EXPECTED_REPOSITORY)
        self.assertEqual(origin["commit"], EXPECTED_COMMIT)
        self.assertEqual(origin["component"], "satgenpy")
        self.assertEqual(origin["license"], "MIT")
        self.assertEqual(len(origin["sources"]), 3)
        for source in origin["sources"]:
            with self.subTest(path=source["vendored_path"]):
                self.assertTrue((TOOL_DIR / source["vendored_path"]).is_file())
                self.assertEqual(len(source["upstream_sha256"]), 64)

        license_path = VENDOR_DIR / "LICENSE"
        notice_path = TOOL_DIR / "THIRD_PARTY_NOTICES.md"
        self.assertTrue(license_path.is_file())
        self.assertTrue(notice_path.is_file())
        self.assertIn(
            "Copyright (c) 2020 ETH Zurich",
            license_path.read_text(encoding="utf-8"),
        )
        self.assertIn(
            EXPECTED_COMMIT,
            notice_path.read_text(encoding="utf-8"),
        )

    def test_invalid_position_time_is_rejected(self) -> None:
        adapter = HypatiaAdapter()
        with self.assertRaises(HypatiaAdapterError):
            adapter.satellite_position_at(object(), object(), -1.0)
        with self.assertRaises(HypatiaAdapterError):
            adapter.satellite_position_at(object(), object(), math.inf)

    def test_tle_reader_rejects_invalid_headers(self) -> None:
        body = "\n".join(self.valid_tle_text.splitlines()[1:]) + "\n"
        for header in ("", "2", "2 3 extra", "two 3", "0 3", "2 -1"):
            with self.subTest(header=header):
                text = f"{header}\n{body}" if header else body
                with self.assertRaisesRegex(
                    ValueError,
                    "header must contain exactly two positive integers",
                ):
                    self._read_tle_text(text)

    def test_tle_reader_rejects_truncated_extra_and_nonsequential_blocks(
        self,
    ) -> None:
        lines = self.valid_tle_text.splitlines()
        truncated = "\n".join(lines[:-2]) + "\n"
        with self.assertRaisesRegex(ValueError, "truncated at satellite 5"):
            self._read_tle_text(truncated)

        extra = (
            self.valid_tle_text
            + "satcompute-smoke 6\n"
            + lines[2]
            + "\n"
            + lines[3]
            + "\n"
        )
        with self.assertRaisesRegex(ValueError, "more satellite blocks"):
            self._read_tle_text(extra)

        nonsequential = list(lines)
        nonsequential[1] = "satcompute-smoke 1"
        with self.assertRaisesRegex(ValueError, "not increasing by one"):
            self._read_tle_text("\n".join(nonsequential) + "\n")

    @staticmethod
    def _read_tle_text(text: str) -> dict:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-malformed-tle-"
        ) as temp:
            path = Path(temp) / "input.tle"
            path.write_text(text, encoding="utf-8")
            return HypatiaAdapter().read_tles(path)


if __name__ == "__main__":
    unittest.main()
