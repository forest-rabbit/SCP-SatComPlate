#!/usr/bin/env python3
"""Tests for the orbit-free topology-trace visualization reader."""

import json
from pathlib import Path
import tempfile
import unittest

from contrib.satcompute.tests.unit.visualization.orbit._helpers import (
    write_json,
    write_trace,
)
from contrib.satcompute.tools.visualization.orbit.scenario_reader import (
    TraceVisualizationError,
    load_trace,
)


class TraceReaderTest(unittest.TestCase):
    def test_positions_links_roles_and_frame_times_come_from_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace_dir = root / "trace"
            compute_profile = write_trace(trace_dir)
            trace = load_trace(trace_dir, compute_profile=compute_profile)
            self.assertEqual(trace.node_count, 4)
            self.assertEqual(trace.num_orbits, 2)
            self.assertEqual(trace.satellites_per_orbit, 2)
            self.assertEqual(trace.compute_node_ids, (1, 3))
            self.assertEqual(trace.relay_node_ids, (0, 2))
            self.assertEqual(trace.links_at(0.5), ((0, 1), (2, 3)))
            self.assertEqual(trace.links_at(2.0), ((0, 2), (1, 3)))
            self.assertEqual(trace.source_snapshot_time_s(1.75), 1.0)
            self.assertEqual(trace.positions_at(0.75), trace.positions_at(0.0))
            self.assertNotEqual(trace.positions_at(1.0), trace.positions_at(0.0))
            self.assertEqual(trace.frame_times(0.75), (0.0, 0.75, 1.5, 2.0))
            self.assertEqual(len(trace.orbit_positions(trace.positions_at(0), 0)), 2)

    def test_compute_profile_is_optional_and_closed_world(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace_dir = root / "trace"
            compute_profile = write_trace(trace_dir)
            trace = load_trace(trace_dir)
            self.assertEqual(trace.compute_node_ids, ())
            self.assertEqual(trace.relay_node_ids, (0, 1, 2, 3))

            profile = json.loads(compute_profile.read_text(encoding="utf-8"))
            profile["compute_nodes"][0]["node_id"] = 9
            write_json(compute_profile, profile)
            with self.assertRaisesRegex(TraceVisualizationError, "invalid"):
                load_trace(trace_dir, compute_profile=compute_profile)

    def test_manifest_hash_tamper_is_rejected_before_display(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace_dir = root / "trace"
            write_trace(trace_dir)
            (trace_dir / "nodes_1s.json").write_text("{}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "hash differs"):
                load_trace(trace_dir)

    def test_display_time_bounds_are_strict(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace_dir = root / "trace"
            write_trace(trace_dir)
            trace = load_trace(trace_dir)
            for value in (-1, 2.1, True, float("inf")):
                with self.subTest(value=value):
                    with self.assertRaises(TraceVisualizationError):
                        trace.positions_at(value)


if __name__ == "__main__":
    unittest.main()
