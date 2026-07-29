"""Shared atomic output-directory publication for topology generators."""

from __future__ import annotations

import shutil
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator


class AtomicOutputError(RuntimeError):
    """Raised when an output directory cannot be published safely."""


def temporary_output_dir(output_dir: Path) -> Path:
    """Return the deterministic sibling temporary directory."""
    return output_dir.with_name(f"{output_dir.name}.tmp")


def require_safe_output_target(output_dir: Path) -> None:
    """Reject symlinks, files, and non-empty formal output directories."""
    if output_dir.is_symlink() or (
        output_dir.exists() and not output_dir.is_dir()
    ):
        raise AtomicOutputError(
            f"output path must be a directory: {output_dir}"
        )
    if output_dir.exists() and any(output_dir.iterdir()):
        raise AtomicOutputError(
            f"refusing to overwrite non-empty output directory: {output_dir}"
        )
    output_dir.parent.mkdir(parents=True, exist_ok=True)


def remove_temporary_path(path: Path) -> None:
    """Remove only the exact deterministic temporary path."""
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


@contextmanager
def atomic_output_directory(output_dir: Path) -> Iterator[Path]:
    """Yield a clean temporary directory and publish it on successful exit."""
    output = Path(output_dir)
    temporary = temporary_output_dir(output)
    require_safe_output_target(output)
    remove_temporary_path(temporary)
    try:
        temporary.mkdir(parents=True)
        yield temporary
        if not temporary.is_dir():
            raise AtomicOutputError(
                f"temporary output directory disappeared: {temporary}"
            )
        temporary.replace(output)
    except BaseException:
        remove_temporary_path(temporary)
        raise
