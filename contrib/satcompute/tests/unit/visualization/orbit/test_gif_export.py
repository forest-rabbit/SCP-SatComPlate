#!/usr/bin/env python3
"""Tiny Pillow-readable GIF test for topology-trace frames."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tests.unit.visualization.orbit._helpers import (
    valid_payload,
    write_trace,
)
from contrib.satcompute.tools.visualization.orbit.scenario_reader import (
    load_trace,
)

try:
    import matplotlib

    matplotlib.use("Agg")

    from PIL import Image

    from contrib.satcompute.tools.visualization.orbit.configuration import (
        parse_config,
    )
    from contrib.satcompute.tools.visualization.orbit.gif_export import export_gif
    from contrib.satcompute.tools.visualization.orbit.renderer import OrbitRenderer

    VISUALIZATION_DEPENDENCIES = True
except ModuleNotFoundError:
    VISUALIZATION_DEPENDENCIES = False


@unittest.skipUnless(
    VISUALIZATION_DEPENDENCIES,
    "Matplotlib, NumPy, and Pillow are optional visualization dependencies",
)
class OrbitGifExportTest(unittest.TestCase):
    def test_three_frame_gif_is_readable_by_pillow(self):
        with tempfile.TemporaryDirectory(
            prefix="satcompute-trace-gif-"
        ) as temp:
            root = Path(temp)
            trace_root = root / "trace"
            compute_profile = write_trace(trace_root)
            trace = load_trace(trace_root, compute_profile=compute_profile)
            payload = valid_payload()
            payload["enabled"] = True
            renderer = OrbitRenderer(trace, parse_config(payload))
            output = root / "preview.gif"
            try:
                export_gif(
                    renderer,
                    (0.0, 1.0, 2.0),
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
