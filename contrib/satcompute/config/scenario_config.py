"""Parse the closed-world SatCompute scenario 0.2 contract."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path
from typing import Any


SCHEMA_VERSION = "0.2"
NANOSECONDS_PER_SECOND = Decimal(1_000_000_000)
INT64_MAX = (1 << 63) - 1
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
MAX_SATELLITES = 99_999
SAFE_TOKEN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")

ROOT_FIELDS = frozenset(
    {
        "schema_version",
        "scenario_name",
        "simulation",
        "constellation",
        "network",
        "routing",
        "workloads",
        "trace_export",
        "randomness",
    }
)
SIMULATION_FIELDS = frozenset({"start_time_s", "duration_s"})
CONSTELLATION_FIELDS = frozenset(
    {
        "orbit_provider",
        "constellation_name",
        "constellation_pattern",
        "num_orbits",
        "satellites_per_orbit",
        "altitude_m",
        "inclination_deg",
        "phase_diff",
        "orbit_epoch_offset_s",
    }
)
NETWORK_FIELDS = frozenset(
    {
        "topology_source",
        "replay_directory",
        "isl_candidate_strategy",
        "seam_enabled",
        "max_isl_distance_m",
        "delay_mode",
        "fixed_delay_us",
        "network_update_interval_s",
        "link_bandwidth_bps",
        "isl_mtu_bytes",
        "isl_queue_bytes",
        "receiver_rcv_buf_bytes",
    }
)
ROUTING_FIELDS = frozenset({"mode", "hash_seed", "recompute_policy"})
WORKLOAD_FIELDS = frozenset(
    {
        "transfer_trace",
        "compute_profile",
        "task_trace",
        "transfer_chunk_mode",
        "transfer_payload_bytes",
        "task_completion_policy",
    }
)
TRACE_EXPORT_FIELDS = frozenset(
    {"enabled", "interval_s", "include_final_state", "format"}
)
RANDOMNESS_FIELDS = frozenset({"seed", "run", "stream_start"})

ROUTING_MODES = frozenset(
    {
        "global-first",
        "global-hash-per-flow",
        "global-hrw-per-flow",
        "global-size-aware-hrw",
        "global-capacity-aware-hrw",
    }
)


class ScenarioConfigError(ValueError):
    """Raised when a scenario violates contract 0.2."""


@dataclass(frozen=True)
class SimulationConfig:
    """Resolved simulation clock in integer nanoseconds."""

    start_time_ns: int
    duration_ns: int


@dataclass(frozen=True)
class ConstellationConfig:
    """Resolved circular-orbit shell."""

    orbit_provider: str
    constellation_name: str
    constellation_pattern: str
    num_orbits: int
    satellites_per_orbit: int
    altitude_m: Decimal
    inclination_deg: Decimal
    phase_diff: bool
    orbit_epoch_offset_ns: int

    @property
    def satellite_count(self) -> int:
        """Return the stable external satellite ID domain size."""

        return self.num_orbits * self.satellites_per_orbit


@dataclass(frozen=True)
class NetworkConfig:
    """Resolved ISL policy and resources."""

    topology_source: str
    replay_directory: Path | None
    isl_candidate_strategy: str
    seam_enabled: bool
    max_isl_distance_m: Decimal
    delay_mode: str
    fixed_delay_us: int | None
    network_update_interval_ns: int
    link_bandwidth_bps: int
    isl_mtu_bytes: int
    isl_queue_bytes: int
    receiver_rcv_buf_bytes: int


@dataclass(frozen=True)
class RoutingConfig:
    """Resolved deterministic IPv4 routing configuration."""

    mode: str
    hash_seed: int
    recompute_policy: str


@dataclass(frozen=True)
class WorkloadConfig:
    """Resolved workload and compute-resource references."""

    transfer_trace: Path | None
    compute_profile: Path | None
    task_trace: Path | None
    transfer_chunk_mode: str
    transfer_payload_bytes: int
    task_completion_policy: str


@dataclass(frozen=True)
class TraceExportConfig:
    """Resolved deterministic offline trace controls."""

    enabled: bool
    interval_ns: int
    include_final_state: bool
    format: str


@dataclass(frozen=True)
class RandomnessConfig:
    """Resolved ns-3 random-stream identity."""

    seed: int
    run: int
    stream_start: int


@dataclass(frozen=True)
class ScenarioConfig:
    """Fully validated semantic scenario input."""

    schema_version: str
    scenario_name: str
    source_path: Path
    simulation: SimulationConfig
    constellation: ConstellationConfig
    network: NetworkConfig
    routing: RoutingConfig
    workloads: WorkloadConfig
    trace_export: TraceExportConfig
    randomness: RandomnessConfig


def _require_object(
    value: Any,
    name: str,
    fields: frozenset[str],
) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ScenarioConfigError(f"{name} must be an object")
    actual = frozenset(value)
    if actual != fields:
        raise ScenarioConfigError(
            f"{name} fields differ: missing={sorted(fields - actual)}, "
            f"unknown={sorted(actual - fields)}"
        )
    return value


def _require_string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value:
        raise ScenarioConfigError(f"{name} must be a non-empty string")
    return value


def _require_token(value: Any, name: str) -> str:
    token = _require_string(value, name)
    if SAFE_TOKEN.fullmatch(token) is None:
        raise ScenarioConfigError(
            f"{name} must match ^[A-Za-z0-9][A-Za-z0-9._-]*$"
        )
    return token


def _require_enum(value: Any, name: str, allowed: frozenset[str]) -> str:
    if not isinstance(value, str) or value not in allowed:
        raise ScenarioConfigError(
            f"{name} must be one of {sorted(allowed)}"
        )
    return value


def _require_bool(value: Any, name: str) -> bool:
    if not isinstance(value, bool):
        raise ScenarioConfigError(f"{name} must be a boolean")
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


def _require_decimal(
    value: Any,
    name: str,
    *,
    minimum: Decimal,
    maximum: Decimal | None = None,
    minimum_exclusive: bool = False,
    maximum_exclusive: bool = False,
) -> Decimal:
    if isinstance(value, bool) or not isinstance(value, (int, Decimal)):
        raise ScenarioConfigError(f"{name} must be a JSON number")
    number = Decimal(value)
    if not number.is_finite():
        raise ScenarioConfigError(f"{name} must be finite")
    below_minimum = number <= minimum if minimum_exclusive else number < minimum
    if below_minimum:
        relation = ">" if minimum_exclusive else ">="
        raise ScenarioConfigError(f"{name} must be {relation} {minimum}")
    if maximum is not None:
        above_maximum = (
            number >= maximum if maximum_exclusive else number > maximum
        )
        if above_maximum:
            relation = "<" if maximum_exclusive else "<="
            raise ScenarioConfigError(f"{name} must be {relation} {maximum}")
    return number


def seconds_to_nanoseconds(
    value: Any,
    name: str,
    *,
    positive: bool = False,
) -> int:
    """Convert an exact JSON decimal second value to signed 64-bit ns."""

    seconds = _require_decimal(
        value,
        name,
        minimum=Decimal(0),
        minimum_exclusive=positive,
    )
    scaled = seconds * NANOSECONDS_PER_SECOND
    if scaled != scaled.to_integral_value():
        raise ScenarioConfigError(
            f"{name} has precision finer than one nanosecond"
        )
    nanoseconds = int(scaled)
    if nanoseconds > INT64_MAX:
        raise ScenarioConfigError(
            f"{name} exceeds signed 64-bit nanosecond range"
        )
    return nanoseconds


def _optional_path(value: Any, name: str, base_directory: Path) -> Path | None:
    if value is None:
        return None
    path_text = _require_string(value, name)
    path = Path(path_text)
    if not path.is_absolute():
        path = base_directory / path
    return path.resolve(strict=False)


def _parse_simulation(payload: Any) -> SimulationConfig:
    data = _require_object(payload, "simulation", SIMULATION_FIELDS)
    start_time_ns = seconds_to_nanoseconds(
        data["start_time_s"], "simulation.start_time_s"
    )
    if start_time_ns != 0:
        raise ScenarioConfigError("simulation.start_time_s must be 0")
    return SimulationConfig(
        start_time_ns=start_time_ns,
        duration_ns=seconds_to_nanoseconds(
            data["duration_s"], "simulation.duration_s", positive=True
        ),
    )


def _parse_constellation(payload: Any) -> ConstellationConfig:
    data = _require_object(payload, "constellation", CONSTELLATION_FIELDS)
    num_orbits = _require_integer(
        data["num_orbits"], "constellation.num_orbits", 1, MAX_SATELLITES
    )
    satellites_per_orbit = _require_integer(
        data["satellites_per_orbit"],
        "constellation.satellites_per_orbit",
        1,
        MAX_SATELLITES,
    )
    if num_orbits * satellites_per_orbit > MAX_SATELLITES:
        raise ScenarioConfigError(
            "constellation total satellite count must not exceed 99999"
        )
    return ConstellationConfig(
        orbit_provider=_require_enum(
            data["orbit_provider"],
            "constellation.orbit_provider",
            frozenset({"ns3-circular", "json-replay"}),
        ),
        constellation_name=_require_token(
            data["constellation_name"], "constellation.constellation_name"
        ),
        constellation_pattern=_require_enum(
            data["constellation_pattern"],
            "constellation.constellation_pattern",
            frozenset({"walker-star", "walker-delta"}),
        ),
        num_orbits=num_orbits,
        satellites_per_orbit=satellites_per_orbit,
        altitude_m=_require_decimal(
            data["altitude_m"],
            "constellation.altitude_m",
            minimum=Decimal(0),
            minimum_exclusive=True,
        ),
        inclination_deg=_require_decimal(
            data["inclination_deg"],
            "constellation.inclination_deg",
            minimum=Decimal(0),
            maximum=Decimal(180),
            maximum_exclusive=True,
        ),
        phase_diff=_require_bool(
            data["phase_diff"], "constellation.phase_diff"
        ),
        orbit_epoch_offset_ns=seconds_to_nanoseconds(
            data["orbit_epoch_offset_s"],
            "constellation.orbit_epoch_offset_s",
        ),
    )


def _parse_network(payload: Any, base_directory: Path) -> NetworkConfig:
    data = _require_object(payload, "network", NETWORK_FIELDS)
    topology_source = _require_enum(
        data["topology_source"],
        "network.topology_source",
        frozenset({"online", "json-replay"}),
    )
    replay_directory = _optional_path(
        data["replay_directory"],
        "network.replay_directory",
        base_directory,
    )
    if topology_source == "online" and replay_directory is not None:
        raise ScenarioConfigError(
            "network.replay_directory must be null for online topology"
        )
    if topology_source == "json-replay" and replay_directory is None:
        raise ScenarioConfigError(
            "network.replay_directory is required for json-replay topology"
        )

    delay_mode = _require_enum(
        data["delay_mode"],
        "network.delay_mode",
        frozenset({"fixed", "distance"}),
    )
    fixed_delay = data["fixed_delay_us"]
    if delay_mode == "fixed":
        fixed_delay_us = _require_integer(
            fixed_delay, "network.fixed_delay_us", 1, UINT64_MAX
        )
    else:
        if fixed_delay is not None:
            raise ScenarioConfigError(
                "network.fixed_delay_us must be null for distance mode"
            )
        fixed_delay_us = None

    return NetworkConfig(
        topology_source=topology_source,
        replay_directory=replay_directory,
        isl_candidate_strategy=_require_enum(
            data["isl_candidate_strategy"],
            "network.isl_candidate_strategy",
            frozenset({"plus-grid"}),
        ),
        seam_enabled=_require_bool(
            data["seam_enabled"], "network.seam_enabled"
        ),
        max_isl_distance_m=_require_decimal(
            data["max_isl_distance_m"],
            "network.max_isl_distance_m",
            minimum=Decimal(0),
            minimum_exclusive=True,
        ),
        delay_mode=delay_mode,
        fixed_delay_us=fixed_delay_us,
        network_update_interval_ns=seconds_to_nanoseconds(
            data["network_update_interval_s"],
            "network.network_update_interval_s",
            positive=True,
        ),
        link_bandwidth_bps=_require_integer(
            data["link_bandwidth_bps"],
            "network.link_bandwidth_bps",
            1,
            UINT64_MAX,
        ),
        isl_mtu_bytes=_require_integer(
            data["isl_mtu_bytes"], "network.isl_mtu_bytes", 68, 65535
        ),
        isl_queue_bytes=_require_integer(
            data["isl_queue_bytes"],
            "network.isl_queue_bytes",
            1,
            UINT32_MAX,
        ),
        receiver_rcv_buf_bytes=_require_integer(
            data["receiver_rcv_buf_bytes"],
            "network.receiver_rcv_buf_bytes",
            1,
            UINT32_MAX,
        ),
    )


def _parse_routing(payload: Any) -> RoutingConfig:
    data = _require_object(payload, "routing", ROUTING_FIELDS)
    policy = _require_string(data["recompute_policy"], "routing.recompute_policy")
    if policy != "on-topology-change":
        raise ScenarioConfigError(
            "routing.recompute_policy must be on-topology-change"
        )
    return RoutingConfig(
        mode=_require_enum(data["mode"], "routing.mode", ROUTING_MODES),
        hash_seed=_require_integer(
            data["hash_seed"], "routing.hash_seed", 0, UINT64_MAX
        ),
        recompute_policy=policy,
    )


def _parse_workloads(
    payload: Any,
    base_directory: Path,
    network: NetworkConfig,
) -> WorkloadConfig:
    data = _require_object(payload, "workloads", WORKLOAD_FIELDS)
    transfer_trace = _optional_path(
        data["transfer_trace"], "workloads.transfer_trace", base_directory
    )
    compute_profile = _optional_path(
        data["compute_profile"], "workloads.compute_profile", base_directory
    )
    task_trace = _optional_path(
        data["task_trace"], "workloads.task_trace", base_directory
    )
    if (compute_profile is None) != (task_trace is None):
        raise ScenarioConfigError(
            "workloads.compute_profile and task_trace must be provided together"
        )
    if transfer_trace is not None and compute_profile is not None:
        raise ScenarioConfigError(
            "workloads.transfer_trace cannot be mixed with task inputs"
        )

    chunk_mode = _require_enum(
        data["transfer_chunk_mode"],
        "workloads.transfer_chunk_mode",
        frozenset({"fixed", "size-aware"}),
    )
    payload_bytes = _require_integer(
        data["transfer_payload_bytes"],
        "workloads.transfer_payload_bytes",
        1,
        65507,
    )
    if chunk_mode == "fixed" and payload_bytes + 28 > network.isl_mtu_bytes:
        raise ScenarioConfigError(
            "workloads.transfer_payload_bytes plus 28-byte UDP/IPv4 header "
            "must not exceed network.isl_mtu_bytes"
        )
    if chunk_mode == "size-aware" and network.isl_mtu_bytes < 64028:
        raise ScenarioConfigError(
            "network.isl_mtu_bytes must be at least 64028 for size-aware chunking"
        )

    return WorkloadConfig(
        transfer_trace=transfer_trace,
        compute_profile=compute_profile,
        task_trace=task_trace,
        transfer_chunk_mode=chunk_mode,
        transfer_payload_bytes=payload_bytes,
        task_completion_policy=_require_enum(
            data["task_completion_policy"],
            "workloads.task_completion_policy",
            frozenset({"strict", "report"}),
        ),
    )


def _parse_trace_export(payload: Any) -> TraceExportConfig:
    data = _require_object(payload, "trace_export", TRACE_EXPORT_FIELDS)
    output_format = _require_string(data["format"], "trace_export.format")
    if output_format != "json-slices":
        raise ScenarioConfigError("trace_export.format must be json-slices")
    return TraceExportConfig(
        enabled=_require_bool(data["enabled"], "trace_export.enabled"),
        interval_ns=seconds_to_nanoseconds(
            data["interval_s"], "trace_export.interval_s", positive=True
        ),
        include_final_state=_require_bool(
            data["include_final_state"],
            "trace_export.include_final_state",
        ),
        format=output_format,
    )


def _parse_randomness(payload: Any) -> RandomnessConfig:
    data = _require_object(payload, "randomness", RANDOMNESS_FIELDS)
    return RandomnessConfig(
        seed=_require_integer(data["seed"], "randomness.seed", 1, UINT32_MAX),
        run=_require_integer(data["run"], "randomness.run", 0, UINT64_MAX),
        stream_start=_require_integer(
            data["stream_start"], "randomness.stream_start", 0, INT64_MAX
        ),
    )


def parse_scenario(
    payload: Any,
    *,
    source_path: Path = Path("scenario.json"),
) -> ScenarioConfig:
    """Validate an already decoded scenario payload."""

    source_path = Path(source_path).resolve(strict=False)
    root = _require_object(payload, "scenario root", ROOT_FIELDS)
    if root["schema_version"] != SCHEMA_VERSION:
        raise ScenarioConfigError(
            f"schema_version must be {SCHEMA_VERSION}"
        )
    simulation = _parse_simulation(root["simulation"])
    constellation = _parse_constellation(root["constellation"])
    if constellation.orbit_epoch_offset_ns > INT64_MAX - simulation.duration_ns:
        raise ScenarioConfigError(
            "constellation.orbit_epoch_offset_s plus simulation.duration_s "
            "exceeds signed 64-bit nanosecond range"
        )
    network = _parse_network(root["network"], source_path.parent)
    if (
        constellation.orbit_provider == "ns3-circular"
        and network.topology_source != "online"
    ) or (
        constellation.orbit_provider == "json-replay"
        and network.topology_source != "json-replay"
    ):
        raise ScenarioConfigError(
            "constellation.orbit_provider and network.topology_source disagree"
        )
    return ScenarioConfig(
        schema_version=SCHEMA_VERSION,
        scenario_name=_require_token(root["scenario_name"], "scenario_name"),
        source_path=source_path,
        simulation=simulation,
        constellation=constellation,
        network=network,
        routing=_parse_routing(root["routing"]),
        workloads=_parse_workloads(
            root["workloads"], source_path.parent, network
        ),
        trace_export=_parse_trace_export(root["trace_export"]),
        randomness=_parse_randomness(root["randomness"]),
    )


def _reject_nonstandard_number(token: str) -> None:
    raise ScenarioConfigError(f"non-standard JSON number is forbidden: {token}")


def load_scenario(path: Path) -> ScenarioConfig:
    """Load a scenario while preserving decimal seconds exactly."""

    source_path = Path(path).resolve(strict=False)
    try:
        with source_path.open("r", encoding="utf-8") as source:
            payload = json.load(
                source,
                parse_float=Decimal,
                parse_constant=_reject_nonstandard_number,
            )
    except (OSError, json.JSONDecodeError) as error:
        raise ScenarioConfigError(
            f"cannot load scenario {source_path}: {error}"
        ) from error
    return parse_scenario(payload, source_path=source_path)
