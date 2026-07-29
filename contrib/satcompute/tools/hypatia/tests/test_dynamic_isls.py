#!/usr/bin/env python3
"""Tests for deterministic plus-grid dynamic ISL contracts."""

from __future__ import annotations

import hashlib
import json
import math
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from configuration import load_config  # noqa: E402
from dynamic_isls import (  # noqa: E402
    ACTIVE,
    INTER_PLANE,
    INTRA_PLANE,
    OVER_MAX_DISTANCE,
    CandidateIsl,
    DynamicIslError,
    EvaluatedIsl,
    IslEdge,
    build_isl_snapshot,
    build_candidate_isls,
    clearance_limited_max_distance_m,
    evaluate_candidate_isls,
    generate_isl_snapshots,
    validate_clearance_limit,
)
from hypatia_adapter import HypatiaAdapter  # noqa: E402
from generate_dynamic_isls import (  # noqa: E402
    CANDIDATE_FILENAME,
    MANIFEST_FILENAME,
    SNAPSHOT_FILENAME,
    DynamicIslGenerationError,
    generate_dynamic_isl_output,
    validate_schedule,
)
from orbit_positions import (  # noqa: E402
    SatellitePosition,
    load_orbit_constellation,
)


PRESET = TOOL_DIR / "config" / "synthetic-66.json"


class DynamicIslCandidateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.config = load_config(PRESET)

    def test_synthetic_66_candidate_counts_and_seam(self) -> None:
        without_seam = build_candidate_isls(self.config)
        with_seam = build_candidate_isls(
            replace(self.config, seam_enabled=True)
        )
        self.assertEqual(len(without_seam), 121)
        self.assertEqual(len(with_seam), 132)
        self.assertEqual(
            sum(candidate.kind == INTRA_PLANE for candidate in without_seam),
            66,
        )
        self.assertEqual(
            sum(candidate.kind == INTER_PLANE for candidate in without_seam),
            55,
        )
        self.assertEqual(
            sum(candidate.kind == INTER_PLANE for candidate in with_seam),
            66,
        )
        self.assertEqual(
            build_candidate_isls(self.config),
            without_seam,
        )

    def test_small_constellations_have_no_invalid_or_duplicate_edges(self) -> None:
        cases = (
            (1, 1, False),
            (1, 2, False),
            (1, 3, False),
            (2, 1, False),
            (2, 1, True),
            (2, 2, True),
        )
        for num_orbits, satellites_per_orbit, seam_enabled in cases:
            with self.subTest(
                num_orbits=num_orbits,
                satellites_per_orbit=satellites_per_orbit,
                seam_enabled=seam_enabled,
            ):
                config = replace(
                    self.config,
                    num_orbits=num_orbits,
                    satellites_per_orbit=satellites_per_orbit,
                    seam_enabled=seam_enabled,
                )
                candidates = build_candidate_isls(config)
                edges = tuple(candidate.edge for candidate in candidates)
                self.assertEqual(len(edges), len(set(edges)))
                self.assertEqual(edges, tuple(sorted(edges)))
                for edge in edges:
                    self.assertLess(edge.node1_id, edge.node2_id)
                    self.assertGreaterEqual(edge.node1_id, 0)
                    self.assertLess(
                        edge.node2_id,
                        config.expected_satellite_count,
                    )

    def test_edge_constructor_rejects_invalid_endpoints(self) -> None:
        edge = IslEdge.canonical(3, 1, 4)
        self.assertEqual((edge.node1_id, edge.node2_id), (1, 3))
        invalid_arguments = (
            (-1, 1, 4),
            (1, 1, 4),
            (1, 4, 4),
            (True, 1, 4),
        )
        for arguments in invalid_arguments:
            with self.subTest(arguments=arguments):
                with self.assertRaises(DynamicIslError):
                    IslEdge.canonical(*arguments)

    def test_clearance_limit_is_general_and_enforced(self) -> None:
        exact_limit = clearance_limited_max_distance_m(780.0)
        self.assertAlmostEqual(exact_limit, 6174589.541014042)
        self.assertEqual(validate_clearance_limit(self.config), 6174589)
        with self.assertRaisesRegex(
            DynamicIslError,
            "exceeds.*clearance limit",
        ):
            validate_clearance_limit(
                replace(self.config, max_isl_distance_m=6174590)
            )

        self.assertEqual(clearance_limited_max_distance_m(80.0), 0.0)
        self.assertGreater(
            clearance_limited_max_distance_m(1200.0),
            exact_limit,
        )
        for altitude in (-1.0, math.nan, math.inf, True):
            with self.subTest(altitude=altitude):
                with self.assertRaises(DynamicIslError):
                    clearance_limited_max_distance_m(altitude)

    def test_range_filter_includes_the_exact_threshold(self) -> None:
        candidates = tuple(
            CandidateIsl(
                IslEdge.canonical(0, node_id, 4),
                INTER_PLANE,
            )
            for node_id in (1, 2, 3)
        )
        positions = (
            SatellitePosition(0, 0.0, 0.0, 0.0),
            SatellitePosition(1, 9.0, 0.0, 0.0),
            SatellitePosition(2, 10.0, 0.0, 0.0),
            SatellitePosition(3, 11.0, 0.0, 0.0),
        )
        evaluated = evaluate_candidate_isls(candidates, positions, 10.0)
        self.assertEqual(
            tuple(item.reason for item in evaluated),
            (ACTIVE, ACTIVE, OVER_MAX_DISTANCE),
        )
        self.assertEqual(
            tuple(item.distance_m for item in evaluated),
            (9.0, 10.0, 11.0),
        )

    def test_transition_sets_are_exact_and_disjoint(self) -> None:
        edges = tuple(
            IslEdge.canonical(node1, node2, 4)
            for node1, node2 in ((0, 1), (0, 2), (0, 3))
        )

        def evaluated(active_indices: set[int]) -> tuple[EvaluatedIsl, ...]:
            return tuple(
                EvaluatedIsl(
                    edge=edge,
                    distance_m=float(index + 1),
                    active=index in active_indices,
                    reason=(
                        ACTIVE
                        if index in active_indices
                        else OVER_MAX_DISTANCE
                    ),
                )
                for index, edge in enumerate(edges)
            )

        initial = build_isl_snapshot(0, evaluated({0, 1}), None)
        self.assertEqual(initial.added_edges, ())
        self.assertEqual(initial.removed_edges, ())
        next_snapshot = build_isl_snapshot(
            60,
            evaluated({1, 2}),
            tuple(item.edge for item in initial.active_edges),
        )
        self.assertEqual(next_snapshot.added_edges, (edges[2],))
        self.assertEqual(next_snapshot.removed_edges, (edges[0],))
        self.assertFalse(
            set(next_snapshot.added_edges)
            & set(next_snapshot.removed_edges)
        )

    def test_real_snapshots_are_deterministic_and_consistent(self) -> None:
        candidates = build_candidate_isls(self.config)
        first_orbit = load_orbit_constellation(
            self.config,
            HypatiaAdapter(),
        )
        second_orbit = load_orbit_constellation(
            self.config,
            HypatiaAdapter(),
        )
        first = generate_isl_snapshots(
            first_orbit,
            candidates,
            (0, 60, 120),
            self.config.max_isl_distance_m,
        )
        second = generate_isl_snapshots(
            second_orbit,
            candidates,
            (0, 60, 120),
            self.config.max_isl_distance_m,
        )
        self.assertEqual(first, second)
        for snapshot in first:
            self.assertEqual(snapshot.candidate_count, 121)
            self.assertEqual(
                snapshot.candidate_count,
                len(snapshot.active_edges) + len(snapshot.filtered_edges),
            )


