#!/usr/bin/env python3
"""Tests for the N2 constellation configuration contract."""

from __future__ import annotations

import copy
import math
import sys
import unittest
from pathlib import Path


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from configuration import (  # noqa: E402
    ConstellationConfigError,
    load_config,
    parse_config,
)


PRESET = TOOL_DIR / "config" / "synthetic-66.json"


class ConstellationConfigurationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.config = load_config(PRESET)
        cls.payload = cls.config.input_dict()

    def test_synthetic_66_contract(self) -> None:
        config = self.config
        self.assertEqual(config.constellation_name, "synthetic-66")
        self.assertEqual(config.constellation_pattern, "walker-star")
        self.assertEqual(config.expected_satellite_count, 66)
        self.assertEqual(config.raan_span_deg, 180.0)
        self.assertEqual(config.raan_step_deg, 30.0)
        self.assertAlmostEqual(config.slot_spacing_deg, 360.0 / 11.0)
        self.assertAlmostEqual(config.phase_offset_deg, 180.0 / 11.0)
        self.assertEqual(config.phase_scheme, "alternating-half-slot")
        self.assertFalse(config.seam_enabled)
        self.assertEqual(config.max_isl_distance_m, 6174589)

    def test_pattern_contract_accepts_only_star_and_delta(self) -> None:
        delta = copy.deepcopy(self.payload)
        delta["constellation_pattern"] = "walker-delta"
        self.assertEqual(
            parse_config(delta).constellation_pattern,
            "walker-delta",
        )
        invalid = copy.deepcopy(self.payload)
        invalid["constellation_pattern"] = "walker"
        with self.assertRaisesRegex(
            ConstellationConfigError,
            "walker-star or walker-delta",
        ):
            parse_config(invalid)

    def test_constellation_name_accepts_only_safe_identifiers(self) -> None:
        for name in (
            "synthetic-66",
            "starlink_550",
            "walker.star",
            "A1",
        ):
            with self.subTest(name=name):
                payload = copy.deepcopy(self.payload)
                payload["constellation_name"] = name
                self.assertEqual(parse_config(payload).constellation_name, name)

        for name in (
            "",
            "synthetic 66",
            " synthetic-66",
            "synthetic-66 ",
            "synthetic/66",
            "synthetic\\66",
            "synthetic\n66",
            "synthetic\t66",
        ):
            with self.subTest(name=repr(name)):
                payload = copy.deepcopy(self.payload)
                payload["constellation_name"] = name
                with self.assertRaisesRegex(
                    ConstellationConfigError,
                    "constellation_name must match",
                ):
                    parse_config(payload)

    def test_phase_diff_must_be_boolean(self) -> None:
        invalid = copy.deepcopy(self.payload)
        invalid["phase_diff"] = 1
        with self.assertRaisesRegex(
            ConstellationConfigError,
            "phase_diff must be a boolean",
        ):
            parse_config(invalid)

        aligned = copy.deepcopy(self.payload)
        aligned["phase_diff"] = False
        config = parse_config(aligned)
        self.assertEqual(config.phase_offset_deg, 0.0)
        self.assertEqual(config.phase_scheme, "aligned")

    def test_missing_and_unknown_fields_fail(self) -> None:
        missing = copy.deepcopy(self.payload)
        del missing["altitude_km"]
        with self.assertRaisesRegex(
            ConstellationConfigError,
            "missing=\\['altitude_km'\\]",
        ):
            parse_config(missing)

        unknown = copy.deepcopy(self.payload)
        unknown["walker_phase_factor"] = 1
        with self.assertRaisesRegex(
            ConstellationConfigError,
            "unknown=\\['walker_phase_factor'\\]",
        ):
            parse_config(unknown)

    def test_numeric_types_and_ranges_are_strict(self) -> None:
        invalid_values = (
            ("num_orbits", True),
            ("satellites_per_orbit", 0),
            ("altitude_km", 0.0),
            ("altitude_km", -1.0),
            ("altitude_km", math.nan),
            ("altitude_km", math.inf),
            ("altitude_km", True),
            ("inclination_deg", -0.1),
            ("inclination_deg", 180.0),
            ("inclination_deg", math.nan),
            ("inclination_deg", math.inf),
            ("inclination_deg", True),
            ("max_isl_distance_m", 0),
            ("max_isl_distance_m", True),
        )
        for field, value in invalid_values:
            with self.subTest(field=field, value=value):
                invalid = copy.deepcopy(self.payload)
                invalid[field] = value
                with self.assertRaises(ConstellationConfigError):
                    parse_config(invalid)

    def test_physical_range_boundaries_are_explicit(self) -> None:
        payload = copy.deepcopy(self.payload)
        payload["altitude_km"] = 0.000001
        payload["inclination_deg"] = 0.0
        config = parse_config(payload)
        self.assertEqual(config.altitude_km, 0.000001)
        self.assertEqual(config.inclination_deg, 0.0)

        payload["inclination_deg"] = 179.999999
        self.assertEqual(
            parse_config(payload).inclination_deg,
            179.999999,
        )

    def test_resolving_same_payload_is_deterministic(self) -> None:
        first = parse_config(copy.deepcopy(self.payload))
        second = parse_config(copy.deepcopy(self.payload))
        self.assertEqual(first, second)
        self.assertEqual(first.input_dict(), second.input_dict())


if __name__ == "__main__":
    unittest.main()
