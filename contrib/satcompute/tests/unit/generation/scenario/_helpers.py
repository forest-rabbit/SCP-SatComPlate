"""Shared constructors for unified-scenario tests."""

from __future__ import annotations

import json
from pathlib import Path

from contrib.satcompute.tests.support.paths import SCENARIO_GENERATION_ROOT


PRESET = (
    SCENARIO_GENERATION_ROOT
    / "config"
    / "synthetic-66-compute-22.json"
)


def preset_payload() -> dict:
    return json.loads(PRESET.read_text(encoding="utf-8"))


def write_config(
    path: Path,
    *,
    mode: str,
    delay_mode: str,
) -> Path:
    payload = preset_payload()
    if mode == "static":
        payload["topology"]["schedule"] = {"snapshot_time_s": 17}
    else:
        payload["topology"]["schedule"] = {
            "start_time_s": 0,
            "orbit_sample_offset_s": 0,
            "duration_s": 120,
            "step_s": 60,
        }
    payload["topology"]["mode"] = mode
    payload["topology"]["delay_mode"] = delay_mode
    payload["topology"]["fixed_delay_us"] = (
        8000 if delay_mode == "fixed" else None
    )
    path.write_text(
        json.dumps(payload, indent=2) + "\n",
        encoding="utf-8",
    )
    return path
