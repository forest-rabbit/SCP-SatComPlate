#!/usr/bin/env python3
"""Contract checks for documented independent-input bundle evidence."""

import json
from pathlib import Path
import unittest


SCHEMA = (
    Path(__file__).resolve().parents[4]
    / "tools"
    / "generation"
    / "scenario"
    / "input-bundle.schema.json"
)


class InputBundleSchemaTest(unittest.TestCase):
    def test_root_is_closed_world_and_every_field_is_documented(self):
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        self.assertFalse(schema["additionalProperties"])
        self.assertEqual(set(schema["required"]), set(schema["properties"]))
        for name, definition in schema["properties"].items():
            with self.subTest(name=name):
                self.assertTrue(definition.get("description"))

    def test_bundle_is_evidence_not_a_runtime_configuration(self):
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        self.assertIn("not a platform runtime configuration", schema["description"])
        properties = schema["properties"]
        for forbidden in (
            "routing_mode",
            "delay_mode",
            "network_update_interval",
            "topology_export_interval",
            "output_directory",
        ):
            self.assertNotIn(forbidden, properties)
        self.assertEqual(
            properties["inputs"]["properties"]["fault_trace"],
            {"type": "null"},
        )


if __name__ == "__main__":
    unittest.main()
