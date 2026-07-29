#!/usr/bin/env python3
"""Exercise frozen Hypatia TLE generation and position propagation."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from hypatia_adapter import HypatiaAdapter


NUM_ORBITS = 2
SATELLITES_PER_ORBIT = 3
MEAN_MOTION_REV_PER_DAY = 15.19
ECCENTRICITY = 0.0000001
SAMPLE_TIMES_S = (0.0, 60.0)


def uv_version() -> str:
    result = subprocess.run(
        ["uv", "--version"],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip()


def run_smoke() -> dict[str, Any]:
    adapter = HypatiaAdapter()
    with tempfile.TemporaryDirectory(prefix="satcompute-hypatia-smoke-") as temp:
        tle_path = Path(temp) / "tles.txt"
        adapter.generate_tles(
            tle_path,
            constellation_name="satcompute-smoke",
            num_orbits=NUM_ORBITS,
            satellites_per_orbit=SATELLITES_PER_ORBIT,
            phase_diff=True,
            inclination_deg=53.0,
            eccentricity=ECCENTRICITY,
            argument_of_perigee_deg=0.0,
            mean_motion_rev_per_day=MEAN_MOTION_REV_PER_DAY,
        )
        constellation = adapter.read_tles(tle_path)

    satellites = constellation["satellites"]
    expected_count = NUM_ORBITS * SATELLITES_PER_ORBIT
    if len(satellites) != expected_count:
        raise RuntimeError(
            f"expected {expected_count} satellites, found {len(satellites)}"
        )

    positions: dict[str, list[list[float]]] = {}
    for time_s in SAMPLE_TIMES_S:
        snapshot = []
        for node_id, satellite in enumerate(satellites):
            xyz = adapter.satellite_position_at(
                satellite,
                constellation["epoch"],
                time_s,
            )
            snapshot.append([node_id, *(round(value, 3) for value in xyz)])
        positions[str(int(time_s))] = snapshot

    for node_id in range(expected_count):
        start = positions["0"][node_id][1:]
        end = positions["60"][node_id][1:]
        if math.dist(start, end) <= 1.0:
            raise RuntimeError(
                f"satellite {node_id} did not move between smoke samples"
            )

    canonical = json.dumps(positions, sort_keys=True, separators=(",", ":"))
    return {
        "upstream_commit": adapter.commit,
        "python_version": sys.version.split()[0],
        "uv_version": uv_version(),
        "satellite_count": expected_count,
        "times_s": list(SAMPLE_TIMES_S),
        "epoch": str(constellation["epoch"]),
        "positions_sha256": hashlib.sha256(
            canonical.encode("utf-8")
        ).hexdigest(),
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit compact JSON")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    result = run_smoke()
    if arguments.json:
        print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    else:
        print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
