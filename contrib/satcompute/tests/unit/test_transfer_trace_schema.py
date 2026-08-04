"""Documentation-contract tests for TransferTrace 0.1."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
SCHEMA_PATH = REPOSITORY_ROOT / "contrib/satcompute/traffic/transfer-trace.schema.json"


class TransferTraceSchemaTest(unittest.TestCase):
    """The schema remains a closed-world parameter reference."""

    def test_every_declared_property_is_required_and_documented(self) -> None:
        with SCHEMA_PATH.open("r", encoding="utf-8") as source:
            schema = json.load(source)

        for object_name, object_schema in (
            ("root", schema),
            ("$defs.transfer", schema["$defs"]["transfer"]),
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


if __name__ == "__main__":
    unittest.main()
