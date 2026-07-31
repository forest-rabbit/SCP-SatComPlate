#!/usr/bin/env python3
"""Reusable Matplotlib artists for one Earth-fixed orbit frame."""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any

import matplotlib.pyplot as plt
import numpy as np
from mpl_toolkits.mplot3d.art3d import Line3DCollection

from .configuration import (
    DETAILED,
    OrbitVisualizationConfig,
)
from .geometry import (
    EARTH_RADIUS_KM,
    earth_coordinate_labels_km,
    earth_graticule_segments_km,
    fit_orbit_ring,
)
from .scenario_reader import OrbitScenario, PositionKm


@dataclass(frozen=True)
class RenderStyle:
    """Centralized scale-specific visual weights."""

    compute_size: float
    relay_size: float
    orbit_line_width: float
    orbit_alpha: float
    link_line_width: float
    link_alpha: float


DETAILED_STYLE = RenderStyle(
    compute_size=36.0,
    relay_size=18.0,
    orbit_line_width=0.9,
    orbit_alpha=0.5,
    link_line_width=0.7,
    link_alpha=0.35,
)
SIMPLIFIED_STYLE = RenderStyle(
    compute_size=13.0,
    relay_size=5.0,
    orbit_line_width=0.4,
    orbit_alpha=0.2,
    link_line_width=0.25,
    link_alpha=0.12,
)


