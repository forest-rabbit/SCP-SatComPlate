#!/usr/bin/env python3
"""Tiny Pillow-readable GIF smoke for at most three frames."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

from PIL import Image

from contrib.satcompute.tools.visualization.orbit.configuration import (
    parse_config,
)
from contrib.satcompute.tools.visualization.orbit.gif_export import export_gif
from contrib.satcompute.tools.visualization.orbit.renderer import OrbitRenderer
from contrib.satcompute.tools.visualization.orbit.scenario_reader import (
    load_scenario,
)
from contrib.satcompute.tools.visualization.orbit.tests.test_configuration import (
    valid_payload,
)
from contrib.satcompute.tools.visualization.orbit.tests.test_scenario_reader import (
    FakeOrbit,
    write_fixture,
)


class OrbitGifExportTest(unittest.TestCase):
    def test_three_frame_gif_is_readable_by_pillow(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-orbit-gif-"
        ) as temp:
            root = Path(temp)
            scenario_root = root / "scenario"
            write_fixture(scenario_root, "dynamic")
            scenario = load_scenario(
                scenario_root,
                orbit_loader=lambda config: FakeOrbit(4),
                validate=False,
            )
            payload = valid_payload()
            payload["enabled"] = True
            renderer = OrbitRenderer(scenario, parse_config(payload))
            output = root / "preview.gif"
            try:
                export_gif(
                    renderer,
                    (0.0, 10.0, 20.0),
                    output,
                    playback_interval_ms=100,
                )
            finally:
                renderer.close()

            self.assertTrue(output.is_file())
            with Image.open(output) as image:
                self.assertEqual(image.format, "GIF")
                self.assertEqual(image.n_frames, 3)


if __name__ == "__main__":
    unittest.main()
