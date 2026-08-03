#!/usr/bin/env python3
"""Compute and compare unweighted ECMP candidate next-hop sets."""

from __future__ import annotations

import argparse
import hashlib
import math
import sys
from collections import Counter, deque
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Any

from ...generation.topology.common.hash_utils import compact_json
from .edge_state import (
    Edge,
    EdgeTrace,
    edge_set_sha256,
    expand_held_edges,
    load_edge_trace,
)


class EcmpCandidateError(ValueError):
    """Raised when an ECMP candidate matrix is invalid."""


@dataclass(frozen=True)
class RouteState:
    """Shortest-hop reachability and candidates for one ordered node pair."""

    reachable: bool
    shortest_hop_count: int | None
    candidate_next_hops: tuple[int, ...]


@dataclass(frozen=True)
class EcmpMatrix:
    """All ordered source/destination route states for one edge set."""

    node_count: int
    routes: tuple[tuple[RouteState, ...], ...]

    def route(self, source_id: int, destination_id: int) -> RouteState:
        return self.routes[source_id][destination_id]


def _adjacency(
    node_count: int,
    edges: frozenset[Edge],
) -> tuple[tuple[int, ...], ...]:
    neighbors = [[] for _ in range(node_count)]
    for node1_id, node2_id in edges:
        if not 0 <= node1_id < node2_id < node_count:
            raise EcmpCandidateError("edge endpoint is invalid")
        neighbors[node1_id].append(node2_id)
        neighbors[node2_id].append(node1_id)
    return tuple(tuple(sorted(items)) for items in neighbors)


def compute_ecmp_matrix(
    node_count: int,
    edges: frozenset[Edge],
) -> EcmpMatrix:
    """Derive every source candidate with one reverse BFS per destination."""
    if (
        not isinstance(node_count, int)
        or isinstance(node_count, bool)
        or node_count <= 0
    ):
        raise EcmpCandidateError("node_count must be a positive integer")
    adjacency = _adjacency(node_count, edges)
    routes = [
        [
            RouteState(False, None, ())
            for _ in range(node_count)
        ]
        for _ in range(node_count)
    ]
    for destination_id in range(node_count):
        distances = [-1] * node_count
        distances[destination_id] = 0
        pending = deque((destination_id,))
        while pending:
            node_id = pending.popleft()
            next_distance = distances[node_id] + 1
            for neighbor in adjacency[node_id]:
                if distances[neighbor] == -1:
                    distances[neighbor] = next_distance
                    pending.append(neighbor)
        for source_id in range(node_count):
            distance = distances[source_id]
            if distance < 0:
                state = RouteState(False, None, ())
            elif source_id == destination_id:
                state = RouteState(True, 0, ())
            else:
                candidates = tuple(
                    neighbor
                    for neighbor in adjacency[source_id]
                    if distances[neighbor] == distance - 1
                )
                if not candidates:
                    raise EcmpCandidateError(
                        "reachable route has no next-hop candidate"
                    )
                state = RouteState(True, distance, candidates)
            routes[source_id][destination_id] = state
    return EcmpMatrix(
        node_count,
        tuple(tuple(row) for row in routes),
    )


def ecmp_matrix_sha256(matrix: EcmpMatrix) -> str:
    """Hash all ordered route states in canonical node-ID order."""
    digest = hashlib.sha256()
    for source_id in range(matrix.node_count):
        for destination_id in range(matrix.node_count):
            if source_id == destination_id:
                continue
            state = matrix.route(source_id, destination_id)
            hop_count = (
                str(state.shortest_hop_count)
                if state.shortest_hop_count is not None
                else "-"
            )
            candidates = ",".join(
                str(node_id) for node_id in state.candidate_next_hops
            )
            digest.update(
                (
                    f"{source_id}|{destination_id}|"
                    f"{int(state.reachable)}|{hop_count}|{candidates}\n"
                ).encode("ascii")
            )
    return digest.hexdigest()


def _compare_matrices(
    reference: EcmpMatrix,
    held: EcmpMatrix,
) -> dict[str, Any]:
    if reference.node_count != held.node_count:
        raise EcmpCandidateError("ECMP matrix node counts differ")
    exact = 0
    candidate_count_mismatch = 0
    shortest_hop_mismatch = 0
    reachability_mismatch = 0
    both_unreachable = 0
    reference_reachable = 0
    held_reachable = 0
    reference_ecmp = 0
    held_ecmp = 0
    jaccard_counts: Counter[Fraction] = Counter()
    for source_id in range(reference.node_count):
        for destination_id in range(reference.node_count):
            if source_id == destination_id:
                continue
            expected = reference.route(source_id, destination_id)
            actual = held.route(source_id, destination_id)
            reference_reachable += expected.reachable
            held_reachable += actual.reachable
            reference_ecmp += (
                expected.reachable
                and len(expected.candidate_next_hops) > 1
            )
            held_ecmp += (
                actual.reachable
                and len(actual.candidate_next_hops) > 1
            )
            if not expected.reachable and not actual.reachable:
                both_unreachable += 1
                continue
            reachability_mismatch += (
                expected.reachable != actual.reachable
            )
            shortest_hop_mismatch += (
                expected.shortest_hop_count
                != actual.shortest_hop_count
            )
            candidate_count_mismatch += (
                len(expected.candidate_next_hops)
                != len(actual.candidate_next_hops)
            )
            exact += expected == actual
            expected_candidates = set(expected.candidate_next_hops)
            actual_candidates = set(actual.candidate_next_hops)
            union = expected_candidates | actual_candidates
            jaccard_counts[
                Fraction(
                    len(expected_candidates & actual_candidates),
                    len(union),
                )
                if union
                else Fraction(0, 1)
            ] += 1
    pair_count = reference.node_count * (reference.node_count - 1)
    return {
        "pair_count": pair_count,
        "comparable_pair_count": pair_count - both_unreachable,
        "exact_pair_count": exact,
        "candidate_count_mismatch_pair_count":
            candidate_count_mismatch,
        "shortest_hop_mismatch_pair_count": shortest_hop_mismatch,
        "reachability_mismatch_pair_count": reachability_mismatch,
        "both_unreachable_pair_count": both_unreachable,
        "reference_reachable_pair_count": reference_reachable,
        "held_reachable_pair_count": held_reachable,
        "reference_ecmp_pair_count": reference_ecmp,
        "held_ecmp_pair_count": held_ecmp,
        "jaccard_counts": jaccard_counts,
    }


