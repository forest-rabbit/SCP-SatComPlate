#!/usr/bin/env python3
"""Interactive full-timeline playback controls for OrbitRenderer."""

from __future__ import annotations

from typing import Any

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.widgets import Button, Slider

from .configuration import OrbitVisualizationConfig
from .renderer import OrbitRenderer


class OrbitPlayback:
    """Play, pause, seek, rotate, and zoom one complete scenario timeline."""

    def __init__(
        self,
        renderer: OrbitRenderer,
        config: OrbitVisualizationConfig,
    ) -> None:
        self.renderer = renderer
        self.config = config
        self.frame_times = renderer.scenario.frame_times(config.render_step_s)
        self.animation: FuncAnimation | None = None
        self.slider: Slider | None = None
        self.play_button: Button | None = None
        self.playing = False
        self._updating_slider = False
        renderer.figure.canvas.mpl_connect("close_event", self._on_close)
        if len(self.frame_times) == 1:
            renderer.update(self.frame_times[0])
            return

        slider_axes = renderer.figure.add_axes((0.20, 0.06, 0.65, 0.03))
        button_axes = renderer.figure.add_axes((0.05, 0.045, 0.10, 0.06))
        self.slider = Slider(
            slider_axes,
            "time (s)",
            self.frame_times[0],
            self.frame_times[-1],
            valinit=self.frame_times[0],
            valstep=self.frame_times,
        )
        self.slider.on_changed(self._seek)
        self.play_button = Button(button_axes, "Play")
        self.play_button.on_clicked(self._toggle_play)
        self.animation = FuncAnimation(
            renderer.figure,
            self._draw_frame,
            frames=range(len(self.frame_times)),
            interval=config.playback_interval_ms,
            repeat=False,
            blit=False,
            cache_frame_data=False,
        )
        self.animation.event_source.stop()

    def _draw_frame(self, frame_index: int) -> tuple[Any, ...]:
        time_s = self.frame_times[frame_index]
        artists = self.renderer.update(time_s)
        if self.slider is not None:
            self._updating_slider = True
            self.slider.set_val(time_s)
            self._updating_slider = False
        if frame_index == len(self.frame_times) - 1:
            self.playing = False
            if self.play_button is not None:
                self.play_button.label.set_text("Play")
        return artists

    def _seek(self, value: float) -> None:
        if self._updating_slider:
            return
        self.renderer.update(float(value))
        self.renderer.figure.canvas.draw_idle()

    def _toggle_play(self, _event: Any) -> None:
        if self.animation is None or self.play_button is None:
            return
        if self.playing:
            self.animation.event_source.stop()
            self.play_button.label.set_text("Play")
        else:
            if self.renderer.current_time_s >= self.frame_times[-1]:
                self.renderer.update(self.frame_times[0])
            self.animation.event_source.start()
            self.play_button.label.set_text("Pause")
        self.playing = not self.playing

    def _on_close(self, _event: Any) -> None:
        if self.animation is not None:
            self.animation.event_source.stop()

    def show(self) -> None:
        """Open the native Matplotlib window and block until it closes."""
        plt.show()
