#!/usr/bin/env python3
"""Generate deterministic Walker Star or Delta TLEs for SatCompute."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from configuration import ConstellationConfig, WALKER_DELTA, WALKER_STAR
from hypatia_adapter import HypatiaAdapter
from mean_motion import mean_motion_rev_per_day
from vendor.hypatia_minimal.tle_generator import (
    MEAN_MOTION_CANONICAL,
    MEAN_MOTION_HYPATIA_LEGACY,
)


NEAR_CIRCULAR_ECCENTRICITY = 0.0000001
ARGUMENT_OF_PERIGEE_DEG = 0.0
EPOCH_UTC = "2000-01-01T00:00:00Z"


@dataclass(frozen=True)
class WalkerSlot:
    """Deterministic orbital placement for one satellite."""

    node_id: int
    orbit_index: int
    slot_index: int
    raan_deg: float
    mean_anomaly_deg: float


def walker_slots(config: ConstellationConfig) -> tuple[WalkerSlot, ...]:
    """Resolve node IDs, RAAN values, and mean anomalies."""
    slots = []
    for orbit_index in range(config.num_orbits):
        raan_deg = orbit_index * config.raan_step_deg
        orbit_phase_deg = (
            config.phase_offset_deg if orbit_index % 2 == 1 else 0.0
        )
        for slot_index in range(config.satellites_per_orbit):
            slots.append(
                WalkerSlot(
                    node_id=(
                        orbit_index * config.satellites_per_orbit + slot_index
                    ),
                    orbit_index=orbit_index,
                    slot_index=slot_index,
                    raan_deg=raan_deg,
                    mean_anomaly_deg=(
                        slot_index * config.slot_spacing_deg + orbit_phase_deg
                    ),
                )
            )
    return tuple(slots)


def generate_walker_tles(
    output: Path,
    config: ConstellationConfig,
    adapter: HypatiaAdapter,
) -> None:
    """Write one deterministic TLE set using the selected Walker pattern."""
    if config.constellation_pattern not in (WALKER_STAR, WALKER_DELTA):
        raise ValueError(
            f"unsupported constellation pattern: {config.constellation_pattern}"
        )
    adapter.generate_tles(
        output,
        constellation_name=config.constellation_name,
        num_orbits=config.num_orbits,
        satellites_per_orbit=config.satellites_per_orbit,
        phase_diff=config.phase_diff,
        inclination_deg=config.inclination_deg,
        eccentricity=NEAR_CIRCULAR_ECCENTRICITY,
        argument_of_perigee_deg=ARGUMENT_OF_PERIGEE_DEG,
        mean_motion_rev_per_day=mean_motion_rev_per_day(config.altitude_km),
        raan_span_deg=config.raan_span_deg,
        mean_motion_compatibility=(
            MEAN_MOTION_CANONICAL
            if config.constellation_pattern == WALKER_STAR
            else MEAN_MOTION_HYPATIA_LEGACY
        ),
    )