class OrbitRenderer:
    """Own stable artists and update only frame-dependent coordinates."""

    def __init__(
        self,
        scenario: OrbitScenario,
        config: OrbitVisualizationConfig,
    ) -> None:
        self.scenario = scenario
        self.config = config
        self.display_mode = config.resolved_display_mode(
            scenario.node_count
        )
        self.style = (
            DETAILED_STYLE
            if self.display_mode == DETAILED
            else SIMPLIFIED_STYLE
        )
        self.labels_visible = (
            self.display_mode == DETAILED and config.show_node_labels
        )
        self.figure = plt.figure(figsize=(10, 8))
        self.axes = self.figure.add_subplot(111, projection="3d")
        self.figure.subplots_adjust(bottom=0.16)
        self.axes.set_box_aspect((1.0, 1.0, 1.0))
        self.axes.set_axis_off()
        self.axes.set_title("SatCompute orbit visualization")
        self.current_time_s = 0.0
        self.current_positions: tuple[PositionKm, ...] = ()
        self.link_count = 0
        self.earth_graticule_count = 0

        if config.show_earth:
            self.earth_artist = self._create_earth()
            self.earth_graticule_artist = self._create_earth_graticule()
            self.earth_coordinate_labels = self._create_earth_labels()
        else:
            self.earth_artist = None
            self.earth_graticule_artist = None
            self.earth_coordinate_labels = ()

        positions = scenario.positions_at(0.0)
        self._set_equal_limits(positions)
        self.relay_scatter = self._create_scatter(
            positions,
            scenario.relay_node_ids,
            color="#8da0cb",
            size=self.style.relay_size,
            label="Relay satellite",
        )
        self.compute_scatter = self._create_scatter(
            positions,
            scenario.compute_node_ids,
            color="#e34a33",
            size=self.style.compute_size,
            label="Compute satellite",
        )
        self.orbit_artists = self._create_orbit_artists(positions)
        self.link_artist = self._create_link_artist(positions)
        self.node_labels = self._create_node_labels(positions)
        self.time_artist = self.axes.text2D(
            0.02,
            0.97,
            "",
            transform=self.axes.transAxes,
            va="top",
        )
        self.axes.legend(loc="upper right")
        self.update(0.0)

    def _create_earth(self) -> Any:
        longitude = np.linspace(0.0, 2.0 * np.pi, 48)
        latitude = np.linspace(-np.pi / 2.0, np.pi / 2.0, 25)
        longitude_grid, latitude_grid = np.meshgrid(longitude, latitude)
        x = EARTH_RADIUS_KM * np.cos(latitude_grid) * np.cos(longitude_grid)
        y = EARTH_RADIUS_KM * np.cos(latitude_grid) * np.sin(longitude_grid)
        z = EARTH_RADIUS_KM * np.sin(latitude_grid)
        return self.axes.plot_surface(
            x,
            y,
            z,
            color="#4f81bd",
            alpha=0.22,
            linewidth=0.0,
            antialiased=True,
            shade=True,
        )

    def _create_earth_graticule(self) -> Line3DCollection:
        segments = earth_graticule_segments_km()
        artist = Line3DCollection(
            segments,
            colors="#d9edf7",
            linewidths=0.45,
            alpha=0.55,
        )
        self.axes.add_collection3d(artist)
        self.earth_graticule_count = len(segments)
        return artist

    def _create_earth_labels(self) -> tuple[Any, ...]:
        return tuple(
            self.axes.text(
                *position,
                text,
                color="#17365d",
                fontsize=7,
                ha="center",
                va="center",
            )
            for text, position in earth_coordinate_labels_km()
        )

    def _set_equal_limits(self, positions: tuple[PositionKm, ...]) -> None:
        maximum_radius = max(
            math.sqrt(sum(value * value for value in position.xyz_km))
            for position in positions
        )
        limit = max(EARTH_RADIUS_KM, maximum_radius) * 1.08
        self.axes.set_xlim(-limit, limit)
        self.axes.set_ylim(-limit, limit)
        self.axes.set_zlim(-limit, limit)

    @staticmethod
    def _coordinates(
        positions: tuple[PositionKm, ...],
        node_ids: tuple[int, ...],
    ) -> tuple[list[float], list[float], list[float]]:
        selected = tuple(positions[node_id] for node_id in node_ids)
        return (
            [position.x_km for position in selected],
            [position.y_km for position in selected],
            [position.z_km for position in selected],
        )

    def _create_scatter(
        self,
        positions: tuple[PositionKm, ...],
        node_ids: tuple[int, ...],
        *,
        color: str,
        size: float,
        label: str,
    ) -> Any:
        x, y, z = self._coordinates(positions, node_ids)
        return self.axes.scatter(
            x,
            y,
            z,
            s=size,
            c=color,
            depthshade=False,
            label=label,
        )

    def _create_orbit_artists(
        self,
        positions: tuple[PositionKm, ...],
    ) -> tuple[Any, ...]:
        if not self.config.show_orbits:
            return ()
        artists = []
        for orbit_index in range(self.scenario.num_orbits):
            ring = fit_orbit_ring(
                self.scenario.orbit_positions(positions, orbit_index)
            )
            x, y, z = zip(*ring.points_km)
            artist, = self.axes.plot(
                x,
                y,
                z,
                color="#666666",
                linewidth=self.style.orbit_line_width,
                alpha=self.style.orbit_alpha,
            )
            artists.append(artist)
        return tuple(artists)

    def _link_segments(
        self,
        positions: tuple[PositionKm, ...],
        time_s: float,
    ) -> list[tuple[tuple[float, float, float], ...]]:
        return [
            (
                positions[node1_id].xyz_km,
                positions[node2_id].xyz_km,
            )
            for node1_id, node2_id in self.scenario.links_at(time_s)
        ]

    def _create_link_artist(
        self,
        positions: tuple[PositionKm, ...],
    ) -> Line3DCollection | None:
        if not self.config.show_links:
            return None
        segments = self._link_segments(positions, 0.0)
        artist = Line3DCollection(
            segments,
            colors="#4d4d4d",
            linewidths=self.style.link_line_width,
            alpha=self.style.link_alpha,
        )
        self.axes.add_collection3d(artist)
        self.link_count = len(segments)
        return artist

    def _create_node_labels(
        self,
        positions: tuple[PositionKm, ...],
    ) -> tuple[Any, ...]:
        if not self.labels_visible:
            return ()
        return tuple(
            self.axes.text(
                position.x_km,
                position.y_km,
                position.z_km,
                str(position.node_id),
                fontsize=6,
            )
            for position in positions
        )

    def update(self, time_s: float) -> tuple[Any, ...]:
        """Update all frame-dependent artists in place."""
        positions = self.scenario.positions_at(time_s)
        relay_xyz = self._coordinates(
            positions,
            self.scenario.relay_node_ids,
        )
        compute_xyz = self._coordinates(
            positions,
            self.scenario.compute_node_ids,
        )
        self.relay_scatter._offsets3d = relay_xyz
        self.compute_scatter._offsets3d = compute_xyz

        for orbit_index, artist in enumerate(self.orbit_artists):
            ring = fit_orbit_ring(
                self.scenario.orbit_positions(positions, orbit_index)
            )
            x, y, z = zip(*ring.points_km)
            artist.set_data_3d(x, y, z)

        if self.link_artist is not None:
            segments = self._link_segments(positions, time_s)
            self.link_artist.set_segments(segments)
            self.link_count = len(segments)

        for label, position in zip(self.node_labels, positions):
            label.set_position((position.x_km, position.y_km))
            label.set_3d_properties(position.z_km)

        physical_time = self.scenario.physical_time_s(time_s)
        self.time_artist.set_text(
            f"simulation time: {time_s:g} s\n"
            f"physical orbit time: {physical_time:g} s"
        )
        self.current_time_s = float(time_s)
        self.current_positions = positions
        artists = [self.relay_scatter, self.compute_scatter, self.time_artist]
        artists.extend(self.orbit_artists)
        artists.extend(self.node_labels)
        if self.link_artist is not None:
            artists.append(self.link_artist)
        return tuple(artists)

    def summary(self) -> dict[str, int | float | str | bool]:
        """Return stable counts for headless smoke tests and CLI output."""
        return {
            "display_mode": self.display_mode,
            "node_count": self.scenario.node_count,
            "compute_node_count": len(self.scenario.compute_node_ids),
            "relay_node_count": len(self.scenario.relay_node_ids),
            "earth_graticule_count": self.earth_graticule_count,
            "earth_coordinate_label_count": len(
                self.earth_coordinate_labels
            ),
            "orbit_artist_count": len(self.orbit_artists),
            "node_label_count": len(self.node_labels),
            "show_links": self.link_artist is not None,
            "active_link_count": self.link_count,
            "current_time_s": self.current_time_s,
        }

    def close(self) -> None:
        """Close this renderer's figure without affecting scenario files."""
        plt.close(self.figure)
