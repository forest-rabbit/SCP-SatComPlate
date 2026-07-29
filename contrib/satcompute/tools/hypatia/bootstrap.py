#!/usr/bin/env python3
"""Clone and pin the external Hypatia checkout used by SatCompute tools."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Sequence


TOOL_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = TOOL_DIR.parents[3]
DEFAULT_UPSTREAM = TOOL_DIR / "upstream.json"
DEFAULT_CHECKOUT = REPOSITORY_ROOT / ".external" / "hypatia"


class BootstrapError(RuntimeError):
    """Raised when the external checkout violates the frozen contract."""


def run_git(arguments: Sequence[str], *, cwd: Path | None = None) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=cwd,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise BootstrapError(
            f"git {' '.join(arguments)} failed with exit code "
            f"{result.returncode}: {detail}"
        )
    return result.stdout.strip()


def load_upstream(path: Path) -> tuple[str, str, str]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BootstrapError(f"cannot read upstream contract {path}: {error}") from error

    repository = payload.get("repository")
    commit = payload.get("commit")
    component = payload.get("component")
    if not isinstance(repository, str) or not repository:
        raise BootstrapError("upstream repository must be a non-empty string")
    if (
        not isinstance(commit, str)
        or len(commit) != 40
        or any(character not in "0123456789abcdef" for character in commit)
    ):
        raise BootstrapError(
            "upstream commit must be a full lowercase 40-character SHA"
        )
    if component != "satgenpy":
        raise BootstrapError("only the satgenpy component is supported")
    return repository, commit, component


def normalize_remote(value: str) -> str:
    normalized = value.rstrip("/")
    return normalized[:-4] if normalized.endswith(".git") else normalized


def require_clean_checkout(checkout: Path) -> None:
    status = run_git(["status", "--porcelain"], cwd=checkout)
    if status:
        raise BootstrapError(
            f"refusing to modify dirty Hypatia checkout at {checkout}:\n{status}"
        )


def bootstrap(upstream_path: Path, checkout: Path) -> dict[str, str]:
    repository, commit, component = load_upstream(upstream_path)
    checkout.parent.mkdir(parents=True, exist_ok=True)

    if checkout.exists():
        if not (checkout / ".git").is_dir():
            raise BootstrapError(
                f"checkout path exists but is not a Git repository: {checkout}"
            )
        require_clean_checkout(checkout)
    else:
        run_git(
            [
                "clone",
                "--filter=blob:none",
                "--no-tags",
                repository,
                str(checkout),
            ]
        )

    remote = run_git(["remote", "get-url", "origin"], cwd=checkout)
    if normalize_remote(remote) != normalize_remote(repository):
        raise BootstrapError(
            f"origin mismatch for {checkout}: expected {repository}, found {remote}"
        )

    run_git(["fetch", "--force", "--no-tags", "origin", commit], cwd=checkout)
    run_git(["checkout", "--detach", "--force", commit], cwd=checkout)
    require_clean_checkout(checkout)

    head = run_git(["rev-parse", "HEAD"], cwd=checkout)
    if head != commit:
        raise BootstrapError(
            f"checkout HEAD mismatch: expected {commit}, found {head}"
        )
    if not (checkout / component).is_dir():
        raise BootstrapError(
            f"frozen component is missing from checkout: {checkout / component}"
        )

    result = {
        "repository": repository,
        "commit": commit,
        "component": component,
        "checkout": str(checkout),
        "head": head,
    }
    print(json.dumps(result, sort_keys=True))
    return result


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, default=DEFAULT_UPSTREAM)
    parser.add_argument("--checkout", type=Path, default=DEFAULT_CHECKOUT)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        bootstrap(arguments.upstream.resolve(), arguments.checkout.resolve())
    except BootstrapError as error:
        print(f"ERROR: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
