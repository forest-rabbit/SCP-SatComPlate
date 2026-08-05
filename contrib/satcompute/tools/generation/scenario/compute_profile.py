#!/usr/bin/env python3
"""Build and validate canonical SatCompute compute-profile JSON."""

from __future__ import annotations

from dataclasses import dataclass
import json
from typing import Any, Iterable


SCHEMA_VERSION = "0.1"
ROOT_FIELDS = frozenset(("schema_version", "compute_nodes"))
NODE_FIELDS = frozenset(
    ("node_id", "compute_rate_work_units_per_second")
)
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1


class ComputeProfileError(ValueError):
    """Raised when a compute profile violates schema 0.1."""


@dataclass(frozen=True, order=True)
class ComputeNode:
    node_id: int
    compute_rate_work_units_per_second: int


def _require_integer(
    value: Any,
    name: str,
    minimum: int,
    maximum: int,
) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or not minimum <= value <= maximum
    ):
        raise ComputeProfileError(
            f"{name} must be an integer in [{minimum}, {maximum}]"
        )
    return value


def build_compute_profile(
    selected_node_ids: Iterable[int],
    compute_rate_work_units_per_second: int,
) -> dict[str, Any]:
    """Build one canonical profile from selected satellite IDs."""
    rate = _require_integer(
        compute_rate_work_units_per_second,
        "compute_rate_work_units_per_second",
        1,
        UINT64_MAX,
    )
    node_ids = tuple(
        _require_integer(node_id, "node_id", 0, UINT32_MAX)
        for node_id in selected_node_ids
    )
    if not node_ids:
        raise ComputeProfileError("compute profile must not be empty")
    if len(node_ids) != len(set(node_ids)):
        raise ComputeProfileError("compute node IDs must be unique")
    return {
        "schema_version": SCHEMA_VERSION,
        "compute_nodes": [
            {
                "node_id": node_id,
                "compute_rate_work_units_per_second": rate,
            }
            for node_id in sorted(node_ids)
        ],
    }


def parse_compute_profile(
    payload: Any,
    *,
    valid_node_ids: Iterable[int] | None = None,
) -> tuple[ComputeNode, ...]:
    """Validate a canonical compute profile and return ordered nodes."""
    if not isinstance(payload, dict) or set(payload) != ROOT_FIELDS:
        raise ComputeProfileError(
            "compute profile root must contain only "
            "schema_version and compute_nodes"
        )
    if payload["schema_version"] != SCHEMA_VERSION:
        raise ComputeProfileError(
            f"schema_version must be {SCHEMA_VERSION}"
        )
    items = payload["compute_nodes"]
    if not isinstance(items, list) or not items:
        raise ComputeProfileError(
            "compute_nodes must be a non-empty array"
        )
    allowed_ids = (
        None if valid_node_ids is None else frozenset(valid_node_ids)
    )
    nodes = []
    for item in items:
        if not isinstance(item, dict) or set(item) != NODE_FIELDS:
            raise ComputeProfileError(
                "compute node fields must be node_id and "
                "compute_rate_work_units_per_second"
            )
        node = ComputeNode(
            node_id=_require_integer(
                item["node_id"],
                "node_id",
                0,
                UINT32_MAX,
            ),
            compute_rate_work_units_per_second=_require_integer(
                item["compute_rate_work_units_per_second"],
                "compute_rate_work_units_per_second",
                1,
                UINT64_MAX,
            ),
        )
        if allowed_ids is not None and node.node_id not in allowed_ids:
            raise ComputeProfileError(
                f"compute profile references unknown node_id={node.node_id}"
            )
        nodes.append(node)
    if len({node.node_id for node in nodes}) != len(nodes):
        raise ComputeProfileError("compute node IDs must be unique")
    return tuple(sorted(nodes, key=lambda node: node.node_id))


def compute_profile_bytes(payload: Any) -> bytes:
    """Validate and serialize one canonical compute profile."""
    nodes = parse_compute_profile(payload)
    canonical = {
        "schema_version": SCHEMA_VERSION,
        "compute_nodes": [
            {
                "node_id": node.node_id,
                "compute_rate_work_units_per_second": (
                    node.compute_rate_work_units_per_second
                ),
            }
            for node in nodes
        ],
    }
    return (
        json.dumps(canonical, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode("utf-8")
