"""Documentation-contract tests for ComputeProfile and TaskTrace 0.1."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
TASK_ROOT = REPOSITORY_ROOT / "contrib/satcompute/task"


class TaskInputSchemaTest(unittest.TestCase):
    """Every accepted JSON parameter remains required and documented."""

    def check_closed_world_schema(self, filename: str, definition: str) -> None:
        with (TASK_ROOT / filename).open("r", encoding="utf-8") as source:
            schema = json.load(source)

        for object_name, object_schema in (
            ("root", schema),
            (definition, schema["$defs"][definition]),
        ):
            self.assertFalse(object_schema.get("additionalProperties", True))
            properties = object_schema["properties"]
            self.assertEqual(set(properties), set(object_schema["required"]))
            for property_name, property_schema in properties.items():
                description = property_schema.get("description")
                self.assertIsInstance(
                    description,
                    str,
                    f"{object_name}.{property_name} lacks a description",
                )
                self.assertTrue(description.strip())

    def test_compute_profile_is_a_documented_closed_world_contract(self) -> None:
        self.check_closed_world_schema(
            "compute-profile.schema.json", "compute_node"
        )

    def test_task_trace_is_a_documented_closed_world_contract(self) -> None:
        self.check_closed_world_schema("task-trace.schema.json", "task")


if __name__ == "__main__":
    unittest.main()
