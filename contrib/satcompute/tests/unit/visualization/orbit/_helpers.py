"""Shared topology-trace constructors for visualization tests."""

import hashlib
import json
import math
from pathlib import Path


def valid_payload():
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


def write_json(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_trace(root: Path):
    root.mkdir()
    records = []
    radius_m = 7_100_000.0
    for time_s in range(3):
        angle = time_s * 0.1
        positions = (
            (math.cos(angle), math.sin(angle), 0.0),
            (-math.sin(angle), math.cos(angle), 0.0),
            (math.cos(angle), 0.0, math.sin(angle)),
            (-math.sin(angle), 0.0, math.cos(angle)),
        )
        nodes_path = root / f"nodes_{time_s}s.json"
        topology_path = root / f"topology_{time_s}s.json"
        write_json(
            nodes_path,
            {
                "schema_version": "0.2",
                "simulation_time_ns": time_s * 1_000_000_000,
                "state_semantics": "orbit-policy-evaluation",
                "coordinate_frame": "ECEF",
                "coordinate_units": "m",
                "node_count": 4,
                "nodes": [
                    {
                        "node_id": node_id,
                        "node_type": "sat",
                        "x_m": position[0] * radius_m,
                        "y_m": position[1] * radius_m,
                        "z_m": position[2] * radius_m,
                    }
                    for node_id, position in enumerate(positions)
                ],
            },
        )
        endpoints = ((0, 1), (2, 3)) if time_s < 2 else ((0, 2), (1, 3))
        links = [
            {
                "node1_id": endpoint1,
                "node2_id": endpoint2,
                "type": "sat",
                "candidate_kind": "unit",
                "distance_m": 1,
                "delay_ns": 1,
                "link_bandwidth_bps": 100,
            }
            for endpoint1, endpoint2 in endpoints
        ]
        write_json(
            topology_path,
            {
                "schema_version": "0.2",
                "simulation_time_ns": time_s * 1_000_000_000,
                "state_semantics": "orbit-policy-evaluation",
                "distance_units": "m",
                "delay_units": "ns",
                "bandwidth_units": "bps",
                "candidate_link_count": 4,
                "active_link_count": len(links),
                "links": links,
            },
        )
        records.append(
            {
                "simulation_time_ns": time_s * 1_000_000_000,
                "nodes_file": nodes_path.name,
                "nodes_sha256": sha256(nodes_path),
                "topology_file": topology_path.name,
                "topology_sha256": sha256(topology_path),
                "active_link_count": len(links),
            }
        )
    write_json(
        root / "manifest.json",
        {
            "schema_version": "0.3",
            "run_name": "viewer-unit",
            "constellation_config_sha256": "0" * 64,
            "ns3_version": "3.48",
            "state_semantics": "orbit-policy-evaluation",
            "coordinate_frame": "ECEF",
            "coordinate_units": "m",
            "speed_of_light_m_per_s": 299792458,
            "simulation_duration_ns": 2_000_000_000,
            "trace_interval_ns": 1_000_000_000,
            "network_update_interval_ns": 2_000_000_000,
            "include_final_state": True,
            "constellation": {
                "num_orbits": 2,
                "satellites_per_orbit": 2,
                "satellite_count": 4,
            },
            "topology": {"delay_mode": "distance"},
            "randomness": {"seed": 1, "run": 1, "stream_start": 0},
            "slice_count": len(records),
            "slices": records,
        },
    )
    compute_profile = root.parent / "compute-profile.json"
    write_json(
        compute_profile,
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
    return compute_profile
