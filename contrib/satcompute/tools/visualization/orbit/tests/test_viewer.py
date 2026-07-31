#!/usr/bin/env python3
"""Tests for the default-off viewer boundary."""

from __future__ import annotations

import builtins
import unittest
from pathlib import Path
from unittest.mock import patch

from contrib.satcompute.tools.visualization.orbit.configuration import (
    parse_config,
)
from contrib.satcompute.tools.visualization.orbit.tests.test_configuration import (
    valid_payload,
)
from contrib.satcompute.tools.visualization.orbit.viewer import (
    OrbitViewerError,
    _require_gif_outside_scenario,
    run_viewer,
)


class OrbitViewerBoundaryTest(unittest.TestCase):
    def test_disabled_config_has_zero_graphics_or_scenario_side_effects(self) -> None:
        config = parse_config(valid_payload())
        original_import = builtins.__import__

        def guarded_import(name, *args, **kwargs):
            if name == "PIL" or name.startswith(("PIL.", "matplotlib")):
                raise AssertionError(f"disabled viewer imported {name}")
            return original_import(name, *args, **kwargs)

        with patch(
            "contrib.satcompute.tools.visualization.orbit.viewer.load_config",
            return_value=config,
        ), patch("builtins.__import__", side_effect=guarded_import):
            summary = run_viewer(
                Path("/does/not/exist/scenario"),
                Path("/does/not/exist/config.json"),
            )
        self.assertEqual(summary, {"enabled": False, "side_effects": 0})

    def test_gif_must_remain_outside_scenario(self) -> None:
        scenario = Path("/tmp/satcompute-viewer-scenario")
        with self.assertRaisesRegex(OrbitViewerError, "outside"):
            _require_gif_outside_scenario(
                scenario,
                scenario / "preview.gif",
            )
        _require_gif_outside_scenario(
            scenario,
            Path("/tmp/satcompute-viewer-preview.gif"),
        )


if __name__ == "__main__":
    unittest.main()
