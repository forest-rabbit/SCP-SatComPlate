"""Shared constructors for orbit-visualization tests."""

from __future__ import annotations

from pathlib import Path

from contrib.satcompute.tests.support.fixtures import write_json
from contrib.satcompute.tools.generation.topology.orbit.hypatia.orbit_positions import (
    SatellitePosition,
)


def valid_payload() -> dict:
    return {
        "schema_version": "0.1",
        "enabled": False,
        "display_mode": "auto",
        "render_step_s": 1,
        "playback_interval_ms": 50,
        "show_earth": True,
        "show_orbits": True,
        "show_links": False,
        "show_node_labels": False,
        "detail_node_threshold": 100,
        "export_gif": False,
        "gif_path": None,
        "gif_frame_step_s": 5,
    }


class FakeOrbit:
    def __init__(self, node_count: int) -> None:
        self.node_count = node_count
        self.requested_times: list[float] = []

    def positions_at(self, time_s: float) -> tuple[SatellitePosition, ...]:
        self.requested_times.append(time_s)
        return tuple(
            SatellitePosition(
                node_id,
                7_000_000.0 + node_id * 1000.0,
                time_s * 1000.0,
                node_id * 10_000.0,
            )
            for node_id in range(self.node_count)
        )


def scenario_config(mode: str) -> dict:
    schedule = (
        {
            "start_time_s": 0,
            "orbit_sample_offset_s": 100,
            "duration_s": 20,
            "step_s": 20,
        }
        if mode == "dynamic"
        else {"snapshot_time_s": 17}
    )
    return {
        "schema_version": "0.1",
        "scenario_name": f"visualization-{mode}",
        "constellation": {
            "constellation_name": "visualization-fixture",
            "constellation_pattern": "walker-star",
            "num_orbits": 2,
            "satellites_per_orbit": 2,
            "altitude_km": 780.0,
            "inclination_deg": 86.4,
            "phase_diff": True,
        },
        "topology": {
            "mode": mode,
            "schedule": schedule,
            "isl_candidate_strategy": "plus-grid",
            "seam_enabled": False,
            "max_isl_distance_m": 6_174_589,
            "delay_mode": "fixed",
            "fixed_delay_us": 8000,
            "link_bandwidth_kbps": 2_000_000,
        },
        "compute": {
            "compute_node_count": 2,
            "placement_strategy": "even-plane-slot",
            "compute_rate_work_units_per_second": 1_500_000,
        },
    }


def write_fixture(root: Path, mode: str) -> None:
    config = scenario_config(mode)
    write_json(
        root / "scenario-manifest.json",
        {"scenario_config": config},
    )
    write_json(
        root / "resources" / "compute-profile.json",
        {
            "schema_version": "0.1",
            "compute_nodes": [
                {
                    "node_id": 1,
                    "compute_rate_work_units_per_second": 1_500_000,
                },
                {
                    "node_id": 3,
                    "compute_rate_work_units_per_second": 1_500_000,
                },
            ],
        },
    )
    nodes = {
        "nodes": [
            {"node_id": node_id, "node_type": "sat"}
            for node_id in range(4)
        ]
    }
    times = (0, 20) if mode == "dynamic" else (0,)
    for time_s in times:
        write_json(root / "topology" / f"nodes_{time_s}s.json", nodes)
        endpoints = (0, 1) if time_s == 0 else (2, 3)
        write_json(
            root / "topology" / f"topology_{time_s}s.json",
            {
                "links": [
                    {
                        "node1_id": endpoints[0],
                        "node2_id": endpoints[1],
                        "type": "sat",
                        "delay": 8000,
                        "link_bandwidth": 2_000_000,
                    }
                ]
            },
        )
