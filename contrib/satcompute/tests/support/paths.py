"""Stable repository paths for SatCompute tests and integration drivers."""

from __future__ import annotations

from pathlib import Path


def find_repository_root(source: Path = Path(__file__)) -> Path:
    """Return the ns-3.48 repository root without a fixed parent depth."""
    for candidate in source.resolve().parents:
        if (
            (candidate / "ns3").is_file()
            and (candidate / "contrib" / "satcompute").is_dir()
        ):
            return candidate
    raise RuntimeError(f"cannot locate SatCompute repository from {source}")


REPOSITORY_ROOT = find_repository_root()
SATCOMPUTE_ROOT = REPOSITORY_ROOT / "contrib" / "satcompute"
TOOLS_ROOT = SATCOMPUTE_ROOT / "tools"

TOPOLOGY_GENERATION_ROOT = TOOLS_ROOT / "generation" / "topology"
SCENARIO_GENERATION_ROOT = TOOLS_ROOT / "generation" / "scenario"
INTERVAL_ANALYSIS_ROOT = TOOLS_ROOT / "analysis" / "topology_interval"
ORBIT_VISUALIZATION_ROOT = TOOLS_ROOT / "visualization" / "orbit"

TESTS_ROOT = SATCOMPUTE_ROOT / "tests"
FIXTURES_ROOT = TESTS_ROOT / "fixtures"
TOPOLOGY_SNAPSHOT_FIXTURES = FIXTURES_ROOT / "topology" / "snapshots"
COMPUTE_PROFILE_FIXTURES = FIXTURES_ROOT / "topology" / "compute-profiles"
TRANSFER_FIXTURES = FIXTURES_ROOT / "traffic" / "transfers"
TASK_FIXTURES = FIXTURES_ROOT / "traffic" / "tasks"
SCENARIO_GENERATION_FIXTURES = FIXTURES_ROOT / "scenario-generation"
