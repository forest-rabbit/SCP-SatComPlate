#!/usr/bin/env python3
"""Optional Pillow-only GIF export for orbit visualization frames."""

from __future__ import annotations

from pathlib import Path

from matplotlib.animation import FuncAnimation, PillowWriter

from .renderer import OrbitRenderer


class OrbitGifError(ValueError):
    """Raised when a GIF destination or frame contract is invalid."""


def export_gif(
    renderer: OrbitRenderer,
    frame_times: tuple[float, ...],
    output_path: Path,
    *,
    playback_interval_ms: int,
) -> Path:
    """Stream selected frame times to PillowWriter without FFmpeg."""
    output = Path(output_path).resolve()
    if output.suffix.lower() != ".gif":
        raise OrbitGifError("GIF output path must end in .gif")
    if not frame_times:
        raise OrbitGifError("GIF export requires at least one frame")
    if playback_interval_ms <= 0:
        raise OrbitGifError("playback_interval_ms must be positive")
    output.parent.mkdir(parents=True, exist_ok=True)
    animation = FuncAnimation(
        renderer.figure,
        renderer.update,
        frames=frame_times,
        interval=playback_interval_ms,
        repeat=False,
        blit=False,
        cache_frame_data=False,
    )
    frames_per_second = max(1.0, 1000.0 / playback_interval_ms)
    animation.save(
        output,
        writer=PillowWriter(fps=frames_per_second),
    )
    return output
