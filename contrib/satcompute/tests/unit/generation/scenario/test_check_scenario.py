#!/usr/bin/env python3
"""Positive and negative tests for the unified scenario checker."""

from __future__ import annotations

import json
import shutil
import tempfile
import unittest
from pathlib import Path
from typing import Any

from contrib.satcompute.tools.generation.scenario.check_scenario import (
    ScenarioCheckError,
    check_scenario,
    scenario_aggregate_sha256,
)
from contrib.satcompute.tools.generation.scenario.generate_scenario import (
    generate_scenario,
)
from contrib.satcompute.tests.unit.generation.scenario._helpers import (
    write_config,
)
from contrib.satcompute.tools.generation.topology.common.hash_utils import (
    compact_json_bytes,
    sha256_file,
)


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Any) -> None:
    path.write_bytes(compact_json_bytes(payload))


class ScenarioCheckerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary_directory = tempfile.TemporaryDirectory(
            prefix="satcompute-scenario-checker-"
        )
        cls.root = Path(cls.temporary_directory.name)
        config = write_config(
            cls.root / "static-fixed.json",
            mode="static",
            delay_mode="fixed",
        )
        cls.valid_output = cls.root / "valid"
        generate_scenario(config, cls.valid_output)
        cls.copy_index = 0

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary_directory.cleanup()

    def copy_valid_output(self) -> Path:
        type(self).copy_index += 1
        output = self.root / f"copy-{type(self).copy_index}"
        shutil.copytree(self.valid_output, output)
        return output

    def mutate_manifest(
        self,
        output: Path,
        field: str,
        value: Any,
    ) -> None:
        path = output / "scenario-manifest.json"
        manifest = read_json(path)
        manifest[field] = value
        write_json(path, manifest)

    def refresh_profile_hashes(self, output: Path) -> None:
        manifest_path = output / "scenario-manifest.json"
        manifest = read_json(manifest_path)
        profile_sha256 = sha256_file(
            output / "resources" / "compute-profile.json"
        )
        manifest["compute_profile_sha256"] = profile_sha256
        manifest["aggregate_scenario_sha256"] = (
            scenario_aggregate_sha256(
                manifest["topology_manifest_sha256"],
                manifest["topology_aggregate_data_sha256"],
                profile_sha256,
                manifest["scenario_config_sha256"],
            )
        )
        write_json(manifest_path, manifest)

    def test_valid_scenario_passes(self) -> None:
        summary = check_scenario(self.valid_output)
        self.assertEqual(summary["scenario_name"], "synthetic-66-compute-22")
        self.assertEqual(summary["topology_mode"], "static")
        self.assertEqual(summary["snapshot_count"], 1)
        self.assertEqual(summary["total_satellite_count"], 66)
        self.assertEqual(summary["compute_node_count"], 22)

    def test_root_and_resource_directory_shapes_are_strict(self) -> None:
        output = self.copy_valid_output()
        (output / "extra.txt").write_text("extra\n", encoding="utf-8")
        with self.assertRaisesRegex(ScenarioCheckError, "unknown=.*extra"):
            check_scenario(output)

        output = self.copy_valid_output()
        (output / "resources" / "extra.json").write_text(
            "{}\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(
            ScenarioCheckError,
            "resource entries differ",
        ):
            check_scenario(output)

        output = self.copy_valid_output()
        (output / "resources" / "compute-profile.json").unlink()
        with self.assertRaisesRegex(
            ScenarioCheckError,
            "missing=.*compute-profile",
        ):
            check_scenario(output)

    def test_manifest_is_closed_world(self) -> None:
        output = self.copy_valid_output()
        manifest_path = output / "scenario-manifest.json"
        manifest = read_json(manifest_path)
        manifest["unknown"] = 1
        write_json(manifest_path, manifest)
        with self.assertRaisesRegex(
            ScenarioCheckError,
            "unknown=.*unknown",
        ):
            check_scenario(output)

        output = self.copy_valid_output()
        manifest_path = output / "scenario-manifest.json"
        manifest = read_json(manifest_path)
        del manifest["compute_node_count"]
        write_json(manifest_path, manifest)
        with self.assertRaisesRegex(
            ScenarioCheckError,
            "missing=.*compute_node_count",
        ):
            check_scenario(output)

    def test_all_recorded_hashes_are_recomputed(self) -> None:
        hash_fields = (
            "scenario_config_sha256",
            "topology_manifest_sha256",
            "topology_aggregate_data_sha256",
            "compute_profile_sha256",
            "uv_lock_sha256",
            "aggregate_scenario_sha256",
        )
        for field in hash_fields:
            with self.subTest(field=field):
                output = self.copy_valid_output()
                self.mutate_manifest(output, field, "0" * 64)
                with self.assertRaises(ValueError):
                    check_scenario(output)

    def test_downsample_provenance_fields_are_paired(self) -> None:
        output = self.copy_valid_output()
        self.mutate_manifest(
            output,
            "reference_scenario_sha256",
            "0" * 64,
        )
        with self.assertRaisesRegex(
            ScenarioCheckError,
            "both be null or both be set",
        ):
            check_scenario(output)

    def test_manifest_counts_placement_and_types_are_recomputed(self) -> None:
        cases = (
            ("snapshot_count", 2),
            ("snapshot_count", True),
            ("total_satellite_count", 65),
            ("compute_node_count", 21),
            ("compute_node_ratio", 0.5),
            ("compute_nodes_per_orbit", [3, 4, 4, 3, 4, True]),
            ("selected_compute_node_ids", [1, 5, 9]),
            ("compute_rate_work_units_per_second", 1),
            ("aggregate_compute_rate_work_units_per_second", 1),
        )
        for field, value in cases:
            with self.subTest(field=field, value=value):
                output = self.copy_valid_output()
                self.mutate_manifest(output, field, value)
                with self.assertRaises(ValueError):
                    check_scenario(output)

    def test_profile_order_membership_and_rate_are_enforced(self) -> None:
        output = self.copy_valid_output()
        profile_path = output / "resources" / "compute-profile.json"
        profile = read_json(profile_path)
        profile["compute_nodes"].reverse()
        write_json(profile_path, profile)
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            check_scenario(output)

        output = self.copy_valid_output()
        profile_path = output / "resources" / "compute-profile.json"
        profile = read_json(profile_path)
        profile["compute_nodes"][0]["node_id"] = 999
        write_json(profile_path, profile)
        with self.assertRaisesRegex(ValueError, "unknown node_id"):
            check_scenario(output)

        output = self.copy_valid_output()
        profile_path = output / "resources" / "compute-profile.json"
        profile = read_json(profile_path)
        profile["compute_nodes"][0][
            "compute_rate_work_units_per_second"
        ] = 1
        write_json(profile_path, profile)
        self.refresh_profile_hashes(output)
        with self.assertRaisesRegex(
            ScenarioCheckError,
            "rate differs from config",
        ):
            check_scenario(output)

    def test_topology_mutation_is_rejected(self) -> None:
        output = self.copy_valid_output()
        topology_path = output / "topology" / "topology_0s.json"
        topology = read_json(topology_path)
        topology["links"][0]["delay"] += 1
        write_json(topology_path, topology)
        with self.assertRaises(ValueError):
            check_scenario(output)

    def test_provenance_types_are_strict(self) -> None:
        output = self.copy_valid_output()
        self.mutate_manifest(output, "satcompute_commit", "short")
        with self.assertRaisesRegex(
            ScenarioCheckError,
            "full lowercase commit SHA",
        ):
            check_scenario(output)

        output = self.copy_valid_output()
        self.mutate_manifest(output, "satcompute_worktree_clean", 1)
        with self.assertRaisesRegex(
            ScenarioCheckError,
            "must be boolean",
        ):
            check_scenario(output)


if __name__ == "__main__":
    unittest.main()
