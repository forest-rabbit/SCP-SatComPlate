#!/usr/bin/env python3
"""Tests for the closed-world orbit-visualization configuration."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tools.visualization.orbit.configuration import (
    DETAILED,
    SIMPLIFIED,
    OrbitVisualizationConfigError,
    parse_config,
)
from contrib.satcompute.tests.unit.visualization.orbit._helpers import (
    valid_payload,
)


class OrbitVisualizationConfigurationTest(unittest.TestCase):
    def test_auto_mode_uses_the_configured_threshold(self) -> None:
        config = parse_config(valid_payload())
        self.assertEqual(config.resolved_display_mode(66), DETAILED)
        self.assertEqual(config.resolved_display_mode(100), DETAILED)
        self.assertEqual(config.resolved_display_mode(351), SIMPLIFIED)
        self.assertEqual(config.resolved_display_mode(720), SIMPLIFIED)

    def test_closed_world_rejects_missing_and_unknown_fields(self) -> None:
        missing = valid_payload()
        missing.pop("render_step_s")
        with self.assertRaisesRegex(
            OrbitVisualizationConfigError,
            "missing=.*render_step_s",
        ):
            parse_config(missing)

        unknown = valid_payload()
        unknown["server_port"] = 8080
        with self.assertRaisesRegex(
            OrbitVisualizationConfigError,
            "unknown=.*server_port",
        ):
            parse_config(unknown)

    def test_display_and_positive_value_contracts_are_strict(self) -> None:
        for field, value in (
            ("display_mode", "full"),
            ("render_step_s", 0),
            ("playback_interval_ms", -1),
            ("detail_node_threshold", True),
            ("gif_frame_step_s", float("inf")),
        ):
            with self.subTest(field=field):
                payload = valid_payload()
                payload[field] = value
                with self.assertRaises(OrbitVisualizationConfigError):
                    parse_config(payload)

    def test_gif_path_contract_and_relative_resolution(self) -> None:
        payload = valid_payload()
        payload.pop("gif_path")
        self.assertIsNone(parse_config(payload).gif_path)

        payload = valid_payload()
        payload["gif_path"] = "unexpected.gif"
        with self.assertRaisesRegex(
            OrbitVisualizationConfigError,
            "gif_path",
        ):
            parse_config(payload)

        with tempfile.TemporaryDirectory(
            prefix="satcompute-visualization-config-"
        ) as temp:
            payload = valid_payload()
            payload["export_gif"] = True
            payload["gif_path"] = "preview.gif"
            config = parse_config(payload, base_dir=Path(temp))
            self.assertEqual(
                config.gif_path,
                (Path(temp) / "preview.gif").resolve(),
            )

        for gif_path in (None, "", "preview.png"):
            with self.subTest(gif_path=gif_path):
                payload = valid_payload()
                payload["export_gif"] = True
                payload["gif_path"] = gif_path
                with self.assertRaises(OrbitVisualizationConfigError):
                    parse_config(payload)


if __name__ == "__main__":
    unittest.main()
