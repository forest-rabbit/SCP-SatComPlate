#!/usr/bin/env python3
"""Tests for deterministic plus-grid dynamic ISL contracts."""

from __future__ import annotations

import math
import sys
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


if __name__ == "__main__":
    unittest.main()
