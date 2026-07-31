#!/usr/bin/env python3
"""Agg tests for reusable orbit renderer artists and optional links."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

from contrib.satcompute.tools.visualization.orbit.configuration import (
    parse_config,
)
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


class OrbitRendererTest(unittest.TestCase):
    def _renderer(
        self,
        root: Path,
        *,
        show_links: bool,
        show_earth: bool = True,
        threshold: int = 100,
    ) -> OrbitRenderer:
        write_fixture(root, "dynamic")
        scenario = load_scenario(
            root,
            orbit_loader=lambda config: FakeOrbit(4),
            validate=False,
        )
        payload = valid_payload()
        payload["enabled"] = True
        payload["show_earth"] = show_earth
        payload["show_links"] = show_links
        payload["show_node_labels"] = True
        payload["detail_node_threshold"] = threshold
        return OrbitRenderer(scenario, parse_config(payload))

    def test_detailed_renderer_reuses_artists_for_full_timeline(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-orbit-renderer-"
        ) as temp:
            renderer = self._renderer(Path(temp), show_links=False)
            try:
                relay_artist = renderer.relay_scatter
                compute_artist = renderer.compute_scatter
                ring_artists = renderer.orbit_artists
                graticule_artist = renderer.earth_graticule_artist
                coordinate_labels = renderer.earth_coordinate_labels
                for time_s in (0.0, 10.0, 20.0):
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
                self.assertEqual(renderer.axes.get_xlabel(), "")
                self.assertEqual(renderer.axes.get_ylabel(), "")
                self.assertEqual(renderer.axes.get_zlabel(), "")
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
                self.assertEqual(summary["current_time_s"], 20.0)
            finally:
                renderer.close()

    def test_hiding_earth_also_hides_its_coordinates(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-orbit-no-earth-"
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

    def test_links_follow_held_snapshot_and_simplified_hides_labels(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-orbit-links-"
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
                renderer.update(19.0)
                self.assertEqual(renderer.link_count, 1)
                before = tuple(renderer.link_artist._segments3d)
                renderer.update(20.0)
                after = tuple(renderer.link_artist._segments3d)
                self.assertNotEqual(before, after)
                self.assertEqual(renderer.link_count, 1)
            finally:
                renderer.close()


if __name__ == "__main__":
    unittest.main()
