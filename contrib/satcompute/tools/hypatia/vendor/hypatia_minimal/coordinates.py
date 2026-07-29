#!/usr/bin/env python3
"""Minimal WGS72 coordinate conversion adapted from Hypatia/satgenpy."""

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


def geodetic2cartesian(
    latitude_degrees: float,
    longitude_degrees: float,
    elevation_m: float,
) -> tuple[float, float, float]:
    """Convert geodetic WGS72 coordinates to Cartesian metres."""
    semi_major_axis_m = 6378135.0
    flattening = 1.0 / 298.26
    eccentricity = math.sqrt(2.0 * flattening - flattening * flattening)
    latitude = math.radians(latitude_degrees)
    longitude = math.radians(longitude_degrees)
    vertical_radius = semi_major_axis_m / math.sqrt(
        1.0
        - eccentricity
        * eccentricity
        * math.sin(latitude)
        * math.sin(latitude)
    )
    x = (
        (vertical_radius + elevation_m)
        * math.cos(latitude)
        * math.cos(longitude)
    )
    y = (
        (vertical_radius + elevation_m)
        * math.cos(latitude)
        * math.sin(longitude)
    )
    z = (
        vertical_radius * (1.0 - eccentricity * eccentricity) + elevation_m
    ) * math.sin(latitude)
    return x, y, z
