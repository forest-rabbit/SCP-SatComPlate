#!/usr/bin/env python3
"""Agg tests for reusable topology-trace renderer artists."""

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

    from matplotlib.colors import to_rgba

    from contrib.satcompute.tools.visualization.orbit.configuration import (
        parse_config,
    )
    from contrib.satcompute.tools.visualization.orbit.renderer import (
        ISL_COLOR,
        ORBIT_COLOR,
        OrbitRenderer,
    )

    VISUALIZATION_DEPENDENCIES = True
except ModuleNotFoundError:
    VISUALIZATION_DEPENDENCIES = False


@unittest.skipUnless(
    VISUALIZATION_DEPENDENCIES,
    "Matplotlib and NumPy are optional visualization dependencies",
)
class OrbitRendererTest(unittest.TestCase):
    def _renderer(
        self,
        root: Path,
        *,
        show_links: bool,
        show_earth: bool = True,
        threshold: int = 100,
    ) -> OrbitRenderer:
        trace_root = root / "trace"
        compute_profile = write_trace(trace_root)
        trace = load_trace(trace_root, compute_profile=compute_profile)
        payload = valid_payload()
        payload["enabled"] = True
        payload["show_earth"] = show_earth
        payload["show_links"] = show_links
        payload["show_node_labels"] = True
        payload["detail_node_threshold"] = threshold
        return OrbitRenderer(trace, parse_config(payload))

    def test_detailed_renderer_reuses_artists_for_full_timeline(self):
        with tempfile.TemporaryDirectory(
            prefix="satcompute-trace-renderer-"
        ) as temp:
            renderer = self._renderer(Path(temp), show_links=False)
            try:
                relay_artist = renderer.relay_scatter
                compute_artist = renderer.compute_scatter
                ring_artists = renderer.orbit_artists
                graticule_artist = renderer.earth_graticule_artist
                coordinate_labels = renderer.earth_coordinate_labels
                for time_s in (0.0, 1.0, 2.0):
                    renderer.update(time_s)
                    renderer.figure.canvas.draw()
                summary = renderer.summary()
                self.assertIs(renderer.relay_scatter, relay_artist)
                self.assertIs(renderer.compute_scatter, compute_artist)
                self.assertEqual(renderer.orbit_artists, ring_artists)
                self.assertIs(
                    renderer.earth_graticule_artist,
                    graticule_artist,
                )
                self.assertEqual(
                    renderer.earth_coordinate_labels,
                    coordinate_labels,
                )
                self.assertFalse(renderer.axes.axison)
                self.assertEqual(summary["display_mode"], "detailed")
                self.assertEqual(summary["node_count"], 4)
                self.assertEqual(summary["compute_node_count"], 2)
                self.assertEqual(summary["relay_node_count"], 2)
                self.assertEqual(summary["earth_graticule_count"], 17)
                self.assertEqual(
                    summary["earth_coordinate_label_count"],
                    13,
                )
                self.assertEqual(summary["orbit_artist_count"], 2)
                self.assertEqual(summary["node_label_count"], 4)
                self.assertFalse(summary["show_links"])
                self.assertEqual(summary["current_time_s"], 2.0)
            finally:
                renderer.close()

    def test_hiding_earth_also_hides_its_coordinates(self):
        with tempfile.TemporaryDirectory(
            prefix="satcompute-trace-no-earth-"
        ) as temp:
            renderer = self._renderer(
                Path(temp),
                show_links=False,
                show_earth=False,
            )
            try:
                self.assertIsNone(renderer.earth_artist)
                self.assertIsNone(renderer.earth_graticule_artist)
                self.assertEqual(renderer.earth_coordinate_labels, ())
                summary = renderer.summary()
                self.assertEqual(summary["earth_graticule_count"], 0)
                self.assertEqual(
                    summary["earth_coordinate_label_count"],
                    0,
                )
            finally:
                renderer.close()

    def test_links_follow_held_trace_and_simplified_hides_labels(self):
        with tempfile.TemporaryDirectory(
            prefix="satcompute-trace-links-"
        ) as temp:
            renderer = self._renderer(
                Path(temp),
                show_links=True,
                threshold=2,
            )
            try:
                self.assertEqual(renderer.display_mode, "simplified")
                self.assertEqual(len(renderer.node_labels), 0)
                self.assertIsNotNone(renderer.link_artist)
                renderer.update(1.9)
                self.assertEqual(renderer.link_count, 2)
                self.assertEqual(
                    renderer.orbit_artists[0].get_color(),
                    ORBIT_COLOR,
                )
                self.assertEqual(
                    tuple(renderer.link_artist.get_colors()[0]),
                    to_rgba(ISL_COLOR, renderer.style.link_alpha),
                )
                self.assertNotEqual(ORBIT_COLOR, ISL_COLOR)
                before = tuple(renderer.link_artist._segments3d)
                renderer.update(2.0)
                after = tuple(renderer.link_artist._segments3d)
                self.assertNotEqual(before, after)
                self.assertEqual(renderer.link_count, 2)
            finally:
                renderer.close()


if __name__ == "__main__":
    unittest.main()
