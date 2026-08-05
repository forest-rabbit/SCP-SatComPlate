#!/usr/bin/env python3
"""CLI for optional interactive, headless, and GIF topology-trace viewing."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from .configuration import load_config


class OrbitViewerError(RuntimeError):
    """Raised when a requested display output violates viewer boundaries."""


def _require_output_outside_trace(trace_dir: Path, output_path: Path) -> None:
    trace = trace_dir.resolve()
    output = output_path.resolve()
    if output == trace or trace in output.parents:
        raise OrbitViewerError(
            "visualization output must remain outside the topology trace"
        )


def run_viewer(
    trace_dir: Path,
    config_path: Path,
    *,
    compute_profile: Path | None = None,
    headless: bool = False,
) -> dict[str, Any]:
    """Run only the explicitly enabled display path."""
    config = load_config(config_path)
    if not config.enabled:
        return {"enabled": False, "side_effects": 0}

    if headless:
        import matplotlib

        matplotlib.use("Agg")

    from .renderer import OrbitRenderer
    from .scenario_reader import load_trace

    trace = load_trace(trace_dir, compute_profile=compute_profile)
    renderer = OrbitRenderer(trace, config)
    summary: dict[str, Any]
    try:
        gif_output = None
        if config.export_gif:
            if config.gif_path is None:
                raise OrbitViewerError("enabled GIF export has no output path")
            _require_output_outside_trace(trace.root, config.gif_path)
            from .gif_export import export_gif

            gif_output = export_gif(
                renderer,
                trace.frame_times(config.gif_frame_step_s),
                config.gif_path,
                playback_interval_ms=config.playback_interval_ms,
            )

        if headless:
            frame_times = trace.frame_times(config.render_step_s)
            for time_s in frame_times:
                renderer.update(time_s)
                renderer.figure.canvas.draw()
            summary = renderer.summary()
            summary.update(
                {
                    "enabled": True,
                    "headless": True,
                    "rendered_frame_count": len(frame_times),
                    "first_frame_time_s": frame_times[0],
                    "last_frame_time_s": frame_times[-1],
                    "gif_path": None if gif_output is None else str(gif_output),
                }
            )
        else:
            from .animation import OrbitPlayback

            playback = OrbitPlayback(renderer, config)
            playback.show()
            summary = renderer.summary()
            summary.update(
                {
                    "enabled": True,
                    "headless": False,
                    "gif_path": None if gif_output is None else str(gif_output),
                }
            )
    finally:
        renderer.close()
    return summary


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace-dir", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--compute-profile", type=Path)
    parser.add_argument(
        "--headless",
        action="store_true",
        help="render the configured timeline with Agg and do not show a GUI",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        summary = run_viewer(
            arguments.trace_dir,
            arguments.config,
            compute_profile=arguments.compute_profile,
            headless=arguments.headless,
        )
    except (OSError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
