#!/usr/bin/env python3
"""Tests for edge-state, transition, and connectivity metrics."""

from __future__ import annotations

import unittest

from contrib.satcompute.tools.analysis.topology_interval.edge_state import (
    EdgeStateError,
    EdgeTrace,
    compare_edge_traces,
    connectivity_state,
    edge_set_sha256,
)


class EdgeStateTest(unittest.TestCase):
    def test_transient_and_delayed_changes_are_distinguished(self) -> None:
        base = frozenset(((0, 1), (1, 2), (2, 3)))
        split = frozenset(((0, 1), (2, 3)))
        added = base | {(0, 2)}
        reference = EdgeTrace(
            4,
            (
                (0, base),
                (1, split),
                (2, base),
                (3, added),
                (4, added),
            ),
        )
        held = EdgeTrace(
            4,
            (
                (0, base),
                (2, base),
                (4, added),
            ),
        )
        metrics = compare_edge_traces(reference, held)
        self.assertEqual(metrics["absolute_edge_state_errors"], 2)
        self.assertAlmostEqual(
            metrics["normalized_edge_state_disagreement"],
            2 / 17,
        )
        self.assertEqual(metrics["endpoint_mismatch_seconds"], 2)
        self.assertEqual(metrics["missed_active_edges"], 1)
        self.assertEqual(metrics["spurious_active_edges"], 1)
        self.assertEqual(metrics["missed_transition_count"], 2)
        self.assertEqual(metrics["mean_event_timing_error_s"], 1.0)
        self.assertEqual(metrics["p95_event_timing_error_s"], 1)
        self.assertEqual(metrics["max_event_timing_error_s"], 1)
        self.assertEqual(metrics["maximum_stale_duration_s"], 1)
        self.assertEqual(
            metrics["component_count_mismatch_seconds"],
            1,
        )
        self.assertEqual(metrics["unreachable_pair_error_sum"], 4)
        self.assertEqual(metrics["max_unreachable_pair_error"], 4)
        self.assertEqual(
            metrics["reference_unique_edge_set_count"],
            3,
        )
        self.assertEqual(metrics["held_unique_edge_set_count"], 2)

    def test_identical_one_second_trace_has_zero_error(self) -> None:
        edges = frozenset(((0, 1), (1, 2)))
        trace = EdgeTrace(3, ((0, edges), (1, edges)))
        metrics = compare_edge_traces(trace, trace)
        for field in (
            "absolute_edge_state_errors",
            "endpoint_mismatch_seconds",
            "missed_active_edges",
            "spurious_active_edges",
            "missed_transition_count",
            "maximum_stale_duration_s",
            "component_count_mismatch_seconds",
            "unreachable_pair_error_sum",
            "max_unreachable_pair_error",
        ):
            self.assertEqual(metrics[field], 0)

    def test_connectivity_includes_isolated_nodes(self) -> None:
        state = connectivity_state(5, frozenset(((0, 1), (1, 2))))
        self.assertEqual(state.connected_component_count, 3)
        self.assertEqual(state.largest_component_size, 3)
        self.assertEqual(state.unreachable_unordered_pairs, 7)

    def test_fingerprint_and_trace_contracts_are_strict(self) -> None:
        self.assertEqual(
            edge_set_sha256(((1, 2), (0, 1))),
            edge_set_sha256(((0, 1), (1, 2))),
        )
        with self.assertRaises(EdgeStateError):
            EdgeTrace(3, ((1, frozenset()),))
        with self.assertRaises(EdgeStateError):
            EdgeTrace(3, ((0, frozenset(((1, 1),))),))


if __name__ == "__main__":
    unittest.main()
