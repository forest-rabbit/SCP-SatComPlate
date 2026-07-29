#!/usr/bin/env python3
"""Derive near-circular orbital motion from altitude using WGS72."""

from __future__ import annotations

import math
from typing import Any


WGS72_EARTH_RADIUS_KM = 6378.135
WGS72_MU_KM3_S2 = 398600.8
SECONDS_PER_DAY = 86400.0
MINUTES_PER_DAY = 1440.0


def _finite_non_negative(value: Any, name: str) -> float:
    if (
        not isinstance(value, (int, float))
        or isinstance(value, bool)
        or not math.isfinite(value)
        or value < 0.0
    ):
        raise ValueError(f"{name} must be a finite non-negative number")
    return float(value)


def mean_motion_rev_per_day(altitude_km: float) -> float:
    """Return circular-orbit mean motion in revolutions per day."""
    altitude = _finite_non_negative(altitude_km, "altitude_km")
    semi_major_axis_km = WGS72_EARTH_RADIUS_KM + altitude
    radians_per_second = math.sqrt(
        WGS72_MU_KM3_S2 / semi_major_axis_km**3
    )
    return radians_per_second * SECONDS_PER_DAY / (2.0 * math.pi)


def orbital_period_minutes(mean_motion: float) -> float:
    """Return orbital period in minutes for a positive mean motion."""
    value = _finite_non_negative(mean_motion, "mean_motion_rev_per_day")
    if value == 0.0:
        raise ValueError("mean_motion_rev_per_day must be positive")
    return MINUTES_PER_DAY / value
