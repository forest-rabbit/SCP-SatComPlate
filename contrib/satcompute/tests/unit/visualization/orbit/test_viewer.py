#!/usr/bin/env python3
"""Tests for the default-off topology-trace viewer boundary."""

import builtins
from pathlib import Path
import unittest
from unittest.mock import patch

from contrib.satcompute.tests.unit.visualization.orbit._helpers import valid_payload
from contrib.satcompute.tools.visualization.orbit.configuration import parse_config
from contrib.satcompute.tools.visualization.orbit.viewer import (
    OrbitViewerError,
    _require_output_outside_trace,
    run_viewer,
)


class OrbitViewerBoundaryTest(unittest.TestCase):
    def test_disabled_config_has_zero_graphics_or_trace_side_effects(self):
        config = parse_config(valid_payload())
        original_import = builtins.__import__

        def guarded_import(name, *args, **kwargs):
            if name == "PIL" or name.startswith(("PIL.", "matplotlib", "numpy")):
                raise AssertionError(f"disabled viewer imported {name}")
            return original_import(name, *args, **kwargs)

        with patch(
            "contrib.satcompute.tools.visualization.orbit.viewer.load_config",
            return_value=config,
        ), patch("builtins.__import__", side_effect=guarded_import):
            summary = run_viewer(
                Path("/does/not/exist/trace"),
                Path("/does/not/exist/config.json"),
            )
        self.assertEqual(summary, {"enabled": False, "side_effects": 0})

    def test_output_must_remain_outside_trace(self):
        trace = Path("/tmp/satcompute-viewer-trace")
        with self.assertRaisesRegex(OrbitViewerError, "outside"):
            _require_output_outside_trace(trace, trace / "preview.gif")
        _require_output_outside_trace(
            trace,
            Path("/tmp/satcompute-viewer-preview.gif"),
        )


if __name__ == "__main__":
    unittest.main()
