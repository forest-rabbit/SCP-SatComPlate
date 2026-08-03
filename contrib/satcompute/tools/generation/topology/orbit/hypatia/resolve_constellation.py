#!/usr/bin/env python3
"""Generate a TLE set and resolved manifest for one constellation preset."""

from __future__ import annotations

import argparse
import json
import platform
from pathlib import Path
from typing import Any

from ...common.configuration import ConstellationConfig, load_config
from ...common.hash_utils import (
    REPOSITORY_ROOT,
    sha256_file,
    uv_version,
)
from ...dynamic.dynamic_isls import (
    build_candidate_isls,
    candidate_degree_profile,
)
from .adapter import HypatiaAdapter
from .mean_motion import (
    WGS72_EARTH_RADIUS_KM,
    WGS72_MU_KM3_S2,
    mean_motion_rev_per_day,
    orbital_period_minutes,
)
from .orbit_positions import (
    load_tle_orbit_constellation,
    position_samples_sha256,
)
from .walker_tles import (
    ARGUMENT_OF_PERIGEE_DEG,
    EPOCH_UTC,
    NEAR_CIRCULAR_ECCENTRICITY,
    generate_walker_tles,
    walker_slots,
)


TOOL_DIR = Path(__file__).resolve().parent
TOPOLOGY_ROOT = TOOL_DIR.parents[1]
DEFAULT_CONFIG = TOPOLOGY_ROOT / "config" / "synthetic-66.json"
POSITION_SAMPLE_TIMES_S = (0.0, 60.0)
TLE_FILENAME = "tles.txt"
MANIFEST_FILENAME = "resolved-manifest.json"

def build_manifest(
    config: ConstellationConfig,
    adapter: HypatiaAdapter,
    tle_path: Path,
    positions_sha256: str,
) -> dict[str, Any]:
    mean_motion = mean_motion_rev_per_day(config.altitude_km)
    slots = walker_slots(config)
    candidates = build_candidate_isls(config)
    raan_sequence = [
        slots[orbit * config.satellites_per_orbit].raan_deg
        for orbit in range(config.num_orbits)
    ]
    return {
        "schema_version": config.schema_version,
        "constellation_name": config.constellation_name,
        "constellation_pattern": config.constellation_pattern,
        "num_orbits": config.num_orbits,
        "satellites_per_orbit": config.satellites_per_orbit,
        "expected_satellite_count": config.expected_satellite_count,
        "altitude_km": config.altitude_km,
        "inclination_deg": config.inclination_deg,
        "orbit_model": "near-circular",
        "phase_diff": config.phase_diff,
        "phase_scheme": config.phase_scheme,
        "isl_candidate_strategy": config.isl_candidate_strategy,
        "seam_enabled": config.seam_enabled,
        "max_isl_distance_m": config.max_isl_distance_m,
        "candidate_count": len(candidates),
        "candidate_degree_profile": candidate_degree_profile(
            config,
            candidates,
        ),
        "eccentricity": NEAR_CIRCULAR_ECCENTRICITY,
        "argument_of_perigee_deg": ARGUMENT_OF_PERIGEE_DEG,
        "epoch_utc": EPOCH_UTC,
        "raan_origin_deg": 0.0,
        "raan_span_deg": config.raan_span_deg,
        "raan_step_deg": config.raan_step_deg,
        "raan_sequence_deg": raan_sequence,
        "slot_spacing_deg": config.slot_spacing_deg,
        "phase_offset_deg": config.phase_offset_deg,
        "wgs72_earth_radius_km": WGS72_EARTH_RADIUS_KM,
        "wgs72_mu_km3_s2": WGS72_MU_KM3_S2,
        "mean_motion_rev_per_day": mean_motion,
        "orbital_period_minutes": orbital_period_minutes(mean_motion),
        "hypatia_repository": adapter.repository,
        "hypatia_commit": adapter.commit,
        "hypatia_integration_mode": "vendored-minimal",
        "hypatia_vendor_manifest_sha256": sha256_file(adapter.origin_path),
        "python_version": platform.python_version(),
        "uv_version": uv_version(),
        "uv_lock_sha256": sha256_file(REPOSITORY_ROOT / "uv.lock"),
        "tle_sha256": sha256_file(tle_path),
        "position_sample_times_s": list(POSITION_SAMPLE_TIMES_S),
        "positions_sha256": positions_sha256,
    }


def resolve_constellation(
    config_path: Path,
    output_dir: Path,
) -> dict[str, Any]:
    config = load_config(config_path)
    adapter = HypatiaAdapter()
    output_dir.mkdir(parents=True, exist_ok=True)
    tle_path = output_dir / TLE_FILENAME
    generate_walker_tles(tle_path, config, adapter)
    orbit = load_tle_orbit_constellation(
        tle_path,
        adapter,
        config.expected_satellite_count,
    )

    manifest = build_manifest(
        config,
        adapter,
        tle_path,
        position_samples_sha256(
            orbit,
            POSITION_SAMPLE_TIMES_S,
            minimum_movement_m=1.0,
        ),
    )
    manifest_path = output_dir / MANIFEST_FILENAME
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    manifest = resolve_constellation(
        arguments.config.resolve(),
        arguments.output_dir.resolve(),
    )
    print(json.dumps(manifest, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
