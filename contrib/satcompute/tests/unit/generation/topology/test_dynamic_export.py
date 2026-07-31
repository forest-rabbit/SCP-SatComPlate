#!/usr/bin/env python3
"""End-to-end tests for PR3-to-SatCompute dynamic export."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from contrib.satcompute.tools.generation.topology.check_export import (
    check_export,
)
from contrib.satcompute.tools.generation.topology.dynamic.export_satcompute import (
    DynamicTopologyExportError,
    export_satcompute_topology,
)
from contrib.satcompute.tools.generation.topology.dynamic.generate_dynamic_isls import (
    generate_dynamic_isl_output,
)
from contrib.satcompute.tests.support.paths import TOPOLOGY_GENERATION_ROOT


TOPOLOGY_ROOT = TOPOLOGY_GENERATION_ROOT
PRESET = TOPOLOGY_ROOT / "config" / "synthetic-66.json"


def _read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


class DynamicTopologyExportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary_directory = tempfile.TemporaryDirectory(
            prefix="satcompute-dynamic-export-"
        )
        root = Path(cls.temporary_directory.name)
        cls.source_dir = root / "source"
        generate_dynamic_isl_output(
            PRESET,
            120,
            60,
            cls.source_dir,
        )
        cls.fixed_first_dir = root / "fixed-first"
        cls.fixed_second_dir = root / "fixed-second"
        cls.distance_first_dir = root / "distance-first"
        cls.distance_second_dir = root / "distance-second"
        cls.fixed_first = export_satcompute_topology(
            cls.source_dir,
            "fixed",
            8000,
            10000000,
            cls.fixed_first_dir,
        )
        cls.fixed_second = export_satcompute_topology(
            cls.source_dir,
            "fixed",
            8000,
            10000000,
            cls.fixed_second_dir,
        )
        cls.distance_first = export_satcompute_topology(
            cls.source_dir,
            "distance",
            None,
            10000000,
            cls.distance_first_dir,
        )
        cls.distance_second = export_satcompute_topology(
            cls.source_dir,
            "distance",
            None,
            10000000,
            cls.distance_second_dir,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary_directory.cleanup()

    def test_fixed_and_distance_outputs_pass_checker(self) -> None:
        for output, mode in (
            (self.fixed_first_dir, "fixed"),
            (self.distance_first_dir, "distance"),
        ):
            with self.subTest(mode=mode):
                summary = check_export(
                    output,
                    expected_node_count=66,
                    source_dynamic_dir=self.source_dir,
                )
                self.assertEqual(summary["snapshot_count"], 3)
                self.assertEqual(summary["min_active_count"], 121)
                self.assertEqual(
                    sorted(path.name for path in output.iterdir()),
                    [
                        "manifest.json",
                        "nodes_0s.json",
                        "nodes_120s.json",
                        "nodes_60s.json",
                        "topology_0s.json",
                        "topology_120s.json",
                        "topology_60s.json",
                    ],
                )

    def test_fixed_delay_is_constant_and_nodes_are_repeated(self) -> None:
        initial_nodes = (self.fixed_first_dir / "nodes_0s.json").read_bytes()
        for time_s in (0, 60, 120):
            self.assertEqual(
                (self.fixed_first_dir / f"nodes_{time_s}s.json").read_bytes(),
                initial_nodes,
            )
            topology = _read_json(
                self.fixed_first_dir / f"topology_{time_s}s.json"
            )
            self.assertEqual(len(topology["links"]), 121)
            self.assertTrue(
                all(link["delay"] == 8000 for link in topology["links"])
            )
            self.assertTrue(
                all(
                    link["link_bandwidth"] == 10000000
                    for link in topology["links"]
                )
            )

    def test_distance_delay_changes_without_endpoint_changes(self) -> None:
        topologies = {
            time_s: _read_json(
                self.distance_first_dir / f"topology_{time_s}s.json"
            )["links"]
            for time_s in (0, 60, 120)
        }
        endpoint_sets = {
            time_s: [
                (link["node1_id"], link["node2_id"])
                for link in links
            ]
            for time_s, links in topologies.items()
        }
        self.assertEqual(endpoint_sets[0], endpoint_sets[60])
        self.assertEqual(endpoint_sets[0], endpoint_sets[120])
        delays_by_time = {
            time_s: [link["delay"] for link in links]
            for time_s, links in topologies.items()
        }
        self.assertNotEqual(delays_by_time[0], delays_by_time[60])
        self.assertTrue(
            all(
                link["link_bandwidth"] == 10000000
                for links in topologies.values()
                for link in links
            )
        )

    def test_repeated_data_and_manifests_are_byte_identical(self) -> None:
        for first, second in (
            (self.fixed_first_dir, self.fixed_second_dir),
            (self.distance_first_dir, self.distance_second_dir),
        ):
            with self.subTest(first=first.name):
                self.assertEqual(
                    sorted(path.name for path in first.iterdir()),
                    sorted(path.name for path in second.iterdir()),
                )
                for path in first.iterdir():
                    self.assertEqual(
                        path.read_bytes(),
                        (second / path.name).read_bytes(),
                    )
        self.assertNotEqual(
            self.fixed_first["aggregate_data_sha256"],
            self.distance_first["aggregate_data_sha256"],
        )

    def test_manifest_provenance_and_schedule_are_complete(self) -> None:
        manifest = self.distance_first
        source_manifest = _read_json(self.source_dir / "manifest.json")
        self.assertEqual(manifest["generation_mode"], "dynamic")
        self.assertEqual(manifest["duration_s"], 120)
        self.assertEqual(manifest["step_s"], 60)
        self.assertEqual(manifest["orbit_sample_offset_s"], 0)
        self.assertEqual(manifest["snapshot_count"], 3)
        self.assertEqual(manifest["first_time_s"], 0)
        self.assertEqual(manifest["last_time_s"], 120)
        self.assertEqual(manifest["candidate_count"], 121)
        self.assertEqual(
            manifest["source_dynamic_aggregate_sha256"],
            source_manifest["aggregate_sha256"],
        )
        self.assertIsNone(manifest["fixed_delay_us"])

    def test_parameter_contract_is_strict(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-dynamic-export-invalid-"
        ) as temp:
            root = Path(temp)
            invalid_cases = (
                ("fixed", None, 10000000),
                ("distance", 8000, 10000000),
                ("unknown", None, 10000000),
                ("fixed", -1, 10000000),
                ("fixed", 8000, 0),
            )
            for index, arguments in enumerate(invalid_cases):
                with self.subTest(arguments=arguments):
                    with self.assertRaises(DynamicTopologyExportError):
                        export_satcompute_topology(
                            self.source_dir,
                            *arguments,
                            root / f"invalid-{index}",
                        )

    def test_atomic_and_source_failures_do_not_publish(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-dynamic-export-atomic-"
        ) as temp:
            root = Path(temp)
            blocked = root / "blocked"
            blocked.mkdir()
            marker = blocked / "keep.txt"
            marker.write_text("keep\n", encoding="utf-8")
            with self.assertRaisesRegex(
                DynamicTopologyExportError,
                "refusing to overwrite non-empty",
            ):
                export_satcompute_topology(
                    self.source_dir,
                    "fixed",
                    8000,
                    10000000,
                    blocked,
                )
            self.assertEqual(marker.read_text(encoding="utf-8"), "keep\n")

            failed = root / "failed"
            with patch(
                "contrib.satcompute.tools.generation.topology.dynamic."
                "export_satcompute.check_export",
                side_effect=ValueError("checker failed"),
            ):
                with self.assertRaisesRegex(ValueError, "checker failed"):
                    export_satcompute_topology(
                        self.source_dir,
                        "fixed",
                        8000,
                        10000000,
                        failed,
                    )
            self.assertFalse(failed.exists())
            self.assertFalse((root / "failed.tmp").exists())

    def test_tampered_source_hash_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-dynamic-export-source-"
        ) as temp:
            root = Path(temp)
            source = root / "source"
            generate_dynamic_isl_output(PRESET, 0, 1, source)
            with (source / "candidate-isls.json").open(
                "ab"
            ) as stream:
                stream.write(b" ")
            with self.assertRaisesRegex(
                DynamicTopologyExportError,
                "candidate SHA-256",
            ):
                export_satcompute_topology(
                    source,
                    "fixed",
                    8000,
                    10000000,
                    root / "output",
                )
            self.assertFalse((root / "output").exists())


if __name__ == "__main__":
    unittest.main()
