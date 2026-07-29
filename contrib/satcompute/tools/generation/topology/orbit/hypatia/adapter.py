#!/usr/bin/env python3
"""Narrow adapter for the vendored Hypatia orbit-generation functions."""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

from astropy.time import TimeDelta

from .vendor.hypatia_minimal.coordinates import geodetic2cartesian
from .vendor.hypatia_minimal.tle_generator import (
    MEAN_MOTION_HYPATIA_LEGACY,
    generate_tles_from_scratch_with_sgp,
)
from .vendor.hypatia_minimal.tle_reader import read_tles


TOOL_DIR = Path(__file__).resolve().parent
VENDOR_DIR = TOOL_DIR / "vendor" / "hypatia_minimal"
DEFAULT_ORIGIN = VENDOR_DIR / "ORIGIN.json"


class HypatiaAdapterError(RuntimeError):
    """Raised when the vendored Hypatia orbit interface cannot be used."""


def load_origin(path: Path) -> tuple[str, str, str]:
    """Load and validate the local third-party provenance contract."""
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise HypatiaAdapterError(
            f"cannot read vendored Hypatia origin {path}: {error}"
        ) from error
    repository = payload.get("repository")
    commit = payload.get("commit")
    component = payload.get("component")
    if not isinstance(repository, str) or not repository:
        raise HypatiaAdapterError(
            "vendored Hypatia repository must be a non-empty string"
        )
    if (
        not isinstance(commit, str)
        or len(commit) != 40
        or any(character not in "0123456789abcdef" for character in commit)
    ):
        raise HypatiaAdapterError(
            "vendored Hypatia commit must be a full lowercase SHA"
        )
    if component != "satgenpy":
        raise HypatiaAdapterError(
            "vendored Hypatia component must be satgenpy"
        )
    return repository, commit, component


class HypatiaAdapter:
    """Expose only TLE generation, TLE reading, and satellite positions."""

    def __init__(self, origin_path: Path = DEFAULT_ORIGIN) -> None:
        repository, commit, component = load_origin(origin_path.resolve())
        self.repository = repository
        self.commit = commit
        self.component = component
        self.origin_path = origin_path.resolve()

    def generate_tles(
        self,
        output: Path,
        *,
        constellation_name: str,
        num_orbits: int,
        satellites_per_orbit: int,
        phase_diff: bool,
        inclination_deg: float,
        eccentricity: float,
        argument_of_perigee_deg: float,
        mean_motion_rev_per_day: float,
        raan_span_deg: float = 360.0,
        mean_motion_compatibility: str = MEAN_MOTION_HYPATIA_LEGACY,
    ) -> None:
        output.parent.mkdir(parents=True, exist_ok=True)
        generate_tles_from_scratch_with_sgp(
            str(output),
            constellation_name,
            num_orbits,
            satellites_per_orbit,
            phase_diff,
            inclination_deg,
            eccentricity,
            argument_of_perigee_deg,
            mean_motion_rev_per_day,
            raan_span_degree=raan_span_deg,
            mean_motion_compatibility=mean_motion_compatibility,
        )

    def read_tles(self, path: Path) -> dict[str, Any]:
        return read_tles(str(path))

    def satellite_position_at(
        self,
        satellite: Any,
        epoch: Any,
        time_since_epoch_s: float,
    ) -> tuple[float, float, float]:
        if not math.isfinite(time_since_epoch_s) or time_since_epoch_s < 0.0:
            raise HypatiaAdapterError(
                "time_since_epoch_s must be a finite non-negative number"
            )
        current_time = epoch + TimeDelta(time_since_epoch_s, format="sec")
        satellite.compute(str(current_time), epoch=str(epoch))
        latitude_deg = math.degrees(float(satellite.sublat))
        longitude_deg = math.degrees(float(satellite.sublong))
        elevation_m = float(satellite.elevation)
        position = geodetic2cartesian(
            latitude_deg,
            longitude_deg,
            elevation_m,
        )
        if not all(math.isfinite(value) for value in position):
            raise HypatiaAdapterError(
                "Hypatia returned a non-finite satellite position"
            )
        return position
