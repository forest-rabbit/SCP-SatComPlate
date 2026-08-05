"""Unit checks for the orbit-free topology trace manifest consumer."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


TOPOLOGY_TOOL_ROOT = (
    Path(__file__).resolve().parents[2] / "tools" / "generation" / "topology"
)
sys.path.insert(0, str(TOPOLOGY_TOOL_ROOT))

from common.manifest import TraceValidationError, validate_trace  # noqa: E402


def write_json(path, document):
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


class TopologyGenerationCheckerTest(unittest.TestCase):
    def make_trace(self, directory):
        root = Path(directory)
        nodes = {
            "schema_version": "0.2",
            "simulation_time_ns": 0,
            "state_semantics": "orbit-policy-evaluation",
            "coordinate_frame": "ECEF",
            "coordinate_units": "m",
            "node_count": 2,
            "nodes": [
                {"node_id": 0, "node_type": "sat", "x_m": 1, "y_m": 2, "z_m": 3},
                {"node_id": 1, "node_type": "sat", "x_m": 4, "y_m": 5, "z_m": 6},
            ],
        }
        topology = {
            "schema_version": "0.2",
            "simulation_time_ns": 0,
            "state_semantics": "orbit-policy-evaluation",
            "distance_units": "m",
            "delay_units": "ns",
            "bandwidth_units": "bps",
            "candidate_link_count": 1,
            "active_link_count": 1,
            "links": [
                {
                    "node1_id": 0,
                    "node2_id": 1,
                    "type": "sat",
                    "candidate_kind": "intra-plane",
                    "distance_m": 10,
                    "delay_ns": 1,
                    "link_bandwidth_bps": 100,
                }
            ],
        }
        nodes_path = root / "nodes_0s.json"
        topology_path = root / "topology_0s.json"
        write_json(nodes_path, nodes)
        write_json(topology_path, topology)
        digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        manifest = {
            "schema_version": "0.3",
            "run_name": "checker-unit",
            "constellation_config_sha256": "0" * 64,
            "ns3_version": "3.48",
            "state_semantics": "orbit-policy-evaluation",
            "coordinate_frame": "ECEF",
            "coordinate_units": "m",
            "speed_of_light_m_per_s": 299792458,
            "simulation_duration_ns": 1,
            "trace_interval_ns": 1,
            "network_update_interval_ns": 1,
            "include_final_state": False,
            "constellation": {},
            "topology": {},
            "randomness": {},
            "slice_count": 1,
            "slices": [
                {
                    "simulation_time_ns": 0,
                    "nodes_file": nodes_path.name,
                    "nodes_sha256": digest(nodes_path),
                    "topology_file": topology_path.name,
                    "topology_sha256": digest(topology_path),
                    "active_link_count": 1,
                }
            ],
        }
        write_json(root / "manifest.json", manifest)
        return topology_path

    def test_valid_trace_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            self.make_trace(directory)
            result = validate_trace(directory)
            self.assertEqual(result["run_name"], "checker-unit")
            self.assertEqual(result["satellite_count"], 2)
            self.assertEqual(result["slice_count"], 1)

    def test_tampered_slice_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            topology_path = self.make_trace(directory)
            topology_path.write_text("{}\n", encoding="utf-8")
            with self.assertRaises(TraceValidationError):
                validate_trace(directory)


if __name__ == "__main__":
    unittest.main()
