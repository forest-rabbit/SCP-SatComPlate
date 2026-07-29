"""Deterministic JSON and SHA-256 helpers for topology generation."""

from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any, Iterable


REPOSITORY_ROOT = Path(__file__).resolve().parents[6]


class HashContractError(ValueError):
    """Raised when aggregate-hash inputs violate the file contract."""


def compact_json(payload: Any) -> str:
    """Serialize JSON with stable insertion order and compact separators."""
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def compact_json_bytes(payload: Any) -> bytes:
    """Return newline-terminated compact JSON bytes."""
    return (compact_json(payload) + "\n").encode("utf-8")


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def aggregate_data_sha256(
    root: Path,
    relative_paths: Iterable[Path | str],
) -> str:
    """Hash sorted ``path NUL file-sha newline`` records."""
    normalized = []
    for value in relative_paths:
        relative = Path(value)
        if relative.is_absolute() or ".." in relative.parts:
            raise HashContractError(
                f"aggregate path must remain relative to {root}: {relative}"
            )
        normalized.append(relative)
    if len(normalized) != len(set(normalized)):
        raise HashContractError("aggregate paths must be unique")
    records = bytearray()
    for relative in sorted(normalized, key=lambda path: path.as_posix()):
        path = root / relative
        if not path.is_file():
            raise HashContractError(f"aggregate input is not a file: {path}")
        records.extend(relative.as_posix().encode("utf-8"))
        records.extend(b"\0")
        records.extend(sha256_file(path).encode("ascii"))
        records.extend(b"\n")
    return sha256_bytes(bytes(records))


def uv_version() -> str:
    """Return the pinned uv semantic version without platform decoration."""
    result = subprocess.run(
        ["uv", "--version"],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    fields = result.stdout.split()
    if len(fields) < 2:
        raise RuntimeError(f"unexpected uv version output: {result.stdout!r}")
    return fields[1]
