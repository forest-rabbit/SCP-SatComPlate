#!/usr/bin/env python3
"""Tests for the closed-world unified scenario configuration."""

from __future__ import annotations

import copy
import json
import unittest
from pathlib import Path

from contrib.satcompute.tools.generation.scenario.configuration import (
    DYNAMIC_MODE,
    STATIC_MODE,
    DynamicSchedule,
    ScenarioConfigError,
    StaticSchedule,
    load_config,
    parse_config,
)
from contrib.satcompute.tests.support.paths import SCENARIO_GENERATION_ROOT


SCENARIO_ROOT = SCENARIO_GENERATION_ROOT
PRESET = SCENARIO_ROOT / "config" / "synthetic-66-compute-22.json"
RESEARCH_PRESETS = (
    (
        "synthetic-66-compute-22.json",
        "synthetic-66-compute-22",
        6,
        11,
        22,
        121,
    ),
    (
        "synthetic-351-telesat-t1-compute-117.json",
        "synthetic-351-telesat-t1-compute-117",
        27,
        13,
        117,
        689,
    ),
    (
        "synthetic-720-oneweb-compute-240.json",
        "synthetic-720-oneweb-compute-240",
        18,
        40,
        240,
        1400,
    ),
)


def preset_payload() -> dict:
    return json.loads(PRESET.read_text(encoding="utf-8"))


