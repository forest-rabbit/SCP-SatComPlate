#!/usr/bin/env python3
"""Parse the closed-world orbit-visualization configuration."""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SCHEMA_VERSION = "0.1"
AUTO = "auto"
DETAILED = "detailed"
SIMPLIFIED = "simplified"
DISPLAY_MODES = frozenset((AUTO, DETAILED, SIMPLIFIED))
CONFIG_FIELDS = frozenset(
    (
        "schema_version",
        "enabled",
        "display_mode",
        "render_step_s",
        "playback_interval_ms",
        "show_earth",
        "show_orbits",
        "show_links",
        "show_node_labels",
        "detail_node_threshold",
        "export_gif",
        "gif_frame_step_s",
    )
)
OPTIONAL_FIELDS = frozenset(("gif_path",))


class OrbitVisualizationConfigError(ValueError):
    """Raised when an orbit-visualization config violates schema 0.1."""


@dataclass(frozen=True)
class OrbitVisualizationConfig:
    """Validated display-only settings kept outside scenario hashes."""

    schema_version: str
    enabled: bool
    display_mode: str
    render_step_s: float
    playback_interval_ms: int
    show_earth: bool
    show_orbits: bool
    show_links: bool
    show_node_labels: bool
    detail_node_threshold: int
    export_gif: bool
    gif_path: Path | None
    gif_frame_step_s: float

    def resolved_display_mode(self, node_count: int) -> str:
        """Resolve auto without changing the source configuration."""
        if (
            not isinstance(node_count, int)
            or isinstance(node_count, bool)
            or node_count <= 0
        ):
            raise OrbitVisualizationConfigError(
                "node_count must be a positive integer"
            )
        if self.display_mode != AUTO:
            return self.display_mode
        if node_count <= self.detail_node_threshold:
            return DETAILED
        return SIMPLIFIED


def _require_boolean(value: Any, name: str) -> bool:
    if not isinstance(value, bool):
        raise OrbitVisualizationConfigError(f"{name} must be a boolean")
    return value


def _require_positive_number(value: Any, name: str) -> float:
    if (
        not isinstance(value, (int, float))
        or isinstance(value, bool)
        or not math.isfinite(value)
        or value <= 0
    ):
        raise OrbitVisualizationConfigError(
            f"{name} must be a finite positive number"
        )
    return float(value)


def _require_positive_integer(value: Any, name: str) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value <= 0
    ):
        raise OrbitVisualizationConfigError(
            f"{name} must be a positive integer"
        )
    return value


def parse_config(
    payload: Any,
    *,
    base_dir: Path | None = None,
) -> OrbitVisualizationConfig:
    """Validate one decoded config and resolve a relative GIF path."""
    if not isinstance(payload, dict):
        raise OrbitVisualizationConfigError(
            "visualization config root must be an object"
        )
    actual = frozenset(payload)
    allowed = CONFIG_FIELDS | OPTIONAL_FIELDS
    missing = CONFIG_FIELDS - actual
    unknown = actual - allowed
    if missing or unknown:
        raise OrbitVisualizationConfigError(
            "visualization config fields differ: "
            f"missing={sorted(missing)}, unknown={sorted(unknown)}"
        )
    if payload["schema_version"] != SCHEMA_VERSION:
        raise OrbitVisualizationConfigError(
            f"schema_version must be {SCHEMA_VERSION}"
        )
    display_mode = payload["display_mode"]
    if not isinstance(display_mode, str) or display_mode not in DISPLAY_MODES:
        raise OrbitVisualizationConfigError(
            "display_mode must be auto, detailed, or simplified"
        )

    export_gif = _require_boolean(payload["export_gif"], "export_gif")
    gif_value = payload.get("gif_path")
    if export_gif:
        if (
            not isinstance(gif_value, str)
            or not gif_value.strip()
            or Path(gif_value).suffix.lower() != ".gif"
        ):
            raise OrbitVisualizationConfigError(
                "export_gif=true requires a non-empty .gif path"
            )
        gif_path = Path(gif_value)
        if not gif_path.is_absolute() and base_dir is not None:
            gif_path = Path(base_dir) / gif_path
        gif_path = gif_path.resolve()
    else:
        if gif_value is not None:
            raise OrbitVisualizationConfigError(
                "export_gif=false requires gif_path to be null or omitted"
            )
        gif_path = None

    return OrbitVisualizationConfig(
        schema_version=SCHEMA_VERSION,
        enabled=_require_boolean(payload["enabled"], "enabled"),
        display_mode=display_mode,
        render_step_s=_require_positive_number(
            payload["render_step_s"],
            "render_step_s",
        ),
        playback_interval_ms=_require_positive_integer(
            payload["playback_interval_ms"],
            "playback_interval_ms",
        ),
        show_earth=_require_boolean(payload["show_earth"], "show_earth"),
        show_orbits=_require_boolean(
            payload["show_orbits"],
            "show_orbits",
        ),
        show_links=_require_boolean(payload["show_links"], "show_links"),
        show_node_labels=_require_boolean(
            payload["show_node_labels"],
            "show_node_labels",
        ),
        detail_node_threshold=_require_positive_integer(
            payload["detail_node_threshold"],
            "detail_node_threshold",
        ),
        export_gif=export_gif,
        gif_path=gif_path,
        gif_frame_step_s=_require_positive_number(
            payload["gif_frame_step_s"],
            "gif_frame_step_s",
        ),
    )


def load_config(path: Path) -> OrbitVisualizationConfig:
    """Read and validate one orbit-visualization JSON file."""
    source = Path(path).resolve()
    try:
        payload = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise OrbitVisualizationConfigError(
            f"cannot read visualization config {source}: {error}"
        ) from error
    return parse_config(payload, base_dir=source.parent)
