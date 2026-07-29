#!/usr/bin/env python3
"""Positive and negative tests for the standalone export checker."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import Any

from contrib.satcompute.tools.generation.topology.check_export import (
    ExportCheckError,
    check_export,
)
from contrib.satcompute.tools.generation.topology.common.hash_utils import (
    aggregate_data_sha256,
    compact_json_bytes,
)
from contrib.satcompute.tools.generation.topology.common.satcompute_schema import (
    DELAY_ROUNDING,
    SPEED_OF_LIGHT_M_PER_S,
    satcompute_json_bytes,
)


def _write_json(path: Path, payload: Any) -> None:
    path.write_bytes(satcompute_json_bytes(payload))


def _read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _write_valid_export(
    root: Path,
    *,
    times: tuple[int, ...] = (0,),
    delay_mode: str = "fixed",
) -> None:
    root.mkdir()
    data_names = []
    for time_s in times:
        nodes_name = f"nodes_{time_s}s.json"
        topology_name = f"topology_{time_s}s.json"
        _write_json(
            root / nodes_name,
            {
                "nodes": [
                    {"node_id": 0, "node_type": "sat"},
                    {"node_id": 1, "node_type": "sat"},
                ]
            },
        )
        _write_json(
            root / topology_name,
            {
                "links": [
                    {
                        "node1_id": 0,
                        "node2_id": 1,
                        "type": "sat",
                        "delay": 8000 if delay_mode == "fixed" else 1000 + time_s,
                        "link_bandwidth": 10000000,
                    }
                ]
            },
        )
        data_names.extend((nodes_name, topology_name))
    dynamic = len(times) > 1
    manifest = {
        "schema_version": "0.1",
        "generator": "satcompute-topology-export",
        "generation_mode": "dynamic" if dynamic else "static",
        "satcompute_commit": "0" * 40,
        "satcompute_worktree_clean": True,
        "hypatia_repository": "https://github.com/snkas/hypatia.git",
        "hypatia_commit": "1" * 40,
        "hypatia_integration_mode": "vendored-minimal",
        "python_version": "3.10.12",
        "uv_version": "0.11.25",
        "uv_lock_sha256": "2" * 64,
        "physical_config": {"constellation_name": "fixture"},
        "isl_candidate_strategy": "plus-grid",
        "seam_enabled": False,
        "candidate_count": 1,
        "candidate_degree_profile": {
            "minimum_total_degree": 1,
            "maximum_total_degree": 1,
            "boundary_plane_total_degree": 1,
            "internal_plane_total_degree": 1,
        },
        "source_dynamic_manifest_sha256": "3" * 64 if dynamic else None,
        "source_dynamic_aggregate_sha256": "4" * 64 if dynamic else None,
        "duration_s": times[-1],
        "step_s": times[1] - times[0] if dynamic else None,
        "snapshot_count": len(times),
        "first_time_s": times[0],
        "last_time_s": times[-1],
        "node_count": 2,
        "min_active_count": 1,
        "max_active_count": 1,
        "average_active_count": 1.0,
        "delay_mode": delay_mode,
        "fixed_delay_us": 8000 if delay_mode == "fixed" else None,
        "speed_of_light_m_per_s": SPEED_OF_LIGHT_M_PER_S,
        "delay_rounding": DELAY_ROUNDING,
        "link_bandwidth_kbps": 10000000,
        "aggregate_data_sha256": aggregate_data_sha256(root, data_names),
    }
    (root / "manifest.json").write_bytes(compact_json_bytes(manifest))


class ExportCheckerPositiveTest(unittest.TestCase):
    def test_static_and_dynamic_exports_pass(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-check-positive-"
        ) as temp:
            root = Path(temp)
            static = root / "static"
            dynamic = root / "dynamic"
            _write_valid_export(static)
            _write_valid_export(
                dynamic,
                times=(0, 60, 120),
                delay_mode="distance",
            )
            static_summary = check_export(
                static,
                expected_node_count=2,
            )
            dynamic_summary = check_export(dynamic)
            self.assertEqual(static_summary["snapshot_count"], 1)
            self.assertEqual(dynamic_summary["snapshot_count"], 3)
            self.assertEqual(dynamic_summary["node_count"], 2)

    def test_expected_node_count_is_enforced(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-check-count-"
        ) as temp:
            output = Path(temp) / "output"
            _write_valid_export(output)
            with self.assertRaisesRegex(ExportCheckError, "expected 3 nodes"):
                check_export(output, expected_node_count=3)


class ExportCheckerFilesTest(unittest.TestCase):
    def _invalid_output(self) -> tuple[tempfile.TemporaryDirectory, Path]:
        temporary = tempfile.TemporaryDirectory(
            prefix="satcompute-check-files-"
        )
        output = Path(temporary.name) / "output"
        _write_valid_export(output)
        return temporary, output

    def test_missing_zero_and_missing_pair_are_rejected(self) -> None:
        temporary, output = self._invalid_output()
        with temporary:
            (output / "nodes_0s.json").rename(output / "nodes_60s.json")
            (output / "topology_0s.json").rename(
                output / "topology_60s.json"
            )
            with self.assertRaisesRegex(ExportCheckError, "must exist"):
                check_export(output)

        temporary, output = self._invalid_output()
        with temporary:
            (output / "topology_0s.json").unlink()
            with self.assertRaisesRegex(ExportCheckError, "paired"):
                check_export(output)

    def test_malformed_and_extra_files_are_rejected(self) -> None:
        temporary, output = self._invalid_output()
        with temporary:
            (output / "nodes_bad.json").write_text("{}\n", encoding="utf-8")
            with self.assertRaisesRegex(ExportCheckError, "malformed"):
                check_export(output)

        temporary, output = self._invalid_output()
        with temporary:
            (output / "extra.txt").write_text("extra\n", encoding="utf-8")
            with self.assertRaisesRegex(ExportCheckError, "unexpected"):
                check_export(output)


class ExportCheckerNodesTest(unittest.TestCase):
    def test_duplicate_nodes_and_node_set_changes_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-check-node-duplicate-"
        ) as temp:
            output = Path(temp) / "output"
            _write_valid_export(output)
            _write_json(
                output / "nodes_0s.json",
                {
                    "nodes": [
                        {"node_id": 0, "node_type": "sat"},
                        {"node_id": 0, "node_type": "sat"},
                    ]
                },
            )
            with self.assertRaises(ValueError):
                check_export(output)

        with tempfile.TemporaryDirectory(
            prefix="satcompute-check-node-change-"
        ) as temp:
            output = Path(temp) / "output"
            _write_valid_export(output, times=(0, 60))
            _write_json(
                output / "nodes_60s.json",
                {
                    "nodes": [
                        {"node_id": 0, "node_type": "sat"},
                        {"node_id": 1, "node_type": "sat"},
                        {"node_id": 2, "node_type": "sat"},
                    ]
                },
            )
            with self.assertRaisesRegex(ExportCheckError, "node set changed"):
                check_export(output)


class ExportCheckerLinksTest(unittest.TestCase):
    def _assert_link_rejected(self, links: list[dict[str, Any]]) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-check-link-"
        ) as temp:
            output = Path(temp) / "output"
            _write_valid_export(output)
            _write_json(output / "topology_0s.json", {"links": links})
            with self.assertRaises(ValueError):
                check_export(output)

    def test_endpoint_and_canonical_failures_are_rejected(self) -> None:
        base = {
            "node1_id": 0,
            "node2_id": 1,
            "type": "sat",
            "delay": 8000,
            "link_bandwidth": 10000000,
        }
        self._assert_link_rejected([{**base, "node2_id": 2}])
        self._assert_link_rejected(
            [{**base, "node1_id": 1, "node2_id": 0}]
        )
        self._assert_link_rejected(
            [{**base, "node1_id": 0, "node2_id": 0}]
        )
        self._assert_link_rejected([base, dict(base)])

    def test_type_delay_and_bandwidth_failures_are_rejected(self) -> None:
        base = {
            "node1_id": 0,
            "node2_id": 1,
            "type": "sat",
            "delay": 8000,
            "link_bandwidth": 10000000,
        }
        self._assert_link_rejected([{**base, "type": "ground"}])
        self._assert_link_rejected([{**base, "delay": -1}])
        self._assert_link_rejected([{**base, "link_bandwidth": 0}])


class ExportCheckerManifestTest(unittest.TestCase):
    def test_manifest_count_and_aggregate_failures_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-check-manifest-count-"
        ) as temp:
            output = Path(temp) / "output"
            _write_valid_export(output)
            manifest = _read_json(output / "manifest.json")
            manifest["snapshot_count"] = 2
            (output / "manifest.json").write_bytes(
                compact_json_bytes(manifest)
            )
            with self.assertRaisesRegex(
                ExportCheckError,
                "snapshot_count",
            ):
                check_export(output)

        with tempfile.TemporaryDirectory(
            prefix="satcompute-check-manifest-hash-"
        ) as temp:
            output = Path(temp) / "output"
            _write_valid_export(output)
            manifest = _read_json(output / "manifest.json")
            manifest["aggregate_data_sha256"] = "f" * 64
            (output / "manifest.json").write_bytes(
                compact_json_bytes(manifest)
            )
            with self.assertRaisesRegex(
                ExportCheckError,
                "aggregate_data_sha256",
            ):
                check_export(output)

    def test_fixed_delay_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-check-fixed-delay-"
        ) as temp:
            output = Path(temp) / "output"
            _write_valid_export(output)
            topology = _read_json(output / "topology_0s.json")
            topology["links"][0]["delay"] = 9000
            _write_json(output / "topology_0s.json", topology)
            with self.assertRaisesRegex(ExportCheckError, "fixed snapshot"):
                check_export(output)


if __name__ == "__main__":
    unittest.main()
