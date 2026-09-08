#!/usr/bin/env python3
"""Opt-in C800 run with reproducible inputs; never schedules CI or calibration."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[5]
INPUT = "contrib/satcompute/input/examples/leo-66-1000s-n4c"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--fault-mode", choices=("none", "generate"), default="none")
    parser.add_argument("--audit", action="store_true")
    args = parser.parse_args()
    if args.audit and args.fault_mode != "generate":
        parser.error("audit requires generate")
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("refusing to overwrite an existing output directory")
    output.mkdir(parents=True)
    arguments = ["satcompute", "--simulationDuration=1000", "--orbitStartOffset=0",
                 f"--computeProfile={INPUT}/compute-profile.json", f"--taskTrace={INPUT}/task-trace.json",
                 "--constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv",
                 "--computeDeadlineFactor=1.3", "--islBandwidthBps=10000000000", "--linkMetrics=1",
                 "--delayMode=fixed", "--fixedDelay=0.008", "--networkUpdateInterval=20",
                 "--routingMode=global-capacity-aware-hrw", "--transferChunkMode=size-aware",
                 "--islMtuBytes=64028", "--islQueueBytes=1500000", "--receiverRcvBufBytes=131072",
                 "--linkMetricsInterval=1", "--maxIslDistance=6171353",
                 "--randomSeed=1", "--randomRun=1", "--ecmpHashSeed=1",
                 f"--faultMode={args.fault_mode}", f"--outputDir={output}"]
    if args.fault_mode == "generate":
        arguments += [f"--faultTrace={output}/fault-trace.json", "--taskCompletionPolicy=report",
                      "--faultEnableF1=1", "--faultEnableF2=1", "--faultEnableF3=1",
                      f"--faultProbabilityAudit={int(args.audit)}"]
    command = [str(ROOT / "ns3"), "run", "--no-build", shlex.join(arguments)]
    identity = {"command": command, "seed": 1, "run": 1, "fault_mode": args.fault_mode,
                "audit": args.audit,
                "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "worktree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT))}
    (output / "execution.json").write_text(json.dumps(identity, indent=2) + "\n")
    started = time.monotonic()
    with (output / "run.log").open("w") as log:
        result = subprocess.run(["/usr/bin/time", "-v", "-o", str(output / "time.txt"), *command],
                                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    record = {"returncode": result.returncode, "elapsed_wall_s": time.monotonic() - started}
    (output / "execution-result.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"output_dir": str(output), **record}), flush=True)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
