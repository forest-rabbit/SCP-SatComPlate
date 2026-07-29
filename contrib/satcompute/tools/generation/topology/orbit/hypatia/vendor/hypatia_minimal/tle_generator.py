#!/usr/bin/env python3
"""Adapted minimal Hypatia/satgenpy TLE generator."""

# The MIT License (MIT)
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
from pathlib import Path

from sgp4.api import Satrec, WGS72, jday
from sgp4.exporter import export_tle


MINUTES_PER_DAY = 1440.0
SGP4_EPOCH_OFFSET_DAYS = 2433281.5
UPSTREAM_MEAN_MOTION_DIVISOR = 13750.9870831397
MEAN_MOTION_CANONICAL = "canonical"
MEAN_MOTION_HYPATIA_LEGACY = "hypatia-legacy"


def _mean_motion_rad_per_min_canonical(
    mean_motion_rev_per_day: float,
) -> float:
    """Convert rev/day using the direct canonical conversion."""
    return mean_motion_rev_per_day * 2.0 * math.pi / MINUTES_PER_DAY


def _mean_motion_rad_per_min_hypatia_legacy(
    mean_motion_rev_per_day: float,
) -> float:
    """Preserve Hypatia's historical floating-point conversion path."""
    return (
        mean_motion_rev_per_day
        * 60.0
        / UPSTREAM_MEAN_MOTION_DIVISOR
    )


def _mean_motion_rad_per_min(
    mean_motion_rev_per_day: float,
    compatibility: str,
) -> float:
    if compatibility == MEAN_MOTION_CANONICAL:
        return _mean_motion_rad_per_min_canonical(mean_motion_rev_per_day)
    if compatibility == MEAN_MOTION_HYPATIA_LEGACY:
        return _mean_motion_rad_per_min_hypatia_legacy(
            mean_motion_rev_per_day
        )
    raise ValueError(
        "mean_motion_compatibility must be canonical or hypatia-legacy"
    )


def generate_tles_from_scratch_with_sgp(
    filename_out: str | Path,
    constellation_name: str,
    num_orbits: int,
    num_sats_per_orbit: int,
    phase_diff: bool,
    inclination_degree: float,
    eccentricity: float,
    arg_of_perigee_degree: float,
    mean_motion_rev_per_day: float,
    *,
    raan_span_degree: float = 360.0,
    mean_motion_compatibility: str = MEAN_MOTION_HYPATIA_LEGACY,
) -> None:
    """Generate a deterministic Walker TLE set using a selected RAAN span."""
    epoch_jd, epoch_fraction = jday(2000, 1, 1, 0, 0, 0)
    mean_motion_rad_per_min = _mean_motion_rad_per_min(
        mean_motion_rev_per_day,
        mean_motion_compatibility,
    )
    with Path(filename_out).open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(f"{num_orbits} {num_sats_per_orbit}\n")
        satellite_counter = 0
        for orbit in range(num_orbits):
            raan_degree = orbit * raan_span_degree / num_orbits
            orbit_wise_shift = 0.0
            if orbit % 2 == 1 and phase_diff:
                orbit_wise_shift = 360.0 / (num_sats_per_orbit * 2.0)

            for slot in range(num_sats_per_orbit):
                mean_anomaly_degree = (
                    orbit_wise_shift + slot * 360 / num_sats_per_orbit
                )
                satellite = Satrec()
                satellite.sgp4init(
                    WGS72,
                    "i",
                    satellite_counter + 1,
                    epoch_jd + epoch_fraction - SGP4_EPOCH_OFFSET_DAYS,
                    0.0,
                    0.0,
                    0.0,
                    eccentricity,
                    math.radians(arg_of_perigee_degree),
                    math.radians(inclination_degree),
                    math.radians(mean_anomaly_degree),
                    mean_motion_rad_per_min,
                    math.radians(raan_degree),
                )

                line1, line2 = export_tle(satellite)
                line1 = line1[:7] + "U 00000ABC 00001.00000000 " + line1[33:]
                line1 = line1[:68] + str(tle_checksum(line1[:68]))
                require_valid_tle_line(line1, "line 1")
                require_valid_tle_line(line2, "line 2")
                node_id = orbit * num_sats_per_orbit + slot
                stream.write(f"{constellation_name} {node_id}\n")
                stream.write(f"{line1}\n")
                stream.write(f"{line2}\n")
                satellite_counter += 1


def tle_checksum(line_without_checksum: str) -> int:
    """Return the standard TLE checksum for a 68-character line."""
    if len(line_without_checksum) != 68:
        raise ValueError("TLE line without checksum must have 68 characters")
    return sum(
        int(character) if character.isdigit() else character == "-"
        for character in line_without_checksum
    ) % 10


def require_valid_tle_line(line: str, name: str) -> None:
    """Reject malformed or checksum-invalid TLE lines."""
    if (
        len(line) != 69
        or not line[68].isdigit()
        or tle_checksum(line[:68]) != int(line[68])
    ):
        raise ValueError(f"TLE {name} checksum failed")
