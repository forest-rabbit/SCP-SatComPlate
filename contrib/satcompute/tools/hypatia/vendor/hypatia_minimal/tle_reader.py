#!/usr/bin/env python3
"""Adapted minimal Hypatia/satgenpy TLE reader."""

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

from pathlib import Path
from typing import Any

import ephem
from astropy import units as u
from astropy.time import Time


def read_tles(filename_tles: str | Path) -> dict[str, Any]:
    """Read one Hypatia-format constellation and enforce sequential node IDs."""
    satellites = []
    universal_epoch = None
    with Path(filename_tles).open("r", encoding="utf-8") as stream:
        num_orbits, satellites_per_orbit = (
            int(value) for value in stream.readline().split()
        )
        for expected_node_id, name_line in enumerate(stream):
            line1 = stream.readline()
            line2 = stream.readline()
            node_id = int(name_line.split()[1])
            if node_id != expected_node_id:
                raise ValueError(
                    "Satellite identifier is not increasing by one each line"
                )

            epoch_year = line1[18:20]
            epoch_day = float(line1[20:32])
            epoch = (
                Time(f"20{epoch_year}-01-01 00:00:00", scale="tdb")
                + (epoch_day - 1) * u.day
            )
            if universal_epoch is None:
                universal_epoch = epoch
            elif epoch != universal_epoch:
                raise ValueError("The epoch of all TLES must be the same")
            satellites.append(ephem.readtle(name_line, line1, line2))

    if universal_epoch is None:
        raise ValueError("TLE file contains no satellites")
    return {
        "n_orbits": num_orbits,
        "n_sats_per_orbit": satellites_per_orbit,
        "epoch": universal_epoch,
        "satellites": satellites,
    }
