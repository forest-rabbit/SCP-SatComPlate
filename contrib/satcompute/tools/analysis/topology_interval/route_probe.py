#!/usr/bin/env python3
"""Validate C++ route-candidate audits against Python shortest-hop ECMP."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ...generation.topology.common.hash_utils import compact_json
from .ecmp_candidates import compute_ecmp_matrix
from .edge_state import (
    EdgeTrace,
    edge_set_sha256,
    load_topology_edge_trace,
)


AUDIT_FIELDS = frozenset(
    (
        "time_s",
        "source_id",
        "destination_id",
        "reachable",
        "candidate_next_hop_ids",
    )
)


class RouteProbeError(ValueError):
    """Raised when C++ route audit evidence violates the gate."""


@dataclass(frozen=True)
class CandidateAuditRow:
    time_s: int
    source_id: int
    destination_id: int
    reachable: bool
    candidate_next_hop_ids: tuple[int, ...]


def _require_node_id(value: Any, name: str, node_count: int) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or not 0 <= value < node_count
    ):
        raise RouteProbeError(
            f"{name} must be an integer in [0, {node_count})"
        )
    return value


def load_candidate_audit(
    audit_path: Path,
    node_count: int,
) -> tuple[CandidateAuditRow, ...]:
    """Parse strict JSONL emitted by the C++ read-only audit executable."""
    path = Path(audit_path)
    rows = []
    seen = set()
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(),
        start=1,
    ):
        try:
            payload = json.loads(line)
        except json.JSONDecodeError as error:
            raise RouteProbeError(
                f"audit line {line_number} is invalid JSON: {error}"
            ) from error
        if not isinstance(payload, dict) or frozenset(payload) != AUDIT_FIELDS:
            raise RouteProbeError(
                f"audit line {line_number} fields are invalid"
            )
        time_s = payload["time_s"]
        if (
            not isinstance(time_s, int)
            or isinstance(time_s, bool)
            or time_s < 0
        ):
            raise RouteProbeError("audit time_s must be non-negative")
        source_id = _require_node_id(
            payload["source_id"],
            "source_id",
            node_count,
        )
        destination_id = _require_node_id(
            payload["destination_id"],
            "destination_id",
            node_count,
        )
        if source_id == destination_id:
            raise RouteProbeError("audit source and destination must differ")
        reachable = payload["reachable"]
        if not isinstance(reachable, bool):
            raise RouteProbeError("audit reachable must be boolean")
        candidates_payload = payload["candidate_next_hop_ids"]
        if not isinstance(candidates_payload, list):
            raise RouteProbeError(
                "candidate_next_hop_ids must be an array"
            )
        candidates = tuple(
            _require_node_id(item, "candidate_next_hop_id", node_count)
            for item in candidates_payload
        )
        if candidates != tuple(sorted(set(candidates))):
            raise RouteProbeError(
                "candidate_next_hop_ids must be sorted and unique"
            )
        if reachable != bool(candidates):
            raise RouteProbeError(
                "audit reachable must agree with candidate presence"
            )
        key = (time_s, source_id, destination_id)
        if key in seen:
            raise RouteProbeError("audit contains a duplicate route row")
        seen.add(key)
        rows.append(
            CandidateAuditRow(
                time_s,
                source_id,
                destination_id,
                reachable,
                candidates,
            )
        )
    if not rows:
        raise RouteProbeError("candidate audit is empty")
    return tuple(rows)


def verify_cpp_candidate_audit(
    trace: EdgeTrace,
    audit_path: Path,
) -> dict[str, Any]:
    """Require exact candidate IDs and reachability for every audited time."""
    rows = load_candidate_audit(audit_path, trace.node_count)
    edges_by_time = dict(trace.snapshots)
    audit_times = tuple(sorted({row.time_s for row in rows}))
    for time_s in audit_times:
        if time_s not in edges_by_time:
            raise RouteProbeError(
                f"audit time {time_s}s is not a topology snapshot"
            )
    expected_keys = {
        (time_s, source_id, destination_id)
        for time_s in audit_times
        for source_id in range(trace.node_count)
        for destination_id in range(trace.node_count)
        if source_id != destination_id
    }
    actual_keys = {
        (row.time_s, row.source_id, row.destination_id)
        for row in rows
    }
    if actual_keys != expected_keys:
        raise RouteProbeError(
            "audit does not contain every ordered pair exactly once: "
            f"missing={len(expected_keys - actual_keys)}, "
            f"unknown={len(actual_keys - expected_keys)}"
        )

    matrix_cache = {}
    for row in rows:
        edges = edges_by_time[row.time_s]
        fingerprint = edge_set_sha256(edges)
        if fingerprint not in matrix_cache:
            matrix_cache[fingerprint] = compute_ecmp_matrix(
                trace.node_count,
                edges,
            )
        expected = matrix_cache[fingerprint].route(
            row.source_id,
            row.destination_id,
        )
        if (
            row.reachable != expected.reachable
            or row.candidate_next_hop_ids
            != expected.candidate_next_hops
        ):
            raise RouteProbeError(
                "Python/C++ ECMP candidate mismatch at "
                f"t={row.time_s}s {row.source_id}->{row.destination_id}: "
                f"python={expected.candidate_next_hops}, "
                f"cpp={row.candidate_next_hop_ids}"
            )
    return {
        "node_count": trace.node_count,
        "audit_times_s": list(audit_times),
        "ordered_pair_rows": len(rows),
        "unique_edge_set_count": len(matrix_cache),
        "candidate_mismatch_count": 0,
        "conclusion": "success",
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--topology-dir", type=Path, required=True)
    parser.add_argument("--candidate-audit", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        result = verify_cpp_candidate_audit(
            load_topology_edge_trace(arguments.topology_dir),
            arguments.candidate_audit,
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(compact_json(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