def _weighted_percentile(
    counts: Counter[Fraction],
    percentile: float,
) -> float | None:
    total = sum(counts.values())
    if total == 0:
        return None
    rank = max(1, math.ceil(percentile * total))
    cumulative = 0
    for value in sorted(counts):
        cumulative += counts[value]
        if cumulative >= rank:
            return float(value)
    raise AssertionError("weighted percentile rank was not reached")


def compare_ecmp_traces(
    reference: EdgeTrace,
    held: EdgeTrace,
) -> dict[str, Any]:
    """Compare every ordered route state at every reference second."""
    held_edges = expand_held_edges(reference, held)
    reference_edges = tuple(edges for _, edges in reference.snapshots)
    matrix_cache: dict[str, EcmpMatrix] = {}
    route_fingerprint_cache: dict[str, str] = {}

    def matrix_for(edges: frozenset[Edge]) -> tuple[str, EcmpMatrix]:
        fingerprint = edge_set_sha256(edges)
        if fingerprint not in matrix_cache:
            matrix_cache[fingerprint] = compute_ecmp_matrix(
                reference.node_count,
                edges,
            )
            route_fingerprint_cache[fingerprint] = ecmp_matrix_sha256(
                matrix_cache[fingerprint]
            )
        return fingerprint, matrix_cache[fingerprint]

    state_pair_counts: Counter[tuple[str, str]] = Counter()
    reference_route_fingerprints = set()
    held_route_fingerprints = set()
    for expected_edges, actual_edges in zip(reference_edges, held_edges):
        expected_edge_sha, _ = matrix_for(expected_edges)
        actual_edge_sha, _ = matrix_for(actual_edges)
        state_pair_counts[(expected_edge_sha, actual_edge_sha)] += 1
        reference_route_fingerprints.add(
            route_fingerprint_cache[expected_edge_sha]
        )
        held_route_fingerprints.add(
            route_fingerprint_cache[actual_edge_sha]
        )

    comparison_cache = {}
    totals: Counter[str] = Counter()
    jaccard_counts: Counter[Fraction] = Counter()
    for fingerprints, seconds in state_pair_counts.items():
        if fingerprints not in comparison_cache:
            comparison_cache[fingerprints] = _compare_matrices(
                matrix_cache[fingerprints[0]],
                matrix_cache[fingerprints[1]],
            )
        comparison = comparison_cache[fingerprints]
        for field, value in comparison.items():
            if field == "jaccard_counts":
                for jaccard, count in value.items():
                    jaccard_counts[jaccard] += count * seconds
            else:
                totals[field] += value * seconds

    comparable = totals["comparable_pair_count"]
    exact = totals["exact_pair_count"]
    reference_reachable = totals["reference_reachable_pair_count"]
    held_reachable = totals["held_reachable_pair_count"]
    jaccard_total = sum(jaccard_counts.values())
    mean_jaccard = (
        sum(float(value) * count for value, count in jaccard_counts.items())
        / jaccard_total
        if jaccard_total
        else None
    )
    return {
        "ordered_pair_seconds": totals["pair_count"],
        "candidate_comparable_pair_seconds": comparable,
        "both_unreachable_pair_seconds":
            totals["both_unreachable_pair_count"],
        "reachability_mismatch_pair_seconds":
            totals["reachability_mismatch_pair_count"],
        "exact_candidate_match_ratio":
            exact / comparable if comparable else None,
        "candidate_mismatch_pair_seconds": comparable - exact,
        "candidate_count_mismatch_pair_seconds":
            totals["candidate_count_mismatch_pair_count"],
        "shortest_hop_mismatch_pair_seconds":
            totals["shortest_hop_mismatch_pair_count"],
        "mean_candidate_jaccard": mean_jaccard,
        "p05_candidate_jaccard":
            _weighted_percentile(jaccard_counts, 0.05),
        "minimum_candidate_jaccard":
            float(min(jaccard_counts)) if jaccard_counts else None,
        "reference_reachable_pair_fraction":
            reference_reachable / totals["pair_count"],
        "held_reachable_pair_fraction":
            held_reachable / totals["pair_count"],
        "reference_ecmp_pair_fraction":
            totals["reference_ecmp_pair_count"] / reference_reachable
            if reference_reachable
            else None,
        "held_ecmp_pair_fraction":
            totals["held_ecmp_pair_count"] / held_reachable
            if held_reachable
            else None,
        "reference_unique_ecmp_candidate_fingerprint_count":
            len(reference_route_fingerprints),
        "held_unique_ecmp_candidate_fingerprint_count":
            len(held_route_fingerprints),
        "unique_edge_state_pair_comparisons": len(comparison_cache),
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, required=True)
    parser.add_argument("--held-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        result = compare_ecmp_traces(
            load_edge_trace(arguments.reference_dir),
            load_edge_trace(arguments.held_dir),
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(compact_json(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
