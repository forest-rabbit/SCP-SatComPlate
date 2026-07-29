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
    RouteProbeError,
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


if __name__ == "__main__":
    unittest.main()
