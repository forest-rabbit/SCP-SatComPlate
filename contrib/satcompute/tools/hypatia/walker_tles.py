#!/usr/bin/env python3
"""Generate deterministic Walker Star or Delta TLEs for SatCompute."""

# The Star TLE serialization is a narrow adaptation of Hypatia's
# satgen/tles/generate_tles_from_scratch.py:
#
# Copyright (c) 2020 ETH Zurich
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

from sgp4.api import Satrec, WGS72, jday
from sgp4.exporter import export_tle

from configuration import ConstellationConfig, WALKER_DELTA, WALKER_STAR
from hypatia_adapter import HypatiaAdapter
from mean_motion import mean_motion_rev_per_day


NEAR_CIRCULAR_ECCENTRICITY = 0.0000001
ARGUMENT_OF_PERIGEE_DEG = 0.0
EPOCH_UTC = "2000-01-01T00:00:00Z"
MINUTES_PER_DAY = 1440.0
SGP4_EPOCH_OFFSET_DAYS = 2433281.5


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
    output.parent.mkdir(parents=True, exist_ok=True)
    mean_motion = mean_motion_rev_per_day(config.altitude_km)
    if config.constellation_pattern == WALKER_DELTA:
        adapter.generate_tles(
            output,
            constellation_name=config.constellation_name,
            num_orbits=config.num_orbits,
            satellites_per_orbit=config.satellites_per_orbit,
            phase_diff=config.phase_diff,
            inclination_deg=config.inclination_deg,
            eccentricity=NEAR_CIRCULAR_ECCENTRICITY,
            argument_of_perigee_deg=ARGUMENT_OF_PERIGEE_DEG,
            mean_motion_rev_per_day=mean_motion,
        )
        return
    if config.constellation_pattern != WALKER_STAR:
        raise ValueError(
            f"unsupported constellation pattern: {config.constellation_pattern}"
        )
    _generate_star_tles(output, config, mean_motion)


def _generate_star_tles(
    output: Path,
    config: ConstellationConfig,
    mean_motion: float,
) -> None:
    """Generate the 180-degree RAAN variant missing from frozen Hypatia."""
    epoch_jd, epoch_fraction = jday(2000, 1, 1, 0, 0, 0)
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(f"{config.num_orbits} {config.satellites_per_orbit}\n")
        for slot in walker_slots(config):
            satellite = Satrec()
            satellite.sgp4init(
                WGS72,
                "i",
                slot.node_id + 1,
                epoch_jd + epoch_fraction - SGP4_EPOCH_OFFSET_DAYS,
                0.0,
                0.0,
                0.0,
                NEAR_CIRCULAR_ECCENTRICITY,
                math.radians(ARGUMENT_OF_PERIGEE_DEG),
                math.radians(config.inclination_deg),
                math.radians(slot.mean_anomaly_deg),
                mean_motion * 2.0 * math.pi / MINUTES_PER_DAY,
                math.radians(slot.raan_deg),
            )
            line1, line2 = export_tle(satellite)
            line1 = line1[:7] + "U 00000ABC 00001.00000000 " + line1[33:]
            line1 = line1[:68] + str(_tle_checksum(line1[:68]))
            _require_valid_tle_line(line1, "line 1")
            _require_valid_tle_line(line2, "line 2")
            stream.write(f"{config.constellation_name} {slot.node_id}\n")
            stream.write(f"{line1}\n")
            stream.write(f"{line2}\n")


def _tle_checksum(line_without_checksum: str) -> int:
    if len(line_without_checksum) != 68:
        raise ValueError("TLE line without checksum must have 68 characters")
    return sum(
        int(character) if character.isdigit() else character == "-"
        for character in line_without_checksum
    ) % 10


def _require_valid_tle_line(line: str, name: str) -> None:
    if (
        len(line) != 69
        or not line[68].isdigit()
        or _tle_checksum(line[:68]) != int(line[68])
    ):
        raise ValueError(f"TLE {name} checksum failed")
