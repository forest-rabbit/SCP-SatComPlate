#!/usr/bin/env python3
"""End-to-end tests for unified scenario generation."""

from __future__ import annotations

import json
import tempfile
import unittest
import builtins
from pathlib import Path
from unittest.mock import patch

from contrib.satcompute.tools.generation.scenario.check_scenario import (
    check_scenario,
)
from contrib.satcompute.tools.generation.scenario.generate_scenario import (
    ScenarioGenerationError,
    ScenarioVisualizationError,
    _run_optional_visualization,
    generate_scenario,
)
from contrib.satcompute.tests.support.fixtures import directory_bytes
from contrib.satcompute.tests.unit.generation.scenario._helpers import (
    preset_payload,
    write_config,
)


EXPECTED_COMPUTE_NODE_IDS = [
    1, 5, 9, 12, 15, 17, 20, 23, 26, 28, 31, 34, 38, 42, 45, 48, 50,
    53, 56, 59, 61, 64,
]
def visualization_payload(*, enabled: bool) -> dict:
    return {
        "schema_version": "0.1",
        "enabled": enabled,
        "display_mode": "auto",
        "render_step_s": 1,
        "playback_interval_ms": 50,
        "show_earth": True,
        "show_orbits": True,
        "show_links": False,
        "show_node_labels": False,
        "detail_node_threshold": 100,
        "export_gif": False,
        "gif_path": None,
        "gif_frame_step_s": 5,
    }


class ScenarioGenerationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary_directory = tempfile.TemporaryDirectory(
            prefix="satcompute-scenario-generation-"
        )
        cls.root = Path(cls.temporary_directory.name)
        cls.static_fixed_config = write_config(
            cls.root / "static-fixed.json",
            mode="static",
            delay_mode="fixed",
        )
        cls.static_distance_config = write_config(
            cls.root / "static-distance.json",
            mode="static",
            delay_mode="distance",
        )
        cls.dynamic_fixed_config = write_config(
            cls.root / "dynamic-fixed.json",
            mode="dynamic",
            delay_mode="fixed",
        )
        cls.dynamic_distance_config = write_config(
            cls.root / "dynamic-distance.json",
            mode="dynamic",
            delay_mode="distance",
        )
        cls.static_fixed_first = cls.root / "static-fixed-first"
        cls.static_fixed_second = cls.root / "static-fixed-second"
        cls.static_distance = cls.root / "static-distance"
        cls.dynamic_fixed = cls.root / "dynamic-fixed"
        cls.dynamic_distance = cls.root / "dynamic-distance"
        generate_scenario(
            cls.static_fixed_config,
            cls.static_fixed_first,
        )
        generate_scenario(
            cls.static_fixed_config,
            cls.static_fixed_second,
        )
        generate_scenario(
            cls.static_distance_config,
            cls.static_distance,
        )
        generate_scenario(
            cls.dynamic_fixed_config,
            cls.dynamic_fixed,
        )
        generate_scenario(
            cls.dynamic_distance_config,
            cls.dynamic_distance,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary_directory.cleanup()

    def test_output_shape_profile_and_manifest_are_complete(self) -> None:
        output = self.static_fixed_first
        self.assertEqual(
            sorted(path.name for path in output.iterdir()),
            ["resources", "scenario-manifest.json", "topology"],
        )
        self.assertEqual(
            sorted(path.name for path in (output / "resources").iterdir()),
            ["compute-profile.json"],
        )
        summary = check_scenario(output)
        self.assertEqual(summary["total_satellite_count"], 66)
        self.assertEqual(summary["compute_node_count"], 22)

        profile = json.loads(
            (output / "resources" / "compute-profile.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            [
                node["node_id"]
                for node in profile["compute_nodes"]
            ],
            EXPECTED_COMPUTE_NODE_IDS,
        )
        self.assertTrue(
            all(
                node["compute_rate_work_units_per_second"] == 1_500_000
                for node in profile["compute_nodes"]
            )
        )
        manifest = json.loads(
            (output / "scenario-manifest.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            manifest["compute_nodes_per_orbit"],
            [3, 4, 4, 3, 4, 4],
        )
        self.assertEqual(
            manifest["selected_compute_node_ids"],
            EXPECTED_COMPUTE_NODE_IDS,
        )
        self.assertEqual(
            manifest["aggregate_compute_rate_work_units_per_second"],
            33_000_000,
        )
        self.assertIsNone(manifest["reference_scenario_sha256"])
        self.assertIsNone(manifest["downsample_interval_s"])

    def test_static_modes_preserve_sampling_and_link_contracts(self) -> None:
        fixed_manifest = json.loads(
            (
                self.static_fixed_first
                / "topology"
                / "manifest.json"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(fixed_manifest["generation_mode"], "static")
        self.assertEqual(fixed_manifest["duration_s"], 17)
        self.assertIsNone(fixed_manifest["orbit_sample_offset_s"])
        self.assertEqual(fixed_manifest["snapshot_count"], 1)
        fixed_topology = json.loads(
            (
                self.static_fixed_first
                / "topology"
                / "topology_0s.json"
            ).read_text(encoding="utf-8")
        )
        self.assertTrue(
            all(link["delay"] == 8000 for link in fixed_topology["links"])
        )
        self.assertTrue(
            all(
                link["link_bandwidth"] == 2_000_000
                for link in fixed_topology["links"]
            )
        )

        distance_topology = json.loads(
            (
                self.static_distance
                / "topology"
                / "topology_0s.json"
            ).read_text(encoding="utf-8")
        )
        distance_delays = {
            link["delay"] for link in distance_topology["links"]
        }
        self.assertGreater(min(distance_delays), 0)
        self.assertGreater(len(distance_delays), 1)

    def test_dynamic_modes_publish_the_inclusive_schedule(self) -> None:
        for output, delay_mode in (
            (self.dynamic_fixed, "fixed"),
            (self.dynamic_distance, "distance"),
        ):
            with self.subTest(delay_mode=delay_mode):
                summary = check_scenario(output)
                self.assertEqual(summary["topology_mode"], "dynamic")
                self.assertEqual(summary["snapshot_count"], 3)
                manifest = json.loads(
                    (
                        output
                        / "topology"
                        / "manifest.json"
                    ).read_text(encoding="utf-8")
                )
                self.assertEqual(manifest["orbit_sample_offset_s"], 0)
                topology_files = sorted(
                    path.name
                    for path in (output / "topology").glob(
                        "topology_*s.json"
                    )
                )
                self.assertEqual(
                    topology_files,
                    [
                        "topology_0s.json",
                        "topology_120s.json",
                        "topology_60s.json",
                    ],
                )
                links = [
                    link
                    for filename in topology_files
                    for link in json.loads(
                        (output / "topology" / filename).read_text(
                            encoding="utf-8"
                        )
                    )["links"]
                ]
                self.assertTrue(
                    all(
                        link["link_bandwidth"] == 2_000_000
                        for link in links
                    )
                )
                if delay_mode == "fixed":
                    self.assertTrue(
                        all(link["delay"] == 8000 for link in links)
                    )
                else:
                    self.assertGreater(
                        len({link["delay"] for link in links}),
                        1,
                    )

    def test_repeated_generation_is_byte_identical(self) -> None:
        self.assertEqual(
            directory_bytes(self.static_fixed_first),
            directory_bytes(self.static_fixed_second),
        )

    def test_atomic_output_rejects_nonempty_and_removes_stale(self) -> None:
        blocked = self.root / "blocked"
        blocked.mkdir()
        marker = blocked / "keep.txt"
        marker.write_text("keep\n", encoding="utf-8")
        with self.assertRaisesRegex(
            ScenarioGenerationError,
            "refusing to overwrite non-empty",
        ):
            generate_scenario(self.static_fixed_config, blocked)
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep\n")

        output = self.root / "stale-output"
        stale = self.root / "stale-output.tmp"
        stale.mkdir()
        (stale / "partial.txt").write_text(
            "partial\n",
            encoding="utf-8",
        )
        generate_scenario(self.static_fixed_config, output)
        self.assertTrue(output.is_dir())
        self.assertFalse(stale.exists())

    def test_invalid_config_and_checker_failure_do_not_publish(self) -> None:
        invalid = preset_payload()
        invalid["topology"]["mode"] = "invalid"
        invalid_config = self.root / "invalid.json"
        invalid_config.write_text(
            json.dumps(invalid) + "\n",
            encoding="utf-8",
        )
        invalid_output = self.root / "invalid-output"
        with self.assertRaises(ValueError):
            generate_scenario(invalid_config, invalid_output)
        self.assertFalse(invalid_output.exists())

        failed_output = self.root / "checker-failed-output"
        with patch(
            "contrib.satcompute.tools.generation.scenario."
            "generate_scenario.check_scenario",
            side_effect=ValueError("checker failed"),
        ):
            with self.assertRaisesRegex(ValueError, "checker failed"):
                generate_scenario(
                    self.static_fixed_config,
                    failed_output,
                )
        self.assertFalse(failed_output.exists())
        self.assertFalse(
            (self.root / "checker-failed-output.tmp").exists()
        )

    def test_disabled_visualization_has_no_graphics_import_or_output(self) -> None:
        config_path = self.root / "visualization-disabled.json"
        config_path.write_text(
            json.dumps(visualization_payload(enabled=False)) + "\n",
            encoding="utf-8",
        )
        original_import = builtins.__import__

        def guarded_import(name, *args, **kwargs):
            if name == "PIL" or name.startswith(("PIL.", "matplotlib")):
                raise AssertionError(f"disabled visualization imported {name}")
            return original_import(name, *args, **kwargs)

        output = self.root / "visualization-disabled-output"
        with patch("builtins.__import__", side_effect=guarded_import):
            launched = _run_optional_visualization(
                self.static_fixed_first,
                config_path,
            )
            generate_scenario(
                self.static_fixed_config,
                output,
                config_path,
            )
        self.assertFalse(launched)
        self.assertEqual(
            sorted(path.name for path in output.iterdir()),
            ["resources", "scenario-manifest.json", "topology"],
        )
        self.assertEqual(
            directory_bytes(output),
            directory_bytes(self.static_fixed_first),
        )

    def test_visualization_runs_only_after_atomic_publication(self) -> None:
        config_path = self.root / "visualization-enabled.json"
        config_path.write_text(
            json.dumps(visualization_payload(enabled=True)) + "\n",
            encoding="utf-8",
        )
        output = self.root / "visualization-enabled-output"

        def verify_published(output_dir, supplied_config):
            self.assertEqual(output_dir, output.resolve())
            self.assertEqual(supplied_config, config_path.resolve())
            self.assertTrue((output / "scenario-manifest.json").is_file())
            check_scenario(output)
            return True

        with patch(
            "contrib.satcompute.tools.generation.scenario."
            "generate_scenario._run_optional_visualization",
            side_effect=verify_published,
        ) as launch:
            generate_scenario(
                self.static_fixed_config,
                output,
                config_path,
            )
        launch.assert_called_once()

    def test_visualization_failure_does_not_rollback_scenario(self) -> None:
        output = self.root / "visualization-failed-output"
        with patch(
            "contrib.satcompute.tools.generation.scenario."
            "generate_scenario._run_optional_visualization",
            side_effect=RuntimeError("display unavailable"),
        ):
            with self.assertRaisesRegex(
                ScenarioVisualizationError,
                "scenario was published successfully",
            ):
                generate_scenario(
                    self.static_fixed_config,
                    output,
                    self.root / "unused-visualization.json",
                )
        self.assertTrue((output / "scenario-manifest.json").is_file())
        check_scenario(output)


if __name__ == "__main__":
    unittest.main()
