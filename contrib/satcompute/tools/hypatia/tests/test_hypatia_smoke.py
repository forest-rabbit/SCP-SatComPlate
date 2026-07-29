#!/usr/bin/env python3
"""Regression tests for the frozen Hypatia smoke path."""

from __future__ import annotations

import math
import sys
import unittest
from pathlib import Path


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from hypatia_adapter import HypatiaAdapter, HypatiaAdapterError  # noqa: E402
from smoke_positions import run_smoke  # noqa: E402


EXPECTED_COMMIT = "0ac531c313eba2335f6344b46347140c3a0d4230"


class HypatiaSmokeTest(unittest.TestCase):
    """Verify the real, narrowly loaded Hypatia position path."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.first = run_smoke()
        cls.second = run_smoke()

    def test_environment_and_constellation_contract(self) -> None:
        self.assertEqual(self.first["upstream_commit"], EXPECTED_COMMIT)
        self.assertEqual(self.first["python_version"], "3.10.12")
        self.assertEqual(self.first["uv_version"].split()[1], "0.11.25")
        self.assertEqual(self.first["satellite_count"], 6)
        self.assertEqual(self.first["times_s"], [0.0, 60.0])

    def test_position_output_is_deterministic(self) -> None:
        self.assertEqual(
            self.first["positions_sha256"],
            self.second["positions_sha256"],
        )

    def test_adapter_does_not_import_satgen_package(self) -> None:
        self.assertNotIn("satgen", sys.modules)
        self.assertFalse(any(name.startswith("satgen.") for name in sys.modules))

    def test_invalid_position_time_is_rejected(self) -> None:
        adapter = HypatiaAdapter()
        with self.assertRaises(HypatiaAdapterError):
            adapter.satellite_position_at(object(), object(), -1.0)
        with self.assertRaises(HypatiaAdapterError):
            adapter.satellite_position_at(object(), object(), math.inf)


if __name__ == "__main__":
    unittest.main()
