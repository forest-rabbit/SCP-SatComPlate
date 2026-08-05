#!/usr/bin/env python3
"""Tests for unweighted shortest-hop ECMP candidate analysis."""

from __future__ import annotations

import unittest

from contrib.satcompute.tools.analysis.topology_interval.ecmp_candidates import (
    RouteState,
    compare_ecmp_traces,
    compute_ecmp_matrix,
    ecmp_matrix_sha256,
)
from contrib.satcompute.tools.analysis.topology_interval.edge_state import (
    EdgeTrace,
)


class EcmpCandidateTest(unittest.TestCase):
    def test_diamond_has_two_sorted_equal_cost_next_hops(self) -> None:
        diamond = frozenset(((0, 1), (0, 2), (1, 3), (2, 3)))
        matrix = compute_ecmp_matrix(4, diamond)
        self.assertEqual(
            matrix.route(0, 3),
            RouteState(True, 2, (1, 2)),
        )
        self.assertEqual(
            matrix.route(3, 0),
            RouteState(True, 2, (1, 2)),
        )
        self.assertEqual(
            matrix.route(0, 1),
            RouteState(True, 1, (1,)),
        )

    def test_unreachable_state_is_not_a_normal_candidate_match(self) -> None:
        empty = frozenset()
        trace = EdgeTrace(2, ((0, empty),))
        metrics = compare_ecmp_traces(trace, trace)
        self.assertEqual(metrics["ordered_pair_seconds"], 2)
        self.assertEqual(metrics["both_unreachable_pair_seconds"], 2)
        self.assertEqual(metrics["candidate_comparable_pair_seconds"], 0)
        self.assertIsNone(metrics["exact_candidate_match_ratio"])
        self.assertIsNone(metrics["mean_candidate_jaccard"])

    def test_held_diamond_detects_reference_candidate_changes(self) -> None:
        diamond = frozenset(((0, 1), (0, 2), (1, 3), (2, 3)))
        path = frozenset(((0, 1), (1, 3), (2, 3)))
        reference = EdgeTrace(
            4,
            ((0, diamond), (1, path), (2, diamond)),
        )
        held = EdgeTrace(4, ((0, diamond), (2, diamond)))
        metrics = compare_ecmp_traces(reference, held)
        self.assertGreater(metrics["candidate_mismatch_pair_seconds"], 0)
        self.assertGreater(
            metrics["candidate_count_mismatch_pair_seconds"],
            0,
        )
        self.assertLess(metrics["exact_candidate_match_ratio"], 1.0)
        self.assertLess(metrics["minimum_candidate_jaccard"], 1.0)
        self.assertEqual(
            metrics["reference_unique_ecmp_candidate_fingerprint_count"],
            2,
        )
        self.assertEqual(
            metrics["held_unique_ecmp_candidate_fingerprint_count"],
            1,
        )

    def test_matrix_fingerprint_is_deterministic(self) -> None:
        edges = frozenset(((0, 1), (1, 2)))
        first = compute_ecmp_matrix(3, edges)
        second = compute_ecmp_matrix(3, edges)
        self.assertEqual(ecmp_matrix_sha256(first), ecmp_matrix_sha256(second))


if __name__ == "__main__":
    unittest.main()
