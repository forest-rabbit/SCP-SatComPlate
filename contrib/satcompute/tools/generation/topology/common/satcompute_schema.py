"""Canonical SatCompute satellite-node and ISL JSON schema helpers."""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
MAX_DELAY_US = (1 << 63) - 1
MAX_BANDWIDTH_KBPS = UINT64_MAX // 1000
SPEED_OF_LIGHT_M_PER_S = 299792458
DELAY_ROUNDING = "python-round-to-integer-microseconds"
NODE_FIELDS = frozenset(("node_id", "node_type"))
LINK_FIELDS = frozenset(
    (
        "node1_id",
        "node2_id",
        "type",
        "delay",
        "link_bandwidth",
    )
)


class SatComputeSchemaError(ValueError):
    """Raised when canonical SatCompute topology JSON is invalid."""


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
        raise SatComputeSchemaError(
            f"{name} must be an integer in [{minimum}, {maximum}]"
        )
    return value


@dataclass(frozen=True, order=True)
class SatComputeLink:
    """One canonical undirected SatCompute ISL."""

    node1_id: int
    node2_id: int
    delay_us: int
    link_bandwidth_kbps: int

    def __post_init__(self) -> None:
        _require_integer(self.node1_id, "node1_id", 0, UINT32_MAX)
        _require_integer(self.node2_id, "node2_id", 0, UINT32_MAX)
        if self.node1_id >= self.node2_id:
            raise SatComputeSchemaError(
                "ISL endpoints must satisfy node1_id < node2_id"
            )
        _require_integer(self.delay_us, "delay", 0, MAX_DELAY_US)
        _require_integer(
            self.link_bandwidth_kbps,
            "link_bandwidth",
            1,
            MAX_BANDWIDTH_KBPS,
        )

    def payload(self) -> dict[str, int | str]:
        return {
            "node1_id": self.node1_id,
            "node2_id": self.node2_id,
            "type": "sat",
            "delay": self.delay_us,
            "link_bandwidth": self.link_bandwidth_kbps,
        }


def nodes_payload(node_count: int) -> dict[str, list[dict[str, int | str]]]:
    """Build the ordered 0..N-1 satellite-node payload."""
    count = _require_integer(node_count, "node_count", 1, UINT32_MAX)
    return {
        "nodes": [
            {"node_id": node_id, "node_type": "sat"}
            for node_id in range(count)
        ]
    }


def topology_payload(
    links: Iterable[SatComputeLink],
    node_count: int,
) -> dict[str, list[dict[str, int | str]]]:
    """Build sorted canonical links and validate endpoints and uniqueness."""
    count = _require_integer(node_count, "node_count", 1, UINT32_MAX)
    ordered = tuple(sorted(links))
    if not ordered:
        raise SatComputeSchemaError("topology must contain at least one ISL")
    endpoints = set()
    for link in ordered:
        if not isinstance(link, SatComputeLink):
            raise SatComputeSchemaError(
                "topology links must be SatComputeLink values"
            )
        if link.node2_id >= count:
            raise SatComputeSchemaError(
                "ISL endpoint is absent from the paired nodes snapshot"
            )
        key = (link.node1_id, link.node2_id)
        if key in endpoints:
            raise SatComputeSchemaError(f"duplicate ISL endpoints: {key}")
        endpoints.add(key)
    return {"links": [link.payload() for link in ordered]}


def parse_nodes_payload(payload: Any) -> tuple[int, ...]:
    """Validate a closed-world canonical nodes payload."""
    if not isinstance(payload, dict) or set(payload) != {"nodes"}:
        raise SatComputeSchemaError("nodes root must contain only a nodes array")
    nodes = payload["nodes"]
    if not isinstance(nodes, list) or not nodes:
        raise SatComputeSchemaError("nodes must be a non-empty array")
    node_ids = []
    for expected_id, item in enumerate(nodes):
        if not isinstance(item, dict) or set(item) != NODE_FIELDS:
            raise SatComputeSchemaError("invalid node object fields")
        node_id = _require_integer(item["node_id"], "node_id", 0, UINT32_MAX)
        if item["node_type"] != "sat":
            raise SatComputeSchemaError("node_type must be sat")
        if node_id != expected_id:
            raise SatComputeSchemaError(
                "node IDs must be ordered exactly as 0..N-1"
            )
        node_ids.append(node_id)
    return tuple(node_ids)


