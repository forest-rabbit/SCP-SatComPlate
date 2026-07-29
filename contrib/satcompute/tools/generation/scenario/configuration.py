#!/usr/bin/env python3
"""Parse the closed-world unified scenario configuration contract."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ..topology.common.configuration import (
    ConstellationConfig,
    ConstellationConfigError,
    parse_config as parse_constellation_config,
)
from ..topology.common.satcompute_schema import (
    SatComputeSchemaError,
    validate_delay_parameters,
    validate_link_bandwidth_kbps,
)


SCHEMA_VERSION = "0.1"
STATIC_MODE = "static"
DYNAMIC_MODE = "dynamic"
TOPOLOGY_MODES = frozenset((STATIC_MODE, DYNAMIC_MODE))
EVEN_PLANE_SLOT = "even-plane-slot"
PLACEMENT_STRATEGIES = frozenset((EVEN_PLANE_SLOT,))
SAFE_TOKEN_REGEX = r"^[A-Za-z0-9][A-Za-z0-9._-]*$"
SAFE_TOKEN_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
UINT64_MAX = (1 << 64) - 1
INT64_MAX = (1 << 63) - 1

ROOT_FIELDS = frozenset(
    ("schema_version", "scenario_name", "constellation", "topology", "compute")
)
CONSTELLATION_FIELD_ORDER = (
    "constellation_name",
    "constellation_pattern",
    "num_orbits",
    "satellites_per_orbit",
    "altitude_km",
    "inclination_deg",
    "phase_diff",
)
CONSTELLATION_FIELDS = frozenset(CONSTELLATION_FIELD_ORDER)
TOPOLOGY_FIELDS = frozenset(
    (
        "mode",
        "schedule",
        "isl_candidate_strategy",
        "seam_enabled",
        "max_isl_distance_m",
        "delay_mode",
        "fixed_delay_us",
        "link_bandwidth_kbps",
    )
)
STATIC_SCHEDULE_FIELDS = frozenset(("snapshot_time_s",))
DYNAMIC_SCHEDULE_FIELDS = frozenset(
    ("start_time_s", "duration_s", "step_s")
)
COMPUTE_FIELDS = frozenset(
    (
        "compute_node_count",
        "placement_strategy",
        "compute_rate_work_units_per_second",
    )
)


class ScenarioConfigError(ValueError):
    """Raised when a unified scenario config violates schema 0.1."""


@dataclass(frozen=True)
class StaticSchedule:
    snapshot_time_s: int

    def input_dict(self) -> dict[str, int]:
        return {"snapshot_time_s": self.snapshot_time_s}


@dataclass(frozen=True)
class DynamicSchedule:
    start_time_s: int
    duration_s: int
    step_s: int

    def input_dict(self) -> dict[str, int]:
        return {
            "start_time_s": self.start_time_s,
            "duration_s": self.duration_s,
            "step_s": self.step_s,
        }


@dataclass(frozen=True)
class ScenarioTopologyConfig:
    mode: str
    schedule: StaticSchedule | DynamicSchedule
    isl_candidate_strategy: str
    seam_enabled: bool
    max_isl_distance_m: int
    delay_mode: str
    fixed_delay_us: int | None
    link_bandwidth_kbps: int

    def input_dict(self) -> dict[str, Any]:
        return {
            "mode": self.mode,
            "schedule": self.schedule.input_dict(),
            "isl_candidate_strategy": self.isl_candidate_strategy,
            "seam_enabled": self.seam_enabled,
            "max_isl_distance_m": self.max_isl_distance_m,
            "delay_mode": self.delay_mode,
            "fixed_delay_us": self.fixed_delay_us,
            "link_bandwidth_kbps": self.link_bandwidth_kbps,
        }


@dataclass(frozen=True)
class ScenarioComputeConfig:
    compute_node_count: int
    placement_strategy: str
    compute_rate_work_units_per_second: int

    def input_dict(self) -> dict[str, Any]:
        return {
            "compute_node_count": self.compute_node_count,
            "placement_strategy": self.placement_strategy,
            "compute_rate_work_units_per_second": (
                self.compute_rate_work_units_per_second
            ),
        }


@dataclass(frozen=True)
class ScenarioConfig:
    schema_version: str
    scenario_name: str
    constellation: ConstellationConfig
    topology: ScenarioTopologyConfig
    compute: ScenarioComputeConfig

    @property
    def total_satellite_count(self) -> int:
        return self.constellation.expected_satellite_count

    def input_dict(self) -> dict[str, Any]:
        constellation = self.constellation.input_dict()
        return {
            "schema_version": self.schema_version,
            "scenario_name": self.scenario_name,
            "constellation": {
                field: constellation[field]
                for field in CONSTELLATION_FIELD_ORDER
            },
            "topology": self.topology.input_dict(),
            "compute": self.compute.input_dict(),
        }


def _require_object(
    value: Any,
    name: str,
    expected_fields: frozenset[str],
) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ScenarioConfigError(f"{name} must be an object")
    actual = frozenset(value)
    if actual != expected_fields:
        raise ScenarioConfigError(
            f"{name} fields differ: "
            f"missing={sorted(expected_fields - actual)}, "
            f"unknown={sorted(actual - expected_fields)}"
        )
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
        raise ScenarioConfigError(
            f"{name} must be an integer in [{minimum}, {maximum}]"
        )
    return value


def _parse_schedule(mode: str, payload: Any) -> StaticSchedule | DynamicSchedule:
    if mode == STATIC_MODE:
        schedule = _require_object(
            payload,
            "topology.schedule",
            STATIC_SCHEDULE_FIELDS,
        )
        return StaticSchedule(
            _require_integer(
                schedule["snapshot_time_s"],
                "topology.schedule.snapshot_time_s",
                0,
                INT64_MAX,
            )
        )

    schedule = _require_object(
        payload,
        "topology.schedule",
        DYNAMIC_SCHEDULE_FIELDS,
    )
    start_time_s = _require_integer(
        schedule["start_time_s"],
        "topology.schedule.start_time_s",
        0,
        0,
    )
    duration_s = _require_integer(
        schedule["duration_s"],
        "topology.schedule.duration_s",
        0,
        INT64_MAX,
    )
    step_s = _require_integer(
        schedule["step_s"],
        "topology.schedule.step_s",
        1,
        INT64_MAX,
    )
    if duration_s % step_s != 0:
        raise ScenarioConfigError(
            "topology.schedule.duration_s must be divisible by step_s"
        )
    return DynamicSchedule(start_time_s, duration_s, step_s)


def parse_config(payload: Any) -> ScenarioConfig:
    """Validate an already-decoded unified scenario configuration."""
    root = _require_object(payload, "scenario config root", ROOT_FIELDS)
    if root["schema_version"] != SCHEMA_VERSION:
        raise ScenarioConfigError(
            f"schema_version must be {SCHEMA_VERSION}"
        )
    scenario_name = root["scenario_name"]
    if (
        not isinstance(scenario_name, str)
        or SAFE_TOKEN_PATTERN.fullmatch(scenario_name) is None
    ):
        raise ScenarioConfigError(
            f"scenario_name must match {SAFE_TOKEN_REGEX}"
        )

    constellation = _require_object(
        root["constellation"],
        "constellation",
        CONSTELLATION_FIELDS,
    )
    topology = _require_object(
        root["topology"],
        "topology",
        TOPOLOGY_FIELDS,
    )
    mode = topology["mode"]
    if not isinstance(mode, str) or mode not in TOPOLOGY_MODES:
        raise ScenarioConfigError(
            "topology.mode must be static or dynamic"
        )
    schedule = _parse_schedule(mode, topology["schedule"])

    try:
        constellation_config = parse_constellation_config(
            {
                "schema_version": SCHEMA_VERSION,
                **constellation,
                "isl_candidate_strategy":
                    topology["isl_candidate_strategy"],
                "seam_enabled": topology["seam_enabled"],
                "max_isl_distance_m": topology["max_isl_distance_m"],
            }
        )
        validate_delay_parameters(
            topology["delay_mode"],
            topology["fixed_delay_us"],
        )
        bandwidth = validate_link_bandwidth_kbps(
            topology["link_bandwidth_kbps"]
        )
    except (ConstellationConfigError, SatComputeSchemaError) as error:
        raise ScenarioConfigError(str(error)) from error

    compute = _require_object(
        root["compute"],
        "compute",
        COMPUTE_FIELDS,
    )
    placement_strategy = compute["placement_strategy"]
    if (
        not isinstance(placement_strategy, str)
        or placement_strategy not in PLACEMENT_STRATEGIES
    ):
        raise ScenarioConfigError(
            "compute.placement_strategy must be even-plane-slot"
        )
    compute_node_count = _require_integer(
        compute["compute_node_count"],
        "compute.compute_node_count",
        1,
        constellation_config.expected_satellite_count,
    )
    compute_rate = _require_integer(
        compute["compute_rate_work_units_per_second"],
        "compute.compute_rate_work_units_per_second",
        1,
        UINT64_MAX,
    )

    return ScenarioConfig(
        schema_version=SCHEMA_VERSION,
        scenario_name=scenario_name,
        constellation=constellation_config,
        topology=ScenarioTopologyConfig(
            mode=mode,
            schedule=schedule,
            isl_candidate_strategy=
                constellation_config.isl_candidate_strategy,
            seam_enabled=constellation_config.seam_enabled,
            max_isl_distance_m=
                constellation_config.max_isl_distance_m,
            delay_mode=topology["delay_mode"],
            fixed_delay_us=topology["fixed_delay_us"],
            link_bandwidth_kbps=bandwidth,
        ),
        compute=ScenarioComputeConfig(
            compute_node_count=compute_node_count,
            placement_strategy=placement_strategy,
            compute_rate_work_units_per_second=compute_rate,
        ),
    )


def load_config(path: Path) -> ScenarioConfig:
    """Read and validate one unified scenario config JSON file."""
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ScenarioConfigError(
            f"cannot read scenario config {path}: {error}"
        ) from error
    return parse_config(payload)
