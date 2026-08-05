"""Small file helpers shared by SatCompute test domains."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def directory_bytes(root: Path) -> dict[str, bytes]:
    """Return deterministic relative paths and bytes for all files below root."""
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in root.rglob("*")
        if path.is_file()
    }


def write_json(path: Path, payload: Any) -> None:
    """Write a simple newline-terminated JSON fixture."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload) + "\n", encoding="utf-8")
