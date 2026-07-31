#!/usr/bin/env python3
"""Tests for interval-study configuration and orbital window derivation."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tools.analysis.topology_interval.run_interval_study import (
    load_study_preset,
    orbital_period_seconds,
    orbital_window_offsets,
    study_window_offsets,
    write_window_config,
)
from contrib.satcompute.tools.generation.scenario.configuration import (
    DynamicSchedule,
    load_config,
)


class RunIntervalStudyTest(unittest.TestCase):
    def test_frozen_periods_and_window_offsets(self) -> None:
        expected = {
            "66": (6027.130743814793, (0, 2009, 4018)),
            "351": (6326.357647436802, (0, 2109, 4218)),
            "720": (6565.2957073948755, (0, 2188, 4377)),
        }
        for key, (period_s, offsets) in expected.items():
            with self.subTest(key=key):
                preset = load_study_preset(key)
                self.assertAlmostEqual(
                    orbital_period_seconds(preset.altitude_km),
                    period_s,
                )
                self.assertEqual(
                    orbital_window_offsets(preset.altitude_km),
                    offsets,
                )

    def test_frozen_study_window_scope(self) -> None:
        expected = {
            "66": (0, 2009, 4018),
            "351": (0, 2109, 4218),
            "720": (0,),
        }
        for key, offsets in expected.items():
            with self.subTest(key=key):
                self.assertEqual(
                    study_window_offsets(load_study_preset(key)),
                    offsets,
                )

    def test_window_config_changes_only_the_schedule_window(self) -> None:
        preset = load_study_preset("351")
        source = json.loads(preset.path.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(
            prefix="satcompute-window-config-"
        ) as temp:
            output = Path(temp) / "window.json"
            write_window_config(preset, 120, 2109, output)
            generated = json.loads(output.read_text(encoding="utf-8"))
            expected = source
            expected["topology"]["schedule"] = {
                "start_time_s": 0,
                "orbit_sample_offset_s": 2109,
                "duration_s": 120,
                "step_s": 1,
            }
            self.assertEqual(generated, expected)
            config = load_config(output)
            self.assertEqual(
                config.topology.schedule,
                DynamicSchedule(0, 2109, 120, 1),
            )
            write_window_config(preset, 120, 2109, output)


if __name__ == "__main__":
    unittest.main()