class ScenarioConfigurationTest(unittest.TestCase):
    def test_interval_study_presets_share_the_frozen_contract(self) -> None:
        for (
            filename,
            scenario_name,
            num_orbits,
            satellites_per_orbit,
            compute_node_count,
            candidate_count,
        ) in RESEARCH_PRESETS:
            with self.subTest(filename=filename):
                config = load_config(SCENARIO_ROOT / "config" / filename)
                self.assertEqual(config.scenario_name, scenario_name)
                self.assertEqual(config.constellation.num_orbits, num_orbits)
                self.assertEqual(
                    config.constellation.satellites_per_orbit,
                    satellites_per_orbit,
                )
                self.assertEqual(
                    config.total_satellite_count,
                    num_orbits * satellites_per_orbit,
                )
                self.assertEqual(
                    config.compute.compute_node_count,
                    compute_node_count,
                )
                self.assertEqual(
                    (
                        config.total_satellite_count
                        + (num_orbits - 1) * satellites_per_orbit
                    ),
                    candidate_count,
                )
                self.assertEqual(config.topology.mode, DYNAMIC_MODE)
                self.assertEqual(
                    config.topology.schedule,
                    DynamicSchedule(0, 0, 1000, 1),
                )
                self.assertEqual(
                    config.topology.isl_candidate_strategy,
                    "plus-grid",
                )
                self.assertFalse(config.topology.seam_enabled)
                self.assertEqual(config.topology.delay_mode, "fixed")
                self.assertEqual(config.topology.fixed_delay_us, 8000)
                self.assertEqual(
                    config.topology.link_bandwidth_kbps,
                    2_000_000,
                )
                self.assertEqual(
                    config.compute.compute_rate_work_units_per_second,
                    1_500_000,
                )

    def test_synthetic_66_compute_22_contract(self) -> None:
        config = load_config(PRESET)
        self.assertEqual(config.schema_version, "0.1")
        self.assertEqual(config.scenario_name, "synthetic-66-compute-22")
        self.assertEqual(config.total_satellite_count, 66)
        self.assertEqual(config.constellation.num_orbits, 6)
        self.assertEqual(config.constellation.satellites_per_orbit, 11)
        self.assertEqual(config.topology.mode, DYNAMIC_MODE)
        self.assertEqual(
            config.topology.schedule,
            DynamicSchedule(0, 0, 1000, 1),
        )
        self.assertEqual(config.topology.delay_mode, "fixed")
        self.assertEqual(config.topology.fixed_delay_us, 8000)
        self.assertEqual(config.topology.link_bandwidth_kbps, 2_000_000)
        self.assertEqual(config.compute.compute_node_count, 22)
        self.assertEqual(
            config.compute.compute_rate_work_units_per_second,
            1_500_000,
        )
        self.assertEqual(config.input_dict(), preset_payload())

    def test_static_fixed_contract(self) -> None:
        payload = preset_payload()
        payload["topology"].update(
            {
                "mode": "static",
                "schedule": {"snapshot_time_s": 17},
                "delay_mode": "fixed",
                "fixed_delay_us": 8000,
            }
        )
        config = parse_config(payload)
        self.assertEqual(config.topology.mode, STATIC_MODE)
        self.assertEqual(config.topology.schedule, StaticSchedule(17))
        self.assertEqual(config.topology.delay_mode, "fixed")
        self.assertEqual(config.topology.fixed_delay_us, 8000)
        self.assertEqual(config.input_dict(), payload)

    def test_schedule_fields_are_mode_conditional(self) -> None:
        cases = (
            (
                "static",
                {
                    "start_time_s": 0,
                    "orbit_sample_offset_s": 0,
                    "duration_s": 10,
                    "step_s": 1,
                },
            ),
            ("dynamic", {"snapshot_time_s": 0}),
            (
                "dynamic",
                {
                    "start_time_s": 0,
                    "orbit_sample_offset_s": 0,
                    "duration_s": 10,
                    "step_s": 1,
                    "snapshot_time_s": 0,
                },
            ),
        )
        for mode, schedule in cases:
            with self.subTest(mode=mode, schedule=schedule):
                payload = preset_payload()
                payload["topology"]["mode"] = mode
                payload["topology"]["schedule"] = schedule
                with self.assertRaisesRegex(
                    ScenarioConfigError,
                    "schedule.*fields differ",
                ):
                    parse_config(payload)

    def test_dynamic_schedule_is_strict(self) -> None:
        invalid_schedules = (
            {
                "start_time_s": 1,
                "orbit_sample_offset_s": 0,
                "duration_s": 10,
                "step_s": 1,
            },
            {
                "start_time_s": 0,
                "orbit_sample_offset_s": -1,
                "duration_s": 10,
                "step_s": 1,
            },
            {
                "start_time_s": 0,
                "orbit_sample_offset_s": True,
                "duration_s": 10,
                "step_s": 1,
            },
            {
                "start_time_s": 0,
                "orbit_sample_offset_s": 0,
                "duration_s": -1,
                "step_s": 1,
            },
            {
                "start_time_s": 0,
                "orbit_sample_offset_s": 0,
                "duration_s": 10,
                "step_s": 0,
            },
            {
                "start_time_s": 0,
                "orbit_sample_offset_s": 0,
                "duration_s": 10,
                "step_s": 3,
            },
            {
                "start_time_s": False,
                "orbit_sample_offset_s": 0,
                "duration_s": 10,
                "step_s": 1,
            },
            {
                "start_time_s": 0,
                "orbit_sample_offset_s": (1 << 63) - 5,
                "duration_s": 10,
                "step_s": 1,
            },
        )
        for schedule in invalid_schedules:
            with self.subTest(schedule=schedule):
                payload = preset_payload()
                payload["topology"]["schedule"] = schedule
                with self.assertRaises(ScenarioConfigError):
                    parse_config(payload)

    def test_delay_fields_are_mode_conditional(self) -> None:
        cases = (
            ("fixed", None),
            ("fixed", -1),
            ("fixed", True),
            ("distance", 8000),
            ("other", None),
        )
        for delay_mode, fixed_delay_us in cases:
            with self.subTest(
                delay_mode=delay_mode,
                fixed_delay_us=fixed_delay_us,
            ):
                payload = preset_payload()
                payload["topology"]["delay_mode"] = delay_mode
                payload["topology"]["fixed_delay_us"] = fixed_delay_us
                with self.assertRaises(ScenarioConfigError):
                    parse_config(payload)

    def test_all_objects_are_closed_world(self) -> None:
        object_paths = (
            (),
            ("constellation",),
            ("topology",),
            ("topology", "schedule"),
            ("compute",),
        )
        for path in object_paths:
            with self.subTest(path=path):
                payload = preset_payload()
                target = payload
                for field in path:
                    target = target[field]
                target["unexpected"] = 1
                with self.assertRaisesRegex(
                    ScenarioConfigError,
                    "unknown=.*unexpected",
                ):
                    parse_config(payload)

    def test_boolean_values_cannot_impersonate_integers(self) -> None:
        paths = (
            ("constellation", "num_orbits"),
            ("topology", "max_isl_distance_m"),
            ("topology", "schedule", "orbit_sample_offset_s"),
            ("topology", "link_bandwidth_kbps"),
            ("compute", "compute_node_count"),
            ("compute", "compute_rate_work_units_per_second"),
        )
        for path in paths:
            with self.subTest(path=path):
                payload = preset_payload()
                target = payload
                for field in path[:-1]:
                    target = target[field]
                target[path[-1]] = True
                with self.assertRaises(ScenarioConfigError):
                    parse_config(payload)

    def test_compute_bounds_and_enums_are_enforced(self) -> None:
        cases = (
            ("compute_node_count", 0),
            ("compute_node_count", 67),
            ("compute_rate_work_units_per_second", 0),
            ("placement_strategy", "random"),
        )
        for field, value in cases:
            with self.subTest(field=field, value=value):
                payload = preset_payload()
                payload["compute"][field] = value
                with self.assertRaises(ScenarioConfigError):
                    parse_config(payload)

        payload = preset_payload()
        payload["topology"]["isl_candidate_strategy"] = "nearest"
        with self.assertRaises(ScenarioConfigError):
            parse_config(payload)

    def test_names_numbers_and_root_types_are_strict(self) -> None:
        for name in ("", "../unsafe", "space name", "_leading"):
            with self.subTest(name=name):
                payload = preset_payload()
                payload["scenario_name"] = name
                with self.assertRaises(ScenarioConfigError):
                    parse_config(payload)

        for value in (float("inf"), float("-inf"), float("nan")):
            with self.subTest(value=value):
                payload = preset_payload()
                payload["constellation"]["altitude_km"] = value
                with self.assertRaises(ScenarioConfigError):
                    parse_config(payload)

        for value in (None, [], "scenario"):
            with self.subTest(root=value):
                with self.assertRaises(ScenarioConfigError):
                    parse_config(value)

    def test_input_payload_is_not_mutated(self) -> None:
        payload = preset_payload()
        original = copy.deepcopy(payload)
        parse_config(payload)
        self.assertEqual(payload, original)


if __name__ == "__main__":
    unittest.main()
