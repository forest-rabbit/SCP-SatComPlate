#!/usr/bin/env python3
"""Parse the closed-world SatCompute constellation configuration contract."""

from __future__ import annotations

import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


SCHEMA_VERSION = "0.1"
WALKER_STAR = "walker-star"
WALKER_DELTA = "walker-delta"
CONSTELLATION_PATTERNS = frozenset((WALKER_STAR, WALKER_DELTA))
MAX_TLE_SATELLITES = 99999
CONFIG_FIELDS = frozenset(
    (
        "schema_version",
        "constellation_name",
        "constellation_pattern",
        "num_orbits",
        "satellites_per_orbit",
        "altitude_km",
        "inclination_deg",
        "phase_diff",
        "seam_enabled",
        "max_isl_distance_m",
    )
)


class ConstellationConfigError(ValueError):
    """Raised when a constellation configuration violates schema 0.1."""


@dataclass(frozen=True)
class ConstellationConfig:
    """Validated physical and candidate-graph inputs for one constellation."""

    schema_version: str
    constellation_name: str
    constellation_pattern: str
    num_orbits: int
    satellites_per_orbit: int
    altitude_km: float
    inclination_deg: float
    phase_diff: bool
    seam_enabled: bool
    max_isl_distance_m: int

    @property
    def expected_satellite_count(self) -> int:
        return self.num_orbits * self.satellites_per_orbit

    @property
    def raan_span_deg(self) -> float:
        return 180.0 if self.constellation_pattern == WALKER_STAR else 360.0

    @property
    def raan_step_deg(self) -> float:
        return self.raan_span_deg / self.num_orbits

    @property
    def slot_spacing_deg(self) -> float:
        return 360.0 / self.satellites_per_orbit

    @property
    def phase_offset_deg(self) -> float:
        return 180.0 / self.satellites_per_orbit if self.phase_diff else 0.0

    @property
    def phase_scheme(self) -> str:
        return "alternating-half-slot" if self.phase_diff else "aligned"

    def input_dict(self) -> dict[str, Any]:
        """Return the validated input contract in deterministic field order."""
        return asdict(self)


def _require_integer(
    value: Any,
    field: str,
    minimum: int,
    maximum: int,
) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or not minimum <= value <= maximum
    ):
        raise ConstellationConfigError(
            f"{field} must be an integer in [{minimum}, {maximum}]"
        )
    return value


def _require_finite_number(
    value: Any,
    field: str,
    minimum: float,
    maximum: float | None = None,
) -> float:
    if (
        not isinstance(value, (int, float))
        or isinstance(value, bool)
        or not math.isfinite(value)
        or value < minimum
        or (maximum is not None and value > maximum)
    ):
        range_text = (
            f"[{minimum}, {maximum}]" if maximum is not None else f">= {minimum}"
        )
        raise ConstellationConfigError(
            f"{field} must be a finite number in {range_text}"
        )
    return float(value)


def parse_config(payload: Any) -> ConstellationConfig:
    """Validate an already-decoded constellation configuration."""
    if not isinstance(payload, dict):
        raise ConstellationConfigError("constellation config root must be an object")
    actual_fields = frozenset(payload)
    if actual_fields != CONFIG_FIELDS:
        missing = sorted(CONFIG_FIELDS - actual_fields)
        unknown = sorted(actual_fields - CONFIG_FIELDS)
        raise ConstellationConfigError(
            f"constellation config fields differ: missing={missing}, "
            f"unknown={unknown}"
        )
    if payload["schema_version"] != SCHEMA_VERSION:
        raise ConstellationConfigError(
            f"schema_version must be {SCHEMA_VERSION}"
        )

    name = payload["constellation_name"]
    if not isinstance(name, str) or not name.strip():
        raise ConstellationConfigError(
            "constellation_name must be a non-empty string"
        )
    pattern = payload["constellation_pattern"]
    if not isinstance(pattern, str) or pattern not in CONSTELLATION_PATTERNS:
        raise ConstellationConfigError(
            "constellation_pattern must be walker-star or walker-delta"
        )

    num_orbits = _require_integer(
        payload["num_orbits"],
        "num_orbits",
        1,
        MAX_TLE_SATELLITES,
    )
    satellites_per_orbit = _require_integer(
        payload["satellites_per_orbit"],
        "satellites_per_orbit",
        1,
        MAX_TLE_SATELLITES,
    )
    if num_orbits * satellites_per_orbit > MAX_TLE_SATELLITES:
        raise ConstellationConfigError(
            f"satellite count must not exceed {MAX_TLE_SATELLITES}"
        )

    phase_diff = payload["phase_diff"]
    seam_enabled = payload["seam_enabled"]
    if not isinstance(phase_diff, bool):
        raise ConstellationConfigError("phase_diff must be a boolean")
    if not isinstance(seam_enabled, bool):
        raise ConstellationConfigError("seam_enabled must be a boolean")

    return ConstellationConfig(
        schema_version=SCHEMA_VERSION,
        constellation_name=name,
        constellation_pattern=pattern,
        num_orbits=num_orbits,
        satellites_per_orbit=satellites_per_orbit,
        altitude_km=_require_finite_number(
            payload["altitude_km"],
            "altitude_km",
            0.0,
        ),
        inclination_deg=_require_finite_number(
            payload["inclination_deg"],
            "inclination_deg",
            0.0,
            180.0,
        ),
        phase_diff=phase_diff,
        seam_enabled=seam_enabled,
        max_isl_distance_m=_require_integer(
            payload["max_isl_distance_m"],
            "max_isl_distance_m",
            1,
            (1 << 63) - 1,
        ),
    )


def load_config(path: Path) -> ConstellationConfig:
    """Read and validate a constellation configuration JSON file."""
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ConstellationConfigError(
            f"cannot read constellation config {path}: {error}"
        ) from error
    return parse_config(payload)
