#!/usr/bin/env python3
"""Narrow adapter for the frozen Hypatia orbit-generation source files."""

from __future__ import annotations

import importlib.util
import math
import subprocess
from pathlib import Path
from types import ModuleType
from typing import Any

from astropy.time import TimeDelta

from bootstrap import (
    DEFAULT_CHECKOUT,
    DEFAULT_UPSTREAM,
    BootstrapError,
    load_upstream,
)


class HypatiaAdapterError(RuntimeError):
    """Raised when the frozen Hypatia orbit interface cannot be used."""


def load_source_module(name: str, path: Path) -> ModuleType:
    if not path.is_file():
        raise HypatiaAdapterError(f"required Hypatia source file is missing: {path}")
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise HypatiaAdapterError(f"cannot load Hypatia source module: {path}")
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as error:
        raise HypatiaAdapterError(
            f"cannot import frozen Hypatia source module {path}: {error}"
        ) from error
    return module


def checkout_head(checkout: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=checkout,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise HypatiaAdapterError(
            f"cannot read Hypatia checkout HEAD at {checkout}: {detail}"
        )
    return result.stdout.strip()


class HypatiaAdapter:
    """Expose only TLE generation, TLE reading, and satellite positions."""

    def __init__(
        self,
        checkout: Path = DEFAULT_CHECKOUT,
        upstream_path: Path = DEFAULT_UPSTREAM,
    ) -> None:
        self.checkout = checkout.resolve()
        try:
            repository, commit, component = load_upstream(upstream_path.resolve())
        except BootstrapError as error:
            raise HypatiaAdapterError(str(error)) from error

        if not (self.checkout / ".git").is_dir():
            raise HypatiaAdapterError(
                f"Hypatia is not bootstrapped at {self.checkout}; run bootstrap.py"
            )
        actual_head = checkout_head(self.checkout)
        if actual_head != commit:
            raise HypatiaAdapterError(
                f"Hypatia HEAD mismatch: expected {commit}, found {actual_head}"
            )

        source_root = self.checkout / component / "satgen"
        self.repository = repository
        self.commit = commit
        self._tle_generator = load_source_module(
            "satcompute_hypatia_generate_tles",
            source_root / "tles" / "generate_tles_from_scratch.py",
        )
        self._tle_reader = load_source_module(
            "satcompute_hypatia_read_tles",
            source_root / "tles" / "read_tles.py",
        )
        self._distance_tools = load_source_module(
            "satcompute_hypatia_distance_tools",
            source_root / "distance_tools" / "distance_tools.py",
        )

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
    ) -> None:
        output.parent.mkdir(parents=True, exist_ok=True)
        self._tle_generator.generate_tles_from_scratch_with_sgp(
            str(output),
            constellation_name,
            num_orbits,
            satellites_per_orbit,
            phase_diff,
            inclination_deg,
            eccentricity,
            argument_of_perigee_deg,
            mean_motion_rev_per_day,
        )

    def read_tles(self, path: Path) -> dict[str, Any]:
        return self._tle_reader.read_tles(str(path))

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
        position = self._distance_tools.geodetic2cartesian(
            latitude_deg,
            longitude_deg,
            elevation_m,
        )
        if not all(math.isfinite(value) for value in position):
            raise HypatiaAdapterError("Hypatia returned a non-finite satellite position")
        return position
