#!/usr/bin/env python3
"""Run the v0.3 topology-trace interval-analysis integration contract."""

from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path

from contrib.satcompute.tests.support.paths import REPOSITORY_ROOT
from contrib.satcompute.tools.analysis.topology_interval.compare_intervals import (
    compare_traces,
)
from contrib.satcompute.tools.analysis.topology_interval.downsample_scenario import (
    downsample_trace,
)
from contrib.satcompute.tools.generation.topology.common.manifest import (
    validate_trace,
)


def _generate_reference(output_dir: Path) -> None:
    constellation = (
        REPOSITORY_ROOT
        / "contrib/satcompute/tests/fixtures/constellation/diamond-4.csv"
    )
    arguments = " ".join(
        (
            "satcompute-topology-generator",
            "--runName=interval-analysis-smoke",
            "--simulationDuration=4",
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
        raise RuntimeError(
            f"topology generator did not report success: {completed.stdout}"
        )


def main() -> int:
    with tempfile.TemporaryDirectory(
        prefix="satcompute-interval-smoke-"
    ) as temporary:
        root = Path(temporary)
        reference = root / "reference"
        held = root / "held-2s"
        _generate_reference(reference)
        reference_validation = validate_trace(reference)
        downsample = downsample_trace(reference, 2, held)
        held_validation = validate_trace(held)
        comparison = compare_traces(reference, held)

        if reference_validation["slice_count"] != 5:
            raise RuntimeError("one-second reference slice count differs")
        if downsample["slice_count"] != 3:
            raise RuntimeError("two-second held slice count differs")
        if held_validation["slice_count"] != 3:
            raise RuntimeError("held trace validation count differs")
        if comparison["interval_s"] != 2:
            raise RuntimeError("interval comparison did not report two seconds")
        if comparison["node_count"] != 4 or comparison["duration_s"] != 4:
            raise RuntimeError("interval comparison trace identity differs")

        print(
            json.dumps(
                {
                    "schema_version": "0.3",
                    "reference_slice_count": 5,
                    "held_slice_count": 3,
                    "comparison": comparison,
                    "conclusion": "success",
                },
                sort_keys=True,
                separators=(",", ":"),
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
