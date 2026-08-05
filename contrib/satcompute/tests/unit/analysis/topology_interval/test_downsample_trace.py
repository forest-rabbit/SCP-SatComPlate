#!/usr/bin/env python3
"""Tests for copy-only v0.3 topology-trace downsampling."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from contrib.satcompute.tools.analysis.topology_interval.compare_intervals import (
    compare_traces,
)
from contrib.satcompute.tools.analysis.topology_interval.downsample_scenario import (
    TraceDownsampleError,
    downsample_trace,
)
from contrib.satcompute.tools.generation.topology.common.manifest import validate_trace


def write_json(path, document):
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def make_reference_trace(root):
    root.mkdir()
    edge_states = (
        ((0, 1), (1, 2)),
        ((0, 1),),
        ((0, 1), (1, 2)),
        ((0, 1), (1, 2), (0, 2)),
        ((0, 1), (1, 2), (0, 2)),
    )
    records = []
    for time_s, edges in enumerate(edge_states):
        time_ns = time_s * 1_000_000_000
        nodes_path = root / f"nodes_{time_s}s.json"
        topology_path = root / f"topology_{time_s}s.json"
        nodes = {
            "schema_version": "0.2",
            "simulation_time_ns": time_ns,
            "state_semantics": "orbit-policy-evaluation",
            "coordinate_frame": "ECEF",
            "coordinate_units": "m",
            "node_count": 3,
            "nodes": [
                {
                    "node_id": node_id,
                    "node_type": "sat",
                    "x_m": node_id + time_s,
                    "y_m": node_id,
                    "z_m": 0,
                }
                for node_id in range(3)
            ],
        }
        links = [
            {
                "node1_id": min(endpoint1, endpoint2),
                "node2_id": max(endpoint1, endpoint2),
                "type": "sat",
                "candidate_kind": "unit",
                "distance_m": 1,
                "delay_ns": 1,
                "link_bandwidth_bps": 100,
            }
            for endpoint1, endpoint2 in edges
        ]
        links.sort(key=lambda link: (link["node1_id"], link["node2_id"]))
        topology = {
            "schema_version": "0.2",
            "simulation_time_ns": time_ns,
            "state_semantics": "orbit-policy-evaluation",
            "distance_units": "m",
            "delay_units": "ns",
            "bandwidth_units": "bps",
            "candidate_link_count": 3,
            "active_link_count": len(links),
            "links": links,
        }
        write_json(nodes_path, nodes)
        write_json(topology_path, topology)
        records.append(
            {
                "simulation_time_ns": time_ns,
                "nodes_file": nodes_path.name,
                "nodes_sha256": sha256(nodes_path),
                "topology_file": topology_path.name,
                "topology_sha256": sha256(topology_path),
                "active_link_count": len(links),
            }
        )
    manifest = {
        "schema_version": "0.3",
        "run_name": "interval-reference",
        "constellation_config_sha256": "0" * 64,
        "ns3_version": "3.48",
        "state_semantics": "orbit-policy-evaluation",
        "coordinate_frame": "ECEF",
        "coordinate_units": "m",
        "speed_of_light_m_per_s": 299792458,
        "simulation_duration_ns": 4_000_000_000,
        "trace_interval_ns": 1_000_000_000,
        "network_update_interval_ns": 2_000_000_000,
        "include_final_state": True,
        "constellation": {"satellite_count": 3},
        "topology": {"delay_mode": "distance"},
        "randomness": {"seed": 1, "run": 1, "stream_start": 0},
        "slice_count": len(records),
        "slices": records,
    }
    write_json(root / "manifest.json", manifest)


class DownsampleTraceTest(unittest.TestCase):
    def test_selected_slices_are_exact_and_comparable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference = root / "reference"
            output = root / "held-2s"
            make_reference_trace(reference)
            result = downsample_trace(reference, 2, output)
            validation = validate_trace(output)
            self.assertEqual(result["slice_count"], 3)
            self.assertEqual(validation["slice_count"], 3)
            for time_s in (0, 2, 4):
                for kind in ("nodes", "topology"):
                    filename = f"{kind}_{time_s}s.json"
                    self.assertEqual(
                        (reference / filename).read_bytes(),
                        (output / filename).read_bytes(),
                    )
            comparison = compare_traces(reference, output)
            self.assertEqual(comparison["interval_s"], 2)
            self.assertGreater(
                comparison["edge_state"]["absolute_edge_state_errors"],
                0,
            )
            self.assertLess(
                comparison["ecmp_candidates"]["exact_candidate_match_ratio"],
                1.0,
            )

    def test_invalid_interval_overlap_and_existing_output_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference = root / "reference"
            make_reference_trace(reference)
            with self.assertRaisesRegex(TraceDownsampleError, "divisible"):
                downsample_trace(reference, 3, root / "held-3s")
            with self.assertRaisesRegex(TraceDownsampleError, "overlap"):
                downsample_trace(reference, 2, reference / "nested")
            output = root / "existing"
            output.mkdir()
            with self.assertRaisesRegex(TraceDownsampleError, "already exists"):
                downsample_trace(reference, 2, output)


if __name__ == "__main__":
    unittest.main()
