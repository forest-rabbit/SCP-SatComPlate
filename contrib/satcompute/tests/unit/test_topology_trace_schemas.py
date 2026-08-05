"""Documentation-contract tests for topology trace output contracts."""

from __future__ import annotations

import json
import unittest
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
EXPORT_ROOT = REPOSITORY_ROOT / "contrib/satcompute/topology/export"
SCHEMA_FILES = (
    "nodes-slice.schema.json",
    "topology-slice.schema.json",
    "manifest.schema.json",
)


class TopologyTraceSchemaTest(unittest.TestCase):
    """Every emitted field is required, closed-world, and documented."""

    def check_schema_node(self, node: Any, path: str) -> None:
        if isinstance(node, dict):
            properties = node.get("properties")
            if properties is not None:
                self.assertFalse(
                    node.get("additionalProperties", True),
                    f"{path} is not closed-world",
                )
                self.assertEqual(
                    set(properties),
                    set(node.get("required", [])),
                    f"{path} does not require every emitted property",
                )
                for property_name, property_schema in properties.items():
                    description = property_schema.get("description")
                    self.assertIsInstance(
                        description,
                        str,
                        f"{path}.{property_name} lacks a description",
                    )
                    self.assertTrue(description.strip())
            for key, value in node.items():
                self.check_schema_node(value, f"{path}.{key}")
        elif isinstance(node, list):
            for index, value in enumerate(node):
                self.check_schema_node(value, f"{path}[{index}]")

    def test_trace_schemas_are_documented_closed_world_contracts(self) -> None:
        for filename in SCHEMA_FILES:
            with self.subTest(filename=filename):
                with (EXPORT_ROOT / filename).open(encoding="utf-8") as source:
                    schema = json.load(source)
                self.assertEqual(
                    schema["$schema"],
                    "https://json-schema.org/draft/2020-12/schema",
                )
                self.check_schema_node(schema, filename)

    def test_shared_units_and_state_semantics_are_explicit(self) -> None:
        with (EXPORT_ROOT / "nodes-slice.schema.json").open(
            encoding="utf-8"
        ) as source:
            nodes = json.load(source)
        with (EXPORT_ROOT / "topology-slice.schema.json").open(
            encoding="utf-8"
        ) as source:
            topology = json.load(source)
        with (EXPORT_ROOT / "manifest.schema.json").open(
            encoding="utf-8"
        ) as source:
            manifest = json.load(source)

        for schema in (nodes, topology, manifest):
            self.assertEqual(
                schema["properties"]["state_semantics"]["const"],
                "orbit-policy-evaluation",
            )
        self.assertEqual(nodes["properties"]["coordinate_frame"]["const"], "ECEF")
        self.assertEqual(topology["properties"]["delay_units"]["const"], "ns")
        self.assertEqual(
            manifest["properties"]["speed_of_light_m_per_s"]["const"],
            299_792_458,
        )


if __name__ == "__main__":
    unittest.main()
