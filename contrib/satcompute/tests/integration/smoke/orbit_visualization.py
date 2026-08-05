#!/usr/bin/env python3
"""Generate and render a three-frame ns-3.48 topology-trace smoke."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image

from contrib.satcompute.tests.support.paths import REPOSITORY_ROOT
from contrib.satcompute.tools.visualization.orbit.viewer import run_viewer


class OrbitVisualizationSmokeError(RuntimeError):
    """Raised when trace generation or headless rendering differs."""


def _write_json(path: Path, payload: dict) -> None:
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def _generate_trace(output_dir: Path) -> None:
    constellation = (
        REPOSITORY_ROOT
        / "contrib/satcompute/input/topology/constellations/synthetic-66.csv"
    )
    arguments = " ".join(
        (
            "satcompute-topology-generator",
            "--runName=orbit-visualization-smoke",
            "--simulationDuration=2",
            f"--constellationConfig={constellation}",
            "--maxIslDistance=30000000",
            "--delayMode=distance",
            "--fixedDelay=0",
            "--networkUpdateInterval=2",
            "--islBandwidthBps=100000000",
            "--topologyExportInterval=1",
            f"--outputDir={output_dir}",
        )
    )
    completed = subprocess.run(
        (str(REPOSITORY_ROOT / "ns3"), "run", "--no-build", arguments),
        cwd=REPOSITORY_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    if '"status":"generated"' not in completed.stdout:
        raise OrbitVisualizationSmokeError(
            f"topology generator did not report success: {completed.stdout}"
        )


def run_smoke(work_dir: Path, *, export_gif: bool) -> dict:
    root = Path(work_dir).resolve()
    if root.exists() and any(root.iterdir()):
        raise OrbitVisualizationSmokeError(
            f"smoke work directory must be empty: {root}"
        )
    root.mkdir(parents=True, exist_ok=True)

    trace_dir = root / "trace"
    _generate_trace(trace_dir)
    gif_path = root / "orbit-smoke.gif"
    config = {
        "schema_version": "0.1",
        "enabled": True,
        "display_mode": "auto",
        "render_step_s": 1,
        "playback_interval_ms": 100,
        "show_earth": True,
        "show_orbits": True,
        "show_links": True,
        "show_node_labels": False,
        "detail_node_threshold": 100,
        "export_gif": export_gif,
        "gif_path": str(gif_path) if export_gif else None,
        "gif_frame_step_s": 1,
    }
    config_path = root / "orbit-viewer.json"
    _write_json(config_path, config)
    compute_profile = (
        REPOSITORY_ROOT
        / "contrib/satcompute/input/topology/resources/workload/"
        "xw-66sat-static-2g-compute-profile.json"
    )
    summary = run_viewer(
        trace_dir,
        config_path,
        compute_profile=compute_profile,
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
        "active_link_count": 121,
        "show_links": True,
        "rendered_frame_count": 3,
        "first_frame_time_s": 0.0,
        "last_frame_time_s": 2.0,
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
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
