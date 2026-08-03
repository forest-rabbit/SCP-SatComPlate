#!/usr/bin/env python3
"""Generate and render a three-frame 66-satellite visualization smoke."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image

from contrib.satcompute.tests.support.paths import SCENARIO_GENERATION_ROOT
from contrib.satcompute.tools.generation.scenario.generate_scenario import (
    generate_scenario,
)
from contrib.satcompute.tools.visualization.orbit.viewer import run_viewer


SCENARIO_PRESET = (
    SCENARIO_GENERATION_ROOT
    / "config"
    / "synthetic-66-compute-22.json"
)


class OrbitVisualizationSmokeError(RuntimeError):
    """Raised when the short headless visualization contract fails."""


def _write_json(path: Path, payload: dict) -> None:
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def run_smoke(work_dir: Path, *, export_gif: bool) -> dict:
    """Run a real 66-satellite t=0/10/20 Agg render in one empty directory."""
    root = Path(work_dir).resolve()
    if root.exists() and any(root.iterdir()):
        raise OrbitVisualizationSmokeError(
            f"smoke work directory must be empty: {root}"
        )
    root.mkdir(parents=True, exist_ok=True)

    scenario_config = json.loads(
        SCENARIO_PRESET.read_text(encoding="utf-8")
    )
    scenario_config["topology"]["schedule"] = {
        "start_time_s": 0,
        "orbit_sample_offset_s": 0,
        "duration_s": 20,
        "step_s": 10,
    }
    scenario_config_path = root / "scenario-config.json"
    _write_json(scenario_config_path, scenario_config)
    scenario_dir = root / "scenario"
    generate_scenario(scenario_config_path, scenario_dir)

    gif_path = root / "orbit-smoke.gif"
    visualization_config = {
        "schema_version": "0.1",
        "enabled": True,
        "display_mode": "auto",
        "render_step_s": 10,
        "playback_interval_ms": 100,
        "show_earth": True,
        "show_orbits": True,
        "show_links": True,
        "show_node_labels": False,
        "detail_node_threshold": 100,
        "export_gif": export_gif,
        "gif_path": str(gif_path) if export_gif else None,
        "gif_frame_step_s": 10,
    }
    visualization_config_path = root / "orbit-viewer.json"
    _write_json(visualization_config_path, visualization_config)
    summary = run_viewer(
        scenario_dir,
        visualization_config_path,
        headless=True,
    )

    expected = {
        "display_mode": "detailed",
        "node_count": 66,
        "compute_node_count": 22,
        "relay_node_count": 44,
        "earth_graticule_count": 17,
        "earth_coordinate_label_count": 13,
        "orbit_artist_count": 6,
        "show_links": True,
        "active_link_count": 121,
        "rendered_frame_count": 3,
        "first_frame_time_s": 0.0,
        "last_frame_time_s": 20.0,
    }
    mismatches = {
        key: (summary.get(key), value)
        for key, value in expected.items()
        if summary.get(key) != value
    }
    if mismatches:
        raise OrbitVisualizationSmokeError(
            f"headless render summary differs: {mismatches}"
        )
    if export_gif:
        if summary["gif_path"] != str(gif_path):
            raise OrbitVisualizationSmokeError("GIF path was not reported")
        with Image.open(gif_path) as image:
            if image.format != "GIF" or image.n_frames != 3:
                raise OrbitVisualizationSmokeError(
                    "Pillow did not read exactly three GIF frames"
                )
    elif gif_path.exists():
        raise OrbitVisualizationSmokeError(
            "disabled GIF export created an output file"
        )
    return summary


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--export-gif", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        summary = run_smoke(
            arguments.work_dir,
            export_gif=arguments.export_gif,
        )
    except (OSError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
