"""Contract checks for the constellation-only JSON input."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
MODULE_ROOT = REPOSITORY_ROOT / "contrib/satcompute"
SCHEMA_PATH = MODULE_ROOT / "topology/orbit/constellation.schema.json"
EXAMPLE_PATH = MODULE_ROOT / "input/topology/constellations/synthetic-66.json"


class ConstellationSchemaTest(unittest.TestCase):
    """The physical constellation file must not become a runtime config."""

    def setUp(self) -> None:
        with SCHEMA_PATH.open(encoding="utf-8") as source:
            self.schema = json.load(source)
        with EXAMPLE_PATH.open(encoding="utf-8") as source:
            self.example = json.load(source)

    def test_contract_is_closed_world_and_documented(self) -> None:
        self.assertEqual(
            self.schema["$schema"],
            "https://json-schema.org/draft/2020-12/schema",
        )
        self.assertFalse(self.schema.get("additionalProperties", True))
        properties = self.schema["properties"]
        self.assertEqual(set(properties), set(self.schema["required"]))
        for name, definition in properties.items():
            with self.subTest(name=name):
                self.assertTrue(definition.get("description", "").strip())

    def test_example_contains_only_physical_structure(self) -> None:
        self.assertEqual(set(self.example), set(self.schema["properties"]))
        self.assertEqual(self.example["schema_version"], "0.1")
        self.assertEqual(
            self.example["num_orbits"] * self.example["satellites_per_orbit"],
            66,
        )
        forbidden = {
            "simulation_duration_s",
            "network_update_interval_s",
            "topology_export_interval_s",
            "max_isl_distance_m",
            "delay_mode",
            "routing_mode",
            "transfer_trace",
            "compute_profile",
            "task_trace",
            "random_seed",
            "output_dir",
        }
        self.assertTrue(forbidden.isdisjoint(self.example))


if __name__ == "__main__":
    unittest.main()
