#!/usr/bin/env python3
"""Earth and instantaneous orbit-ring geometry in Earth-fixed kilometres."""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Iterable


EARTH_RADIUS_KM = 6378.135
Vector3 = tuple[float, float, float]


class OrbitGeometryError(ValueError):
    """Raised when Earth-centred display geometry is degenerate."""


@dataclass(frozen=True)
class OrbitRing:
    """One fitted circular reference ring in an instantaneous orbit plane."""

    normal: Vector3
    basis_u: Vector3
    basis_v: Vector3
    radius_km: float
    points_km: tuple[Vector3, ...]


def _dot(left: Vector3, right: Vector3) -> float:
    return sum(a * b for a, b in zip(left, right))


def _norm(vector: Vector3) -> float:
    return math.sqrt(_dot(vector, vector))


def _scale(vector: Vector3, factor: float) -> Vector3:
    return tuple(value * factor for value in vector)  # type: ignore[return-value]


def _cross(left: Vector3, right: Vector3) -> Vector3:
    return (
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    )


def _unit(vector: Vector3, name: str) -> Vector3:
    length = _norm(vector)
    if not math.isfinite(length) or length <= 0.0:
        raise OrbitGeometryError(f"{name} must be finite and non-zero")
    return _scale(vector, 1.0 / length)


def sphere_point_km(latitude_rad: float, longitude_rad: float) -> Vector3:
    """Return one WGS72 sphere point for renderer and radius tests."""
    if not all(math.isfinite(value) for value in (latitude_rad, longitude_rad)):
        raise OrbitGeometryError("sphere angles must be finite")
    cos_latitude = math.cos(latitude_rad)
    return (
        EARTH_RADIUS_KM * cos_latitude * math.cos(longitude_rad),
        EARTH_RADIUS_KM * cos_latitude * math.sin(longitude_rad),
        EARTH_RADIUS_KM * math.sin(latitude_rad),
    )


def fit_orbit_ring(
    positions_km: Iterable[Vector3],
    *,
    sample_count: int = 181,
) -> OrbitRing:
    """Fit a circular ring from real same-frame Earth-fixed positions."""
    positions = tuple(tuple(float(value) for value in row) for row in positions_km)
    if len(positions) < 2:
        raise OrbitGeometryError(
            "orbit ring fitting requires at least two satellite positions"
        )
    if (
        not isinstance(sample_count, int)
        or isinstance(sample_count, bool)
        or sample_count < 3
    ):
        raise OrbitGeometryError("sample_count must be an integer of at least 3")
    if any(
        len(position) != 3
        or not all(math.isfinite(value) for value in position)
        for position in positions
    ):
        raise OrbitGeometryError("orbit positions must be finite XYZ triples")

    basis_u = _unit(positions[0], "first orbit position")
    normal = None
    for candidate in positions[1:]:
        cross = _cross(positions[0], candidate)
        scale = _norm(positions[0]) * _norm(candidate)
        if scale > 0.0 and _norm(cross) > scale * 1e-12:
            normal = _unit(cross, "orbit plane normal")
            break
    if normal is None:
        raise OrbitGeometryError(
            "orbit positions do not contain two non-collinear vectors"
        )
    basis_v = _unit(_cross(normal, basis_u), "orbit plane basis")
    radius_km = sum(_norm(position) for position in positions) / len(positions)
    if not math.isfinite(radius_km) or radius_km <= EARTH_RADIUS_KM:
        raise OrbitGeometryError(
            "fitted orbit radius must be finite and above the Earth radius"
        )
    points = []
    for index in range(sample_count):
        if index == sample_count - 1:
            points.append(points[0])
            continue
        angle = 2.0 * math.pi * index / (sample_count - 1)
        point = tuple(
            radius_km
            * (
                math.cos(angle) * basis_u[axis]
                + math.sin(angle) * basis_v[axis]
            )
            for axis in range(3)
        )
        points.append(point)  # type: ignore[arg-type]
    return OrbitRing(
        normal=normal,
        basis_u=basis_u,
        basis_v=basis_v,
        radius_km=radius_km,
        points_km=tuple(points),
    )
