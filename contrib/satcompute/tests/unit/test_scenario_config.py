"""Unit tests for the SatCompute scenario 0.2 contract."""

from __future__ import annotations

import copy
import json
import unittest
from decimal import Decimal
from pathlib import Path

from contrib.satcompute.config.scenario_config import (
    ScenarioConfigError,
    load_scenario,
    parse_scenario,
    seconds_to_nanoseconds,
)


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
EXAMPLE_DIRECTORY = REPOSITORY_ROOT / "contrib/satcompute/input/examples"
SCHEMA_PATH = REPOSITORY_ROOT / "contrib/satcompute/config/scenario.schema.json"
SCENARIO_FIXTURES = REPOSITORY_ROOT / "contrib/satcompute/tests/fixtures/scenario"


def load_payload(name: str = "synthetic-66-fixed.json") -> dict[str, object]:
    """Load a maintained payload while preserving decimal tokens."""

    with (EXAMPLE_DIRECTORY / name).open("r", encoding="utf-8") as source:
        return json.load(source, parse_float=Decimal)


class TimeConversionTest(unittest.TestCase):
    """Exact seconds-to-nanoseconds behavior."""

    def test_exact_integer_and_fractional_seconds(self) -> None:
        self.assertEqual(seconds_to_nanoseconds(20, "time"), 20_000_000_000)
        self.assertEqual(
            seconds_to_nanoseconds(Decimal("1.000000001"), "time"),
            1_000_000_001,
        )

    def test_sub_nanosecond_precision_is_rejected(self) -> None:
        with self.assertRaisesRegex(ScenarioConfigError, "finer"):
            seconds_to_nanoseconds(Decimal("0.0000000001"), "time")

    def test_positive_and_signed_64_bit_bounds_are_enforced(self) -> None:
        with self.assertRaises(ScenarioConfigError):
            seconds_to_nanoseconds(0, "time", positive=True)
        with self.assertRaisesRegex(ScenarioConfigError, "64-bit"):
            seconds_to_nanoseconds(Decimal("9223372036.854775808"), "time")

    def test_binary_float_is_not_an_exact_scenario_number(self) -> None:
        with self.assertRaisesRegex(ScenarioConfigError, "JSON number"):
            seconds_to_nanoseconds(0.1, "time")


