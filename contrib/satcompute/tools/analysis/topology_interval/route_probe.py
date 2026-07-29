#!/usr/bin/env python3
"""Build deterministic probes and validate C++ route-audit evidence."""

from __future__ import annotations

import argparse
import json
import sys
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ...generation.scenario.check_scenario import check_scenario
from ...generation.topology.common.hash_utils import (
    compact_json,
    compact_json_bytes,
    sha256_file,
)
from ...generation.topology.common.satcompute_schema import read_json
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
SELECTION_AUDIT_FIELDS = frozenset(
    (
        "simulation_time_ns",
        "time_s",
        "route_epoch",
        "routing_mode",
        "source_id",
        "destination_id",
        "source_port",
        "destination_port",
        "category",
        "reachable",
        "candidate_next_hop_ids",
        "selected_next_hop_id",
        "selected_gateway",
        "selected_output_interface",
        "candidate_count_before_dedup",
        "candidate_count_after_dedup",
        "event_selected_gateway",
        "event_selected_output_interface",
        "hash_value",
        "selection_reason",
    )
)
SELECTION_MODES = frozenset(
    (
        "global-first",
        "global-hash-per-flow",
        "global-hrw-per-flow",
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


@dataclass(frozen=True)
class ProbePair:
    source_id: int
    destination_id: int
    source_port: int
    destination_port: int
    category: str

    def input_dict(self) -> dict[str, Any]:
        return {
            "source_id": self.source_id,
            "destination_id": self.destination_id,
            "source_port": self.source_port,
            "destination_port": self.destination_port,
            "category": self.category,
        }


@dataclass(frozen=True)
class SelectionAuditRow:
    simulation_time_ns: int
    time_s: int
    route_epoch: int
    routing_mode: str
    probe: ProbePair
    reachable: bool
    candidate_next_hop_ids: tuple[int, ...]
    selected_next_hop_id: int | None
    candidate_count_before_dedup: int
    candidate_count_after_dedup: int
    selection_reason: str


def _append_pair(
    target: list[tuple[int, int]],
    source_id: int,
    destination_id: int,
    node_count: int,
) -> None:
    if (
        0 <= source_id < node_count
        and 0 <= destination_id < node_count
        and source_id != destination_id
    ):
        target.append((source_id, destination_id))


def build_probe_pairs(
    num_orbits: int,
    satellites_per_orbit: int,
    compute_node_ids: tuple[int, ...],
    *,
    maximum_pairs: int = 64,
) -> tuple[ProbePair, ...]:
    """Build a deterministic, category-interleaved ordered-pair sample."""
    if (
        not isinstance(num_orbits, int)
        or isinstance(num_orbits, bool)
        or num_orbits <= 0
        or not isinstance(satellites_per_orbit, int)
        or isinstance(satellites_per_orbit, bool)
        or satellites_per_orbit <= 0
        or not isinstance(maximum_pairs, int)
        or isinstance(maximum_pairs, bool)
        or not 1 <= maximum_pairs <= 64
    ):
        raise RouteProbeError("probe-pair dimensions or limit are invalid")
    node_count = num_orbits * satellites_per_orbit
    compute_ids = tuple(sorted(compute_node_ids))
    if (
        not compute_ids
        or len(compute_ids) != len(set(compute_ids))
        or any(not 0 <= node_id < node_count for node_id in compute_ids)
    ):
        raise RouteProbeError("compute_node_ids are invalid")
    compute_set = set(compute_ids)
    normal_ids = tuple(
        node_id for node_id in range(node_count)
        if node_id not in compute_set
    )
    categories: OrderedDict[str, list[tuple[int, int]]] = OrderedDict(
        (
            ("same-orbit-adjacent", []),
            ("same-orbit-far", []),
            ("adjacent-orbit", []),
            ("non-adjacent-orbit", []),
            ("boundary-to-internal", []),
            ("compute-to-compute", []),
            ("compute-to-normal", []),
            ("normal-to-compute", []),
        )
    )
    slots = tuple(
        sorted(
            {
                0,
                satellites_per_orbit // 3,
                (2 * satellites_per_orbit) // 3,
            }
        )
    )
    for orbit in range(num_orbits):
        base = orbit * satellites_per_orbit
        for slot in slots:
            adjacent = base + ((slot + 1) % satellites_per_orbit)
            _append_pair(
                categories["same-orbit-adjacent"],
                base + slot,
                adjacent,
                node_count,
            )
            _append_pair(
                categories["same-orbit-adjacent"],
                adjacent,
                base + slot,
                node_count,
            )
            far = base + (
                (slot + satellites_per_orbit // 2)
                % satellites_per_orbit
            )
            _append_pair(
                categories["same-orbit-far"],
                base + slot,
                far,
                node_count,
            )
    for orbit in range(max(0, num_orbits - 1)):
        for slot in slots:
            first = orbit * satellites_per_orbit + slot
            second = (orbit + 1) * satellites_per_orbit + slot
            _append_pair(
                categories["adjacent-orbit"],
                first,
                second,
                node_count,
            )
            _append_pair(
                categories["adjacent-orbit"],
                second,
                first,
                node_count,
            )
    if num_orbits >= 3:
        distant_orbit = max(2, num_orbits // 2)
        for slot in slots:
            _append_pair(
                categories["non-adjacent-orbit"],
                slot,
                distant_orbit * satellites_per_orbit + slot,
                node_count,
            )
            _append_pair(
                categories["non-adjacent-orbit"],
                distant_orbit * satellites_per_orbit + slot,
                slot,
                node_count,
            )
        internal_orbit = min(
            num_orbits - 2,
            max(1, num_orbits // 2),
        )
        for slot in slots:
            internal = internal_orbit * satellites_per_orbit + slot
            _append_pair(
                categories["boundary-to-internal"],
                slot,
                internal,
                node_count,
            )
            _append_pair(
                categories["boundary-to-internal"],
                (num_orbits - 1) * satellites_per_orbit + slot,
                internal,
                node_count,
            )
    for index, source_id in enumerate(compute_ids):
        _append_pair(
            categories["compute-to-compute"],
            source_id,
            compute_ids[(index + 1) % len(compute_ids)],
            node_count,
        )
    if normal_ids:
        for index, compute_id in enumerate(compute_ids):
            normal_id = normal_ids[index % len(normal_ids)]
            _append_pair(
                categories["compute-to-normal"],
                compute_id,
                normal_id,
                node_count,
            )
            _append_pair(
                categories["normal-to-compute"],
                normal_id,
                compute_id,
                node_count,
            )

    selected = []
    selected_endpoints = set()
    category_indexes = {category: 0 for category in categories}
    while len(selected) < maximum_pairs:
        added_in_round = False
        for category, candidates in categories.items():
            index = category_indexes[category]
            while (
                index < len(candidates)
                and candidates[index] in selected_endpoints
            ):
                index += 1
            category_indexes[category] = index
            if index >= len(candidates):
                continue
            endpoints = candidates[index]
            category_indexes[category] += 1
            selected_endpoints.add(endpoints)
            selected.append((category, endpoints))
            added_in_round = True
            if len(selected) == maximum_pairs:
                break
        if not added_in_round:
            break
    if not selected:
        raise RouteProbeError("probe-pair construction produced no pairs")
    return tuple(
        ProbePair(
            source_id=source_id,
            destination_id=destination_id,
            source_port=20000 + index,
            destination_port=30000 + index,
            category=category,
        )
        for index, (category, (source_id, destination_id))
        in enumerate(selected)
    )


def write_probe_pairs(
    scenario_dir: Path,
    output_path: Path,
    *,
    maximum_pairs: int = 64,
) -> tuple[tuple[ProbePair, ...], str]:
    """Write deterministic probe-pairs.json and return its SHA-256."""
    root = Path(scenario_dir)
    check_scenario(root)
    manifest = read_json(root / "scenario-manifest.json")
    config = manifest["scenario_config"]
    constellation = config["constellation"]
    pairs = build_probe_pairs(
        constellation["num_orbits"],
        constellation["satellites_per_orbit"],
        tuple(manifest["selected_compute_node_ids"]),
        maximum_pairs=maximum_pairs,
    )
    path = Path(output_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        compact_json_bytes(
            {
                "schema_version": "0.1",
                "probe_pairs": [pair.input_dict() for pair in pairs],
            }
        )
    )
    return pairs, sha256_file(path)


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


def _require_non_negative_integer(value: Any, name: str) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < 0
    ):
        raise RouteProbeError(f"{name} must be a non-negative integer")
    return value


def _parse_probe_from_selection(
    payload: dict[str, Any],
    node_count: int,
) -> ProbePair:
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
        raise RouteProbeError("selection source and destination must differ")
    source_port = _require_non_negative_integer(
        payload["source_port"],
        "source_port",
    )
    destination_port = _require_non_negative_integer(
        payload["destination_port"],
        "destination_port",
    )
    if not 1 <= source_port <= 65535 or not 1 <= destination_port <= 65535:
        raise RouteProbeError("selection ports must be in 1..65535")
    category = payload["category"]
    if not isinstance(category, str) or not category:
        raise RouteProbeError("selection category must be non-empty")
    return ProbePair(
        source_id,
        destination_id,
        source_port,
        destination_port,
        category,
    )


def load_selection_audit(
    audit_path: Path,
    node_count: int,
) -> tuple[SelectionAuditRow, ...]:
    """Parse strict route-selection JSONL from the C++ audit executable."""
    rows = []
    seen = set()
    for line_number, line in enumerate(
        Path(audit_path).read_text(encoding="utf-8").splitlines(),
        start=1,
    ):
        try:
            payload = json.loads(line)
        except json.JSONDecodeError as error:
            raise RouteProbeError(
                f"selection line {line_number} is invalid JSON: {error}"
            ) from error
        if (
            not isinstance(payload, dict)
            or frozenset(payload) != SELECTION_AUDIT_FIELDS
        ):
            raise RouteProbeError(
                f"selection line {line_number} fields are invalid"
            )
        time_s = _require_non_negative_integer(payload["time_s"], "time_s")
        simulation_time_ns = _require_non_negative_integer(
            payload["simulation_time_ns"],
            "simulation_time_ns",
        )
        if simulation_time_ns != time_s * 1_000_000_000 + 1:
            raise RouteProbeError(
                "selection simulation_time_ns differs from audit time"
            )
        route_epoch = _require_non_negative_integer(
            payload["route_epoch"],
            "route_epoch",
        )
        routing_mode = payload["routing_mode"]
        if routing_mode not in SELECTION_MODES:
            raise RouteProbeError("selection routing_mode is invalid")
        probe = _parse_probe_from_selection(payload, node_count)
        reachable = payload["reachable"]
        if not isinstance(reachable, bool):
            raise RouteProbeError("selection reachable must be boolean")
        candidate_payload = payload["candidate_next_hop_ids"]
        if not isinstance(candidate_payload, list):
            raise RouteProbeError(
                "selection candidate_next_hop_ids must be an array"
            )
        candidates = tuple(
            _require_node_id(item, "candidate_next_hop_id", node_count)
            for item in candidate_payload
        )
        if candidates != tuple(sorted(set(candidates))):
            raise RouteProbeError(
                "selection candidates must be sorted and unique"
            )
        selected = payload["selected_next_hop_id"]
        if selected is not None:
            selected = _require_node_id(
                selected,
                "selected_next_hop_id",
                node_count,
            )
        if (
            reachable != bool(candidates)
            or reachable != (selected is not None)
            or (selected is not None and selected not in candidates)
        ):
            raise RouteProbeError(
                "selection reachability, candidates, and next hop disagree"
            )
        before = _require_non_negative_integer(
            payload["candidate_count_before_dedup"],
            "candidate_count_before_dedup",
        )
        after = _require_non_negative_integer(
            payload["candidate_count_after_dedup"],
            "candidate_count_after_dedup",
        )
        if before < after:
            raise RouteProbeError(
                "candidate count before dedup is smaller than after dedup"
            )
        for field in (
            "selected_gateway",
            "event_selected_gateway",
            "selection_reason",
        ):
            if not isinstance(payload[field], str) or not payload[field]:
                raise RouteProbeError(f"selection {field} is invalid")
        _require_non_negative_integer(payload["hash_value"], "hash_value")
        for field in (
            "selected_output_interface",
            "event_selected_output_interface",
        ):
            value = payload[field]
            if (
                not isinstance(value, int)
                or isinstance(value, bool)
                or value < -1
            ):
                raise RouteProbeError(f"selection {field} is invalid")
        key = (time_s, probe.source_id, probe.destination_id)
        if key in seen:
            raise RouteProbeError("selection audit contains duplicate rows")
        seen.add(key)
        rows.append(
            SelectionAuditRow(
                simulation_time_ns,
                time_s,
                route_epoch,
                routing_mode,
                probe,
                reachable,
                candidates,
                selected,
                before,
                after,
                payload["selection_reason"],
            )
        )
    if not rows:
        raise RouteProbeError("selection audit is empty")
    return tuple(rows)


def compare_selection_audits(
    reference_rows: tuple[SelectionAuditRow, ...],
    held_rows: tuple[SelectionAuditRow, ...],
) -> dict[str, Any]:
    """Compare actual C++ next hops under reference and held schedules."""
    reference_modes = {row.routing_mode for row in reference_rows}
    held_modes = {row.routing_mode for row in held_rows}
    if len(reference_modes) != 1 or held_modes != reference_modes:
        raise RouteProbeError("selection audit routing modes differ")
    reference_times = tuple(sorted({row.time_s for row in reference_rows}))
    held_times = tuple(sorted({row.time_s for row in held_rows}))
    if (
        reference_times != tuple(range(reference_times[-1] + 1))
        or held_times[0] != 0
        or held_times[-1] != reference_times[-1]
    ):
        raise RouteProbeError(
            "selection audits do not form reference and held schedules"
        )
    reference_by_key = {
        (row.time_s, row.probe.source_id, row.probe.destination_id): row
        for row in reference_rows
    }
    held_by_key = {
        (row.time_s, row.probe.source_id, row.probe.destination_id): row
        for row in held_rows
    }
    reference_probes = {
        row.probe for row in reference_rows if row.time_s == 0
    }
    held_probes = {row.probe for row in held_rows if row.time_s == 0}
    if not reference_probes or held_probes != reference_probes:
        raise RouteProbeError("selection audits use different probe pairs")
    expected_reference_rows = len(reference_times) * len(reference_probes)
    expected_held_rows = len(held_times) * len(reference_probes)
    if (
        len(reference_by_key) != expected_reference_rows
        or len(held_by_key) != expected_held_rows
    ):
        raise RouteProbeError(
            "selection audit is missing a probe at one or more epochs"
        )

    selected_exact = 0
    selected_mismatch = 0
    survival_denominator = 0
    selected_missing = 0
    candidate_count_mismatch = 0
    event_candidate_count_mismatch = 0
    reachability_mismatch = 0
    held_index = 0
    for time_s in reference_times:
        while (
            held_index + 1 < len(held_times)
            and held_times[held_index + 1] <= time_s
        ):
            held_index += 1
        held_time = held_times[held_index]
        for probe in reference_probes:
            key = (time_s, probe.source_id, probe.destination_id)
            held_key = (
                held_time,
                probe.source_id,
                probe.destination_id,
            )
            expected = reference_by_key[key]
            actual = held_by_key[held_key]
            exact = (
                expected.reachable == actual.reachable
                and expected.selected_next_hop_id
                == actual.selected_next_hop_id
            )
            selected_exact += exact
            selected_mismatch += not exact
            reachability_mismatch += (
                expected.reachable != actual.reachable
            )
            candidate_count_mismatch += (
                len(expected.candidate_next_hop_ids)
                != len(actual.candidate_next_hop_ids)
            )
            event_candidate_count_mismatch += (
                expected.candidate_count_after_dedup
                != actual.candidate_count_after_dedup
            )
            if expected.selected_next_hop_id is not None:
                survival_denominator += 1
                selected_missing += (
                    expected.selected_next_hop_id
                    not in actual.candidate_next_hop_ids
                )
    total = selected_exact + selected_mismatch
    return {
        "routing_mode": next(iter(reference_modes)),
        "probe_pair_count": len(reference_probes),
        "reference_audit_time_count": len(reference_times),
        "held_audit_time_count": len(held_times),
        "selected_next_hop_exact_match_ratio":
            selected_exact / total if total else None,
        "selected_next_hop_mismatch_count": selected_mismatch,
        "selected_candidate_survival_ratio":
            (survival_denominator - selected_missing)
            / survival_denominator
            if survival_denominator
            else None,
        "selected_candidate_missing_count": selected_missing,
        "candidate_count_trace_mismatch_count":
            candidate_count_mismatch,
        "event_candidate_count_trace_mismatch_count":
            event_candidate_count_mismatch,
        "reachability_mismatch_count": reachability_mismatch,
    }


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
    commands = parser.add_subparsers(dest="command", required=True)
    candidate = commands.add_parser(
        "candidate-gate",
        help="compare an all-pairs C++ candidate audit with Python",
    )
    candidate.add_argument("--topology-dir", type=Path, required=True)
    candidate.add_argument("--candidate-audit", type=Path, required=True)
    pairs = commands.add_parser(
        "generate-pairs",
        help="write deterministic probe-pairs.json",
    )
    pairs.add_argument("--scenario-dir", type=Path, required=True)
    pairs.add_argument("--output", type=Path, required=True)
    pairs.add_argument("--maximum-pairs", type=int, default=64)
    selection = commands.add_parser(
        "compare-selection",
        help="compare reference and held C++ selection audits",
    )
    selection.add_argument("--reference-audit", type=Path, required=True)
    selection.add_argument("--held-audit", type=Path, required=True)
    selection.add_argument("--node-count", type=int, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        if arguments.command == "candidate-gate":
            result = verify_cpp_candidate_audit(
                load_topology_edge_trace(arguments.topology_dir),
                arguments.candidate_audit,
            )
        elif arguments.command == "generate-pairs":
            pairs, sha256 = write_probe_pairs(
                arguments.scenario_dir,
                arguments.output,
                maximum_pairs=arguments.maximum_pairs,
            )
            result = {
                "probe_pair_count": len(pairs),
                "probe_pairs_sha256": sha256,
                "output": str(arguments.output),
            }
        else:
            result = compare_selection_audits(
                load_selection_audit(
                    arguments.reference_audit,
                    arguments.node_count,
                ),
                load_selection_audit(
                    arguments.held_audit,
                    arguments.node_count,
                ),
            )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(compact_json(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
