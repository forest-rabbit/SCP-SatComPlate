#!/usr/bin/env python3
"""Tests for the Python/C++ candidate consistency gate."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tools.analysis.topology_interval.ecmp_candidates import (
    compute_ecmp_matrix,
)
from contrib.satcompute.tools.analysis.topology_interval.edge_state import (
    EdgeTrace,
)
from contrib.satcompute.tools.analysis.topology_interval.route_probe import (
    ProbePair,
    RouteProbeError,
    SelectionAuditRow,
    build_probe_pairs,
    compare_selection_audits,
    count_selected_next_hop_changes,
    verify_cpp_candidate_audit,
)


def write_audit(
    path: Path,
    trace: EdgeTrace,
    *,
    tamper: bool = False,
) -> None:
    lines = []
    for time_s, edges in trace.snapshots:
        matrix = compute_ecmp_matrix(trace.node_count, edges)
        for source_id in range(trace.node_count):
            for destination_id in range(trace.node_count):
                if source_id == destination_id:
                    continue
                state = matrix.route(source_id, destination_id)
                candidates = list(state.candidate_next_hops)
                if tamper and (source_id, destination_id) == (0, 3):
                    candidates = candidates[:1]
                lines.append(
                    json.dumps(
                        {
                            "time_s": time_s,
                            "source_id": source_id,
                            "destination_id": destination_id,
                            "reachable": bool(candidates),
                            "candidate_next_hop_ids": candidates,
                        },
                        separators=(",", ":"),
                    )
                )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


class RouteProbeTest(unittest.TestCase):
    def test_probe_pairs_are_stratified_unique_and_deterministic(self) -> None:
        compute_ids = (
            1, 5, 9, 12, 15, 17, 20, 23, 26, 28, 31,
            34, 38, 42, 45, 48, 50, 53, 56, 59, 61, 64,
        )
        first = build_probe_pairs(6, 11, compute_ids)
        second = build_probe_pairs(6, 11, compute_ids)
        self.assertEqual(first, second)
        self.assertEqual(len(first), 64)
        self.assertEqual(
            {pair.category for pair in first},
            {
                "same-orbit-adjacent",
                "same-orbit-far",
                "adjacent-orbit",
                "non-adjacent-orbit",
                "boundary-to-internal",
                "compute-to-compute",
                "compute-to-normal",
                "normal-to-compute",
            },
        )
        self.assertEqual(
            len({(pair.source_id, pair.destination_id) for pair in first}),
            len(first),
        )
        self.assertEqual(
            len({(pair.source_port, pair.destination_port) for pair in first}),
            len(first),
        )

    def test_exact_audit_passes_and_candidate_change_fails(self) -> None:
        diamond = frozenset(((0, 1), (0, 2), (1, 3), (2, 3)))
        trace = EdgeTrace(4, ((0, diamond),))
        with tempfile.TemporaryDirectory(
            prefix="satcompute-route-probe-"
        ) as temp:
            root = Path(temp)
            valid = root / "valid.jsonl"
            write_audit(valid, trace)
            result = verify_cpp_candidate_audit(trace, valid)
            self.assertEqual(result["ordered_pair_rows"], 12)
            self.assertEqual(result["candidate_mismatch_count"], 0)

            invalid = root / "invalid.jsonl"
            write_audit(invalid, trace, tamper=True)
            with self.assertRaisesRegex(
                RouteProbeError,
                "Python/C\\+\\+ ECMP candidate mismatch",
            ):
                verify_cpp_candidate_audit(trace, invalid)

    def test_missing_ordered_pair_is_rejected(self) -> None:
        edges = frozenset(((0, 1),))
        trace = EdgeTrace(2, ((0, edges),))
        with tempfile.TemporaryDirectory(
            prefix="satcompute-route-probe-missing-"
        ) as temp:
            path = Path(temp) / "audit.jsonl"
            write_audit(path, trace)
            lines = path.read_text(encoding="utf-8").splitlines()
            path.write_text(lines[0] + "\n", encoding="utf-8")
            with self.assertRaisesRegex(RouteProbeError, "every ordered pair"):
                verify_cpp_candidate_audit(trace, path)

    def test_selection_comparison_holds_last_audited_epoch(self) -> None:
        probe = ProbePair(0, 3, 20000, 30000, "fixture")

        def row(
            time_s: int,
            selected: int,
            candidates: tuple[int, ...] = (1, 2),
        ) -> SelectionAuditRow:
            return SelectionAuditRow(
                simulation_time_ns=time_s * 1_000_000_000 + 1,
                time_s=time_s,
                route_epoch=time_s,
                routing_mode="global-hrw-per-flow",
                probe=probe,
                reachable=True,
                candidate_next_hop_ids=candidates,
                selected_next_hop_id=selected,
                candidate_count_before_dedup=len(candidates),
                candidate_count_after_dedup=len(candidates),
                selection_reason="HRW_PER_FLOW",
            )

        reference = (row(0, 1), row(1, 2), row(2, 2))
        held = (row(0, 1), row(2, 2))
        metrics = compare_selection_audits(reference, held)
        self.assertAlmostEqual(
            metrics["selected_next_hop_exact_match_ratio"],
            2 / 3,
        )
        self.assertEqual(
            metrics["selected_next_hop_mismatch_count"],
            1,
        )
        self.assertEqual(
            metrics["selected_candidate_survival_ratio"],
            1.0,
        )
        self.assertEqual(
            metrics["selected_candidate_missing_count"],
            0,
        )
        self.assertEqual(
            metrics["candidate_count_trace_mismatch_count"],
            0,
        )
        self.assertEqual(
            count_selected_next_hop_changes(reference),
            1,
        )


if __name__ == "__main__":
    unittest.main()