def parse_topology_payload(
    payload: Any,
    node_ids: tuple[int, ...],
) -> tuple[SatComputeLink, ...]:
    """Validate closed-world links against one canonical node set."""
    if not isinstance(payload, dict) or set(payload) != {"links"}:
        raise SatComputeSchemaError(
            "topology root must contain only a links array"
        )
    links = payload["links"]
    if not isinstance(links, list) or not links:
        raise SatComputeSchemaError("links must be a non-empty array")
    known_nodes = set(node_ids)
    parsed = []
    endpoint_keys = set()
    for item in links:
        if not isinstance(item, dict) or set(item) != LINK_FIELDS:
            raise SatComputeSchemaError("invalid link object fields")
        if item["type"] != "sat":
            raise SatComputeSchemaError("link type must be sat")
        link = SatComputeLink(
            node1_id=item["node1_id"],
            node2_id=item["node2_id"],
            delay_us=item["delay"],
            link_bandwidth_kbps=item["link_bandwidth"],
        )
        key = (link.node1_id, link.node2_id)
        if key in endpoint_keys:
            raise SatComputeSchemaError(f"duplicate ISL endpoints: {key}")
        if link.node1_id not in known_nodes or link.node2_id not in known_nodes:
            raise SatComputeSchemaError(
                "ISL endpoint is absent from the paired nodes snapshot"
            )
        endpoint_keys.add(key)
        parsed.append(link)
    ordered = tuple(parsed)
    if ordered != tuple(sorted(ordered)):
        raise SatComputeSchemaError("links must use canonical sorted order")
    return ordered


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SatComputeSchemaError(f"cannot read JSON {path}: {error}") from error


def satcompute_json_bytes(payload: Any) -> bytes:
    """Serialize readable deterministic SatCompute snapshot JSON."""
    return (
        json.dumps(payload, ensure_ascii=False, indent=2, separators=(",", ": "))
        + "\n"
    ).encode("utf-8")


def distance_delay_us(distance_m: Any) -> int:
    """Convert one-way metres to Python-rounded propagation microseconds."""
    if (
        not isinstance(distance_m, (int, float))
        or isinstance(distance_m, bool)
        or not math.isfinite(distance_m)
        or distance_m < 0.0
    ):
        raise SatComputeSchemaError(
            "distance_m must be a finite non-negative number"
        )
    return int(round(float(distance_m) / SPEED_OF_LIGHT_M_PER_S * 1_000_000))


def validate_delay_parameters(
    delay_mode: str,
    fixed_delay_us: int | None,
) -> None:
    """Validate the shared fixed/distance delay CLI contract."""
    if delay_mode not in ("fixed", "distance"):
        raise SatComputeSchemaError("delay_mode must be fixed or distance")
    if delay_mode == "fixed":
        if fixed_delay_us is None:
            raise SatComputeSchemaError(
                "fixed mode requires fixed_delay_us"
            )
        _require_integer(
            fixed_delay_us,
            "fixed_delay_us",
            0,
            MAX_DELAY_US,
        )
    elif fixed_delay_us is not None:
        raise SatComputeSchemaError(
            "distance mode does not accept fixed_delay_us"
        )


def validate_link_bandwidth_kbps(value: Any) -> int:
    """Validate bandwidth against the C++ kbps-to-bps conversion range."""
    return _require_integer(
        value,
        "link_bandwidth_kbps",
        1,
        MAX_BANDWIDTH_KBPS,
    )
