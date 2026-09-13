#!/usr/bin/env python3
"""CB CLI smoke inputs: existing four-profile fixture, not a new formal workload."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
FIXTURE = ROOT / "contrib/satcompute/tests/fixtures/protection"


def arguments(output, placement, busy):
    return ["satcompute", "--simulationDuration=15", "--randomSeed=1", "--randomRun=11",
        "--faultMode=none", "--faultProbabilityAudit=0", "--compfrr-shadow=0", "--taskCompletionPolicy=strict",
        "--constellationConfig=contrib/satcompute/tests/fixtures/constellation/connected-16.csv",
        f"--taskTrace={FIXTURE/'fixed-four-profiles.json'}", f"--computeProfile={FIXTURE/'fixed-compute.json'}",
        "--protectionMode=checkbullet", "--backupStorageBytesPerNode=10000000000",
        f"--placementMode={placement}", f"--remoteBusyRecoveryPolicy={busy}", "--lrlRecoveryWeight=1",
        "--routingMode=global-capacity-aware-hrw", "--islBandwidthBps=10000000000",
        "--delayMode=fixed", "--fixedDelay=0.001", "--linkMetrics=1", f"--outputDir={output}"]


if __name__ == "__main__":
    import runpy
    import sys
    tools = ROOT / "contrib/satcompute/protection/policy/baseline/checkbullet/tools"
    sys.path.insert(0,str(tools))
    runpy.run_path(str(tools/"run-cb-sat-matrix.py"),run_name="__main__")
