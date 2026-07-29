#!/usr/bin/env python3
"""Tests for interval recommendations and deterministic report output."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tools.analysis.topology_interval.report import (
    ROUTING_MODES,
    finalize_records_and_recommendations,
    summarize_cost_runs,
    write_report_bundle,
)


def records_with_failures(
    failures: set[tuple[int, int]],
) -> list[dict]:
    records = []
    for window_index, offset_s in enumerate((0, 2000, 4000)):
        for interval_s in (1, 2, 5, 10, 20):
            passes = (window_index, interval_s) not in failures
            for mode in ROUTING_MODES:
                records.append(
                    {
                        "constellation": "fixture",
                        "node_count": 4,
                        "candidate_count": 4,
                        "window_index": window_index,
                        "window_offset_s": offset_s,
                        "interval_s": interval_s,
                        "snapshot_count": 21,
                        "reference_unique_edge_set_count": 1,
                        "reference_edge_transition_count": 0,
                        "absolute_edge_state_errors": 0,
                        "normalized_edge_state_disagreement": 0.0,
                        "missed_active_edges": 0,
                        "spurious_active_edges": 0,
                        "missed_transition_count": 0,
                        "max_event_timing_error_s": 0,
                        "component_count_mismatch_seconds": 0,
                        "unreachable_pair_error_sum": 0,
                        "reference_unique_ecmp_candidate_fingerprint_count":
                            1,
                        "exact_candidate_match_ratio":
                            1.0 if passes else 0.9,
                        "candidate_mismatch_pair_seconds":
                            0 if passes else 1,
                        "mean_candidate_jaccard": 1.0,
                        "minimum_candidate_jaccard": 1.0,
                        "routing_mode": mode,
                        "reference_selected_next_hop_change_count": 0,
                        "selected_next_hop_exact_match_ratio": 1.0,
                        "selected_next_hop_mismatch_count": 0,
                        "selected_candidate_survival_ratio": 1.0,
                        "selected_candidate_missing_count": 0,
                        "output_bytes": 100,
                        "checker_wall_time_s": 0.1,
                        "topology_wall_time_min_s": 0.2,
                        "topology_wall_time_median_s": 0.3,
                        "topology_wall_time_max_s": 0.4,
                        "topology_peak_rss_kib": 1000,
                        "route_recomputation_count": 20,
                    }
                )
    return records


class IntervalReportTest(unittest.TestCase):
    def test_largest_passing_and_robust_minimum_are_selected(self) -> None:
        failures = {
            (0, 20),
            (1, 10),
            (1, 20),
            (2, 5),
            (2, 10),
            (2, 20),
        }
        _, recommendations = finalize_records_and_recommendations(
            records_with_failures(failures)
        )
        decision = recommendations["constellations"][0]
        self.assertEqual(
            decision["main_window_recommended_interval_s"],
            10,
        )
        self.assertEqual(decision["robust_recommended_interval_s"], 2)
        self.assertTrue(decision["upper_bound_identified"])

    def test_all_equivalent_returns_20_without_an_upper_bound(self) -> None:
        records, recommendations = finalize_records_and_recommendations(
            records_with_failures(set())
        )
        decision = recommendations["constellations"][0]
        self.assertEqual(decision["robust_recommended_interval_s"], 20)
        self.assertFalse(decision["upper_bound_identified"])
        self.assertTrue(
            all(
                record["all_tested_intervals_fidelity_equivalent"]
                for record in records
            )
        )

    def test_no_passing_interval_falls_back_to_one(self) -> None:
        failures = {
            (window_index, interval_s)
            for window_index in range(3)
            for interval_s in (1, 2, 5, 10, 20)
        }
        _, recommendations = finalize_records_and_recommendations(
            records_with_failures(failures)
        )
        decision = recommendations["constellations"][0]
        self.assertEqual(decision["robust_recommended_interval_s"], 1)

    def test_cost_summary_and_report_bytes_are_deterministic(self) -> None:
        self.assertEqual(
            summarize_cost_runs((3.0, 1.0, 2.0), (100, None, 120)),
            {
                "topology_wall_time_min_s": 1.0,
                "topology_wall_time_median_s": 2.0,
                "topology_wall_time_max_s": 3.0,
                "topology_peak_rss_kib": 120,
            },
        )
        metadata = {
            "window_count": 3,
            "topology_cost_repeats": 3,
        }
        probes = {
            "schema_version": "0.1",
            "constellations": [],
        }
        with tempfile.TemporaryDirectory(
            prefix="satcompute-interval-report-"
        ) as temp:
            root = Path(temp)
            write_report_bundle(
                root,
                metadata,
                records_with_failures(set()),
                probes,
            )
            first = {
                path.name: path.read_bytes() for path in root.iterdir()
            }
            write_report_bundle(
                root,
                metadata,
                records_with_failures(set()),
                probes,
            )
            second = {
                path.name: path.read_bytes() for path in root.iterdir()
            }
            self.assertEqual(first, second)
            self.assertEqual(
                sorted(first),
                [
                    "REPORT.md",
                    "interval-results.csv",
                    "interval-results.json",
                    "probe-pairs.json",
                    "recommendations.json",
                ],
            )


if __name__ == "__main__":
    unittest.main()
