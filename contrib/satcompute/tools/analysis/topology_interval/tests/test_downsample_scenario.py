#!/usr/bin/env python3
"""Tests for copy-only unified-scenario downsampling."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tools.analysis.topology_interval.downsample_scenario import (
    ScenarioDownsampleError,
    downsample_scenario,
)
from contrib.satcompute.tools.generation.scenario.check_scenario import (
    check_scenario,
)
from contrib.satcompute.tools.generation.scenario.generate_scenario import (
    generate_scenario,
)


SCENARIO_ROOT = Path(__file__).resolve().parents[3] / "generation" / "scenario"
PRESET = SCENARIO_ROOT / "config" / "synthetic-66-compute-22.json"


class DownsampleScenarioTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary_directory = tempfile.TemporaryDirectory(
            prefix="satcompute-downsample-"
        )
        cls.root = Path(cls.temporary_directory.name)
        payload = json.loads(PRESET.read_text(encoding="utf-8"))
        payload["topology"]["schedule"].update(
            {
                "orbit_sample_offset_s": 60,
                "duration_s": 20,
                "step_s": 1,
            }
        )
        cls.config = cls.root / "reference-config.json"
        cls.config.write_text(
            json.dumps(payload, indent=2) + "\n",
            encoding="utf-8",
        )
        cls.reference = cls.root / "reference"
        generate_scenario(cls.config, cls.reference)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary_directory.cleanup()

    def test_selected_snapshots_and_hashes_are_exact(self) -> None:
        output = self.root / "interval-5"
        manifest = downsample_scenario(self.reference, 5, output)
        summary = check_scenario(output)
        self.assertEqual(summary["snapshot_count"], 5)
        self.assertEqual(
            sorted(
                path.name
                for path in (output / "topology").glob("topology_*s.json")
            ),
            [
                "topology_0s.json",
                "topology_10s.json",
                "topology_15s.json",
                "topology_20s.json",
                "topology_5s.json",
            ],
        )
        for time_s in (0, 5, 10, 15, 20):
            for kind in ("nodes", "topology"):
                filename = f"{kind}_{time_s}s.json"
                self.assertEqual(
                    (output / "topology" / filename).read_bytes(),
                    (
                        self.reference
                        / "topology"
                        / filename
                    ).read_bytes(),
                )
        self.assertEqual(
            (
                output / "resources" / "compute-profile.json"
            ).read_bytes(),
            (
                self.reference / "resources" / "compute-profile.json"
            ).read_bytes(),
        )
        reference_manifest = json.loads(
            (
                self.reference / "scenario-manifest.json"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(
            manifest["reference_scenario_sha256"],
            reference_manifest["aggregate_scenario_sha256"],
        )
        self.assertEqual(manifest["downsample_interval_s"], 5)
        self.assertEqual(
            manifest["scenario_config"]["topology"]["schedule"],
            {
                "start_time_s": 0,
                "orbit_sample_offset_s": 60,
                "duration_s": 20,
                "step_s": 5,
            },
        )

    def test_invalid_reference_and_interval_are_rejected(self) -> None:
        with self.assertRaisesRegex(
            ScenarioDownsampleError,
            "divisible",
        ):
            downsample_scenario(
                self.reference,
                3,
                self.root / "invalid-3",
            )
        with self.assertRaisesRegex(
            ScenarioDownsampleError,
            "must not overlap",
        ):
            downsample_scenario(
                self.reference,
                5,
                self.reference / "nested",
            )
        first = self.root / "first-downsample"
        downsample_scenario(self.reference, 10, first)
        with self.assertRaisesRegex(
            ScenarioDownsampleError,
            "original reference",
        ):
            downsample_scenario(first, 20, self.root / "second-downsample")


if __name__ == "__main__":
    unittest.main()