class DynamicIslOutputTest(unittest.TestCase):
    def test_schedule_contract_is_inclusive_and_strict(self) -> None:
        self.assertEqual(validate_schedule(120, 60), (0, 60, 120))
        self.assertEqual(validate_schedule(0, 1), (0,))
        for duration_s, step_s in (
            (-1, 1),
            (1, 0),
            (1, -1),
            (100, 60),
            (True, 1),
            (1, True),
        ):
            with self.subTest(duration_s=duration_s, step_s=step_s):
                with self.assertRaises(DynamicIslGenerationError):
                    validate_schedule(duration_s, step_s)

    def test_output_files_counts_and_hashes_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-dynamic-output-"
        ) as temp:
            root = Path(temp)
            first_dir = root / "first"
            second_dir = root / "second"
            first = generate_dynamic_isl_output(
                PRESET,
                120,
                60,
                first_dir,
            )
            second = generate_dynamic_isl_output(
                PRESET,
                120,
                60,
                second_dir,
            )
            self.assertEqual(first, second)
            expected_names = [
                CANDIDATE_FILENAME,
                SNAPSHOT_FILENAME,
                MANIFEST_FILENAME,
            ]
            self.assertEqual(
                sorted(path.name for path in first_dir.iterdir()),
                expected_names,
            )
            for filename in expected_names:
                self.assertEqual(
                    (first_dir / filename).read_bytes(),
                    (second_dir / filename).read_bytes(),
                )

            candidate_bytes = (
                first_dir / CANDIDATE_FILENAME
            ).read_bytes()
            snapshot_bytes = (
                first_dir / SNAPSHOT_FILENAME
            ).read_bytes()
            candidate = json.loads(candidate_bytes)
            snapshots = [
                json.loads(line)
                for line in snapshot_bytes.decode("utf-8").splitlines()
            ]
            manifest = json.loads(
                (first_dir / MANIFEST_FILENAME).read_text(encoding="utf-8")
            )
            self.assertEqual(candidate["candidate_count"], 121)
            self.assertEqual(len(candidate["candidate_isls"]), 121)
            self.assertEqual(
                [snapshot["time_s"] for snapshot in snapshots],
                [0, 60, 120],
            )
            for snapshot in snapshots:
                self.assertEqual(
                    snapshot["candidate_count"],
                    snapshot["active_count"] + snapshot["filtered_count"],
                )
            self.assertEqual(snapshots[0]["added_edges"], [])
            self.assertEqual(snapshots[0]["removed_edges"], [])
            self.assertEqual(manifest, first)
            self.assertEqual(
                manifest["candidate_file_sha256"],
                hashlib.sha256(candidate_bytes).hexdigest(),
            )
            self.assertEqual(
                manifest["snapshot_jsonl_sha256"],
                hashlib.sha256(snapshot_bytes).hexdigest(),
            )
            self.assertEqual(
                manifest["aggregate_sha256"],
                hashlib.sha256(candidate_bytes + snapshot_bytes).hexdigest(),
            )

    def test_atomic_output_rejects_nonempty_and_cleans_stale_temp(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-dynamic-atomic-"
        ) as temp:
            root = Path(temp)
            blocked = root / "blocked"
            blocked.mkdir()
            marker = blocked / "keep.txt"
            marker.write_text("keep\n", encoding="utf-8")
            with self.assertRaisesRegex(
                DynamicIslGenerationError,
                "refusing to overwrite non-empty",
            ):
                generate_dynamic_isl_output(PRESET, 0, 1, blocked)
            self.assertEqual(marker.read_text(encoding="utf-8"), "keep\n")
            self.assertFalse((root / "blocked.tmp").exists())

            output = root / "output"
            stale = root / "output.tmp"
            stale.mkdir()
            (stale / "partial.txt").write_text(
                "partial\n",
                encoding="utf-8",
            )
            generate_dynamic_isl_output(PRESET, 0, 1, output)
            self.assertTrue(output.is_dir())
            self.assertFalse(stale.exists())
            with self.assertRaisesRegex(
                DynamicIslGenerationError,
                "refusing to overwrite non-empty",
            ):
                generate_dynamic_isl_output(PRESET, 0, 1, output)

            empty_output = root / "empty-output"
            empty_output.mkdir()
            generate_dynamic_isl_output(PRESET, 0, 1, empty_output)
            self.assertEqual(
                sorted(path.name for path in empty_output.iterdir()),
                [
                    CANDIDATE_FILENAME,
                    SNAPSHOT_FILENAME,
                    MANIFEST_FILENAME,
                ],
            )


if __name__ == "__main__":
    unittest.main()