class ScenarioParsingTest(unittest.TestCase):
    """Closed-world and cross-field validation."""

    def test_fixed_66_satellite_example(self) -> None:
        config = load_scenario(EXAMPLE_DIRECTORY / "synthetic-66-fixed.json")
        self.assertEqual(config.schema_version, "0.2")
        self.assertEqual(config.constellation.satellite_count, 66)
        self.assertEqual(config.simulation.duration_ns, 1_000_000_000_000)
        self.assertEqual(config.network.network_update_interval_ns, 20_000_000_000)
        self.assertEqual(config.trace_export.interval_ns, 1_000_000_000)
        self.assertEqual(config.network.fixed_delay_us, 8000)

    def test_distance_example_uses_its_own_update_interval(self) -> None:
        config = load_scenario(EXAMPLE_DIRECTORY / "synthetic-66-distance.json")
        self.assertEqual(config.network.delay_mode, "distance")
        self.assertIsNone(config.network.fixed_delay_us)
        self.assertEqual(config.network.network_update_interval_ns, 1_000_000_000)

    def test_compute_and_task_inputs_are_direct_scenario_references(self) -> None:
        config = load_scenario(SCENARIO_FIXTURES / "task-input.json")
        self.assertEqual(
            config.workloads.compute_profile,
            SCENARIO_FIXTURES / "resources/compute-profile.json",
        )
        self.assertEqual(
            config.workloads.task_trace,
            SCENARIO_FIXTURES / "traffic/task-trace.json",
        )

    def test_replay_directory_and_cadence_come_from_scenario(self) -> None:
        config = load_scenario(SCENARIO_FIXTURES / "replay-dynamic.json")
        self.assertEqual(config.constellation.orbit_provider, "json-replay")
        self.assertEqual(config.network.topology_source, "json-replay")
        self.assertEqual(
            config.network.replay_directory,
            SCENARIO_FIXTURES.parent / "topology/snapshots/diamond-4-dynamic",
        )
        self.assertEqual(config.simulation.duration_ns, 5_000_000_000)
        self.assertEqual(config.network.network_update_interval_ns, 2_000_000_000)

    def test_unknown_and_missing_fields_are_rejected(self) -> None:
        unknown = load_payload()
        unknown["unexpected"] = True
        with self.assertRaisesRegex(ScenarioConfigError, "unknown"):
            parse_scenario(unknown)

        missing = load_payload()
        del missing["routing"]
        with self.assertRaisesRegex(ScenarioConfigError, "missing"):
            parse_scenario(missing)

    def test_delay_mode_condition_is_enforced(self) -> None:
        fixed_without_delay = load_payload()
        fixed_without_delay["network"]["fixed_delay_us"] = None
        with self.assertRaisesRegex(ScenarioConfigError, "fixed_delay_us"):
            parse_scenario(fixed_without_delay)

        distance_with_delay = load_payload("synthetic-66-distance.json")
        distance_with_delay["network"]["fixed_delay_us"] = 8000
        with self.assertRaisesRegex(ScenarioConfigError, "must be null"):
            parse_scenario(distance_with_delay)

    def test_topology_and_orbit_sources_must_agree(self) -> None:
        payload = load_payload()
        payload["constellation"]["orbit_provider"] = "json-replay"
        with self.assertRaisesRegex(ScenarioConfigError, "disagree"):
            parse_scenario(payload)

    def test_task_inputs_are_paired_and_resolved_from_scenario(self) -> None:
        payload = load_payload()
        payload["workloads"]["compute_profile"] = "resources/compute.json"
        payload["workloads"]["task_trace"] = "traffic/tasks.json"
        source_path = Path("/tmp/scenario-case/scenario.json")
        config = parse_scenario(payload, source_path=source_path)
        self.assertEqual(
            config.workloads.compute_profile,
            Path("/tmp/scenario-case/resources/compute.json"),
        )
        self.assertEqual(
            config.workloads.task_trace,
            Path("/tmp/scenario-case/traffic/tasks.json"),
        )

        unpaired = copy.deepcopy(payload)
        unpaired["workloads"]["task_trace"] = None
        with self.assertRaisesRegex(ScenarioConfigError, "provided together"):
            parse_scenario(unpaired, source_path=source_path)

    def test_transfer_and_task_inputs_cannot_be_mixed(self) -> None:
        payload = load_payload()
        payload["workloads"]["transfer_trace"] = "traffic/transfers.json"
        payload["workloads"]["compute_profile"] = "resources/compute.json"
        payload["workloads"]["task_trace"] = "traffic/tasks.json"
        with self.assertRaisesRegex(ScenarioConfigError, "cannot be mixed"):
            parse_scenario(payload)

    def test_boolean_cannot_substitute_for_integer(self) -> None:
        payload = load_payload()
        payload["randomness"]["seed"] = True
        with self.assertRaisesRegex(ScenarioConfigError, "integer"):
            parse_scenario(payload)

    def test_isl_queue_must_fit_ns3_queue_size(self) -> None:
        payload = load_payload()
        payload["network"]["isl_queue_bytes"] = 4_294_967_296
        with self.assertRaisesRegex(ScenarioConfigError, "integer in"):
            parse_scenario(payload)


class SchemaDocumentationTest(unittest.TestCase):
    """The JSON Schema is the para.cc-style parameter reference."""

    def test_every_declared_property_has_a_description(self) -> None:
        with SCHEMA_PATH.open("r", encoding="utf-8") as source:
            schema = json.load(source)

        object_schemas = [("root", schema)]
        object_schemas.extend(
            (f"$defs.{name}", definition)
            for name, definition in schema["$defs"].items()
            if "properties" in definition
        )
        for object_name, object_schema in object_schemas:
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
