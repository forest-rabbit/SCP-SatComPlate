#!/usr/bin/env python3
"""End-to-end tests for canonical static topology generation."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from contrib.satcompute.tools.generation.topology.check_export import (
    check_export,
)
from contrib.satcompute.tools.generation.topology.static.generate_static_topology import (
    MANIFEST_FILENAME,
    NODES_FILENAME,
    TOPOLOGY_FILENAME,
    StaticTopologyGenerationError,
    generate_static_topology,
)


TOPOLOGY_ROOT = Path(__file__).resolve().parents[1]
PRESET = TOPOLOGY_ROOT / "config" / "synthetic-66.json"


class StaticTopologyGenerationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary_directory = tempfile.TemporaryDirectory(
            prefix="satcompute-static-topology-"
        )
        root = Path(cls.temporary_directory.name)
        cls.fixed_first_dir = root / "fixed-first"
        cls.fixed_second_dir = root / "fixed-second"
        cls.distance_dir = root / "distance"
        cls.fixed_first = generate_static_topology(
            PRESET,
            0,
            "fixed",
            8000,
            10000000,
            cls.fixed_first_dir,
        )
        cls.fixed_second = generate_static_topology(
            PRESET,
            0,
            "fixed",
            8000,
            10000000,
            cls.fixed_second_dir,
        )
        cls.distance = generate_static_topology(
            PRESET,
            60,
            "distance",
            None,
            10000000,
            cls.distance_dir,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary_directory.cleanup()

    def test_fixed_output_is_canonical_and_repeatable(self) -> None:
        expected_names = [
            MANIFEST_FILENAME,
            NODES_FILENAME,
            TOPOLOGY_FILENAME,
        ]
        self.assertEqual(
            sorted(path.name for path in self.fixed_first_dir.iterdir()),
            expected_names,
        )
        for filename in expected_names:
            self.assertEqual(
                (self.fixed_first_dir / filename).read_bytes(),
                (self.fixed_second_dir / filename).read_bytes(),
            )
        summary = check_export(
            self.fixed_first_dir,
            expected_node_count=66,
        )
        self.assertEqual(summary["snapshot_count"], 1)
        self.assertEqual(summary["min_active_count"], 121)
        manifest = self.fixed_first
        self.assertEqual(manifest["generation_mode"], "static")
        self.assertEqual(manifest["duration_s"], 0)
        self.assertIsNone(manifest["step_s"])
        self.assertEqual(manifest["fixed_delay_us"], 8000)
        self.assertEqual(manifest["candidate_count"], 121)
        topology = json.loads(
            (self.fixed_first_dir / TOPOLOGY_FILENAME).read_text(
                encoding="utf-8"
            )
        )
        self.assertTrue(
            all(link["delay"] == 8000 for link in topology["links"])
        )
        self.assertTrue(
            all(
                link["link_bandwidth"] == 10000000
                for link in topology["links"]
            )
        )

    def test_distance_output_uses_requested_orbit_sample(self) -> None:
        summary = check_export(
            self.distance_dir,
            expected_node_count=66,
        )
        self.assertEqual(summary["snapshot_count"], 1)
        self.assertEqual(self.distance["duration_s"], 60)
        self.assertEqual(self.distance["first_time_s"], 0)
        self.assertEqual(self.distance["last_time_s"], 0)
        self.assertEqual(self.distance["delay_mode"], "distance")
        self.assertIsNone(self.distance["fixed_delay_us"])
        topology = json.loads(
            (self.distance_dir / TOPOLOGY_FILENAME).read_text(
                encoding="utf-8"
            )
        )
        delays = {link["delay"] for link in topology["links"]}
        self.assertGreater(min(delays), 0)
        self.assertGreater(len(delays), 1)

    def test_parameter_contract_is_strict(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-static-invalid-"
        ) as temp:
            root = Path(temp)
            invalid_cases = (
                (-1, "fixed", 8000, 10000000),
                (0, "fixed", None, 10000000),
                (0, "distance", 8000, 10000000),
                (0, "unknown", None, 10000000),
                (0, "fixed", -1, 10000000),
                (0, "fixed", 8000, 0),
            )
            for index, arguments in enumerate(invalid_cases):
                with self.subTest(arguments=arguments):
                    with self.assertRaises(StaticTopologyGenerationError):
                        generate_static_topology(
                            PRESET,
                            *arguments,
                            root / f"invalid-{index}",
                        )

    def test_atomic_output_rejects_nonempty_and_cleans_stale(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-static-atomic-"
        ) as temp:
            root = Path(temp)
            blocked = root / "blocked"
            blocked.mkdir()
            marker = blocked / "keep.txt"
            marker.write_text("keep\n", encoding="utf-8")
            with self.assertRaisesRegex(
                StaticTopologyGenerationError,
                "refusing to overwrite non-empty",
            ):
                generate_static_topology(
                    PRESET,
                    0,
                    "fixed",
                    8000,
                    10000000,
                    blocked,
                )
            self.assertEqual(marker.read_text(encoding="utf-8"), "keep\n")

            output = root / "output"
            stale = root / "output.tmp"
            stale.mkdir()
            (stale / "partial.txt").write_text(
                "partial\n",
                encoding="utf-8",
            )
            generate_static_topology(
                PRESET,
                0,
                "fixed",
                8000,
                10000000,
                output,
            )
            self.assertTrue(output.is_dir())
            self.assertFalse(stale.exists())

    def test_checker_failure_does_not_publish(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-static-checker-failure-"
        ) as temp:
            output = Path(temp) / "output"
            with patch(
                "contrib.satcompute.tools.generation.topology.static."
                "generate_static_topology.check_export",
                side_effect=ValueError("checker failed"),
            ):
                with self.assertRaisesRegex(ValueError, "checker failed"):
                    generate_static_topology(
                        PRESET,
                        0,
                        "fixed",
                        8000,
                        10000000,
                        output,
                    )
            self.assertFalse(output.exists())
            self.assertFalse((Path(temp) / "output.tmp").exists())


if __name__ == "__main__":
    unittest.main()
