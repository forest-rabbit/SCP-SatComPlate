#!/usr/bin/env python3
"""Validate independent workload inputs against one topology-trace manifest."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ..topology.common.manifest import read_json, validate_trace
from .compute_profile import ComputeNode, parse_compute_profile


UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
INT64_MAX = (1 << 63) - 1


class InputBundleConfigError(ValueError):
    """Raised when independent inputs cannot form one deterministic bundle."""


@dataclass(frozen=True)
class TopologyContext:
    """Stable satellite inventory and provenance extracted from a trace."""

    trace_root: Path
    manifest_path: Path
    manifest_sha256: str
    constellation_config_sha256: str
    simulation_duration_ns: int
    satellite_ids: tuple[int, ...]


def _require_object(value: Any, name: str, fields: set[str]) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != fields:
        raise InputBundleConfigError(f"{name} fields differ")
    return value


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
        raise InputBundleConfigError(
            f"{name} must be an integer in [{minimum}, {maximum}]"
        )
    return value


def load_json_file(path: Path, name: str) -> dict[str, Any]:
    source = Path(path)
    try:
        payload = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise InputBundleConfigError(f"cannot read {name} {source}: {error}") from error
    if not isinstance(payload, dict):
        raise InputBundleConfigError(f"{name} must be a JSON object")
    return payload


def load_topology_context(trace_root: Path) -> TopologyContext:
    """Read stable IDs only through a checked v0.3 topology manifest."""
    import hashlib

    root = Path(trace_root).resolve()
    validate_trace(root)
    manifest_path = root / "manifest.json"
    manifest = read_json(manifest_path)
    if (
        not isinstance(manifest["simulation_duration_ns"], int)
        or isinstance(manifest["simulation_duration_ns"], bool)
        or manifest["simulation_duration_ns"] <= 0
    ):
        raise InputBundleConfigError("topology trace duration must be positive")
    first_nodes = read_json(root / manifest["slices"][0]["nodes_file"])
    satellite_ids = tuple(node.get("node_id") for node in first_nodes["nodes"])
    if satellite_ids != tuple(sorted(set(satellite_ids))):
        raise InputBundleConfigError("topology trace satellite IDs are not canonical")
    if any(
        not isinstance(node_id, int)
        or isinstance(node_id, bool)
        or not 0 <= node_id <= UINT32_MAX
        for node_id in satellite_ids
    ):
        raise InputBundleConfigError("topology trace satellite ID is invalid")
    return TopologyContext(
        trace_root=root,
        manifest_path=manifest_path,
        manifest_sha256=hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        constellation_config_sha256=manifest["constellation_config_sha256"],
        simulation_duration_ns=manifest["simulation_duration_ns"],
        satellite_ids=satellite_ids,
    )


def validate_compute_profile(
    payload: Any,
    context: TopologyContext,
) -> tuple[ComputeNode, ...]:
    """Apply the same stable-ID subset contract as the C++ loader."""
    try:
        return parse_compute_profile(payload, valid_node_ids=context.satellite_ids)
    except ValueError as error:
        raise InputBundleConfigError(str(error)) from error


def validate_task_trace(
    payload: Any,
    context: TopologyContext,
    compute_nodes: tuple[ComputeNode, ...],
) -> int:
    """Validate TaskTrace 0.1 fields and endpoint/time relationships."""
    root = _require_object(payload, "TaskTrace root", {"schema_version", "tasks"})
    if root["schema_version"] != "0.1":
        raise InputBundleConfigError("TaskTrace schema_version must be 0.1")
    tasks = root["tasks"]
    if not isinstance(tasks, list) or not tasks:
        raise InputBundleConfigError("TaskTrace tasks must be non-empty")
    valid_ids = frozenset(context.satellite_ids)
    compute_ids = frozenset(node.node_id for node in compute_nodes)
    task_ids = set()
    fields = {
        "task_id",
        "source_node_id",
        "compute_node_id",
        "result_node_id",
        "input_bytes",
        "output_bytes",
        "compute_work_units",
        "arrival_time_ns",
    }
    for index, item in enumerate(tasks):
        task = _require_object(item, f"tasks[{index}]", fields)
        task_id = _require_integer(task["task_id"], "task_id", 1, UINT64_MAX // 2)
        source_id = _require_integer(
            task["source_node_id"], "source_node_id", 0, UINT32_MAX
        )
        compute_id = _require_integer(
            task["compute_node_id"], "compute_node_id", 0, UINT32_MAX
        )
        result_id = _require_integer(
            task["result_node_id"], "result_node_id", 0, UINT32_MAX
        )
        for field in ("input_bytes", "output_bytes", "compute_work_units"):
            _require_integer(task[field], field, 1, UINT64_MAX)
        _require_integer(
            task["arrival_time_ns"],
            "arrival_time_ns",
            0,
            min(INT64_MAX, context.simulation_duration_ns - 1),
        )
        if {source_id, compute_id, result_id} - valid_ids:
            raise InputBundleConfigError(f"tasks[{index}] references an unknown satellite")
        if compute_id not in compute_ids:
            raise InputBundleConfigError(f"tasks[{index}] compute node is absent from profile")
        if source_id == compute_id or compute_id == result_id:
            raise InputBundleConfigError(f"tasks[{index}] has invalid endpoint equality")
        if task_id in task_ids:
            raise InputBundleConfigError("task_id must be unique")
        task_ids.add(task_id)
    return len(tasks)


def validate_transfer_trace(payload: Any, context: TopologyContext) -> int:
    """Validate direct TransferTrace 0.1 against manifest satellite IDs."""
    root = _require_object(
        payload,
        "TransferTrace root",
        {"schema_version", "transfers"},
    )
    if root["schema_version"] != "0.1":
        raise InputBundleConfigError("TransferTrace schema_version must be 0.1")
    transfers = root["transfers"]
    if not isinstance(transfers, list) or not transfers:
        raise InputBundleConfigError("TransferTrace transfers must be non-empty")
    valid_ids = frozenset(context.satellite_ids)
    transfer_ids = set()
    source_counts: dict[int, int] = {}
    fields = {
        "transfer_id",
        "source_node_id",
        "destination_node_id",
        "size_bytes",
        "arrival_time_ns",
    }
    for index, item in enumerate(transfers):
        transfer = _require_object(item, f"transfers[{index}]", fields)
        transfer_id = _require_integer(
            transfer["transfer_id"], "transfer_id", 1, UINT64_MAX
        )
        source_id = _require_integer(
            transfer["source_node_id"], "source_node_id", 0, UINT32_MAX
        )
        destination_id = _require_integer(
            transfer["destination_node_id"],
            "destination_node_id",
            0,
            UINT32_MAX,
        )
        _require_integer(transfer["size_bytes"], "size_bytes", 1, UINT64_MAX)
        _require_integer(
            transfer["arrival_time_ns"],
            "arrival_time_ns",
            0,
            min(INT64_MAX, context.simulation_duration_ns - 1),
        )
        if source_id == destination_id:
            raise InputBundleConfigError(
                f"transfers[{index}] source and destination must differ"
            )
        if source_id not in valid_ids or destination_id not in valid_ids:
            raise InputBundleConfigError(
                f"transfers[{index}] references an unknown satellite"
            )
        if transfer_id in transfer_ids:
            raise InputBundleConfigError("transfer_id must be unique")
        transfer_ids.add(transfer_id)
        source_counts[source_id] = source_counts.get(source_id, 0) + 1
        if source_counts[source_id] > 55_536:
            raise InputBundleConfigError("source satellite exhausts UDP source ports")
    return len(transfers)
