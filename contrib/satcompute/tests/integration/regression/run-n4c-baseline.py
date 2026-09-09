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
    parser.add_argument("--task-trace", default=f"{INPUT}/task-trace.json")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--run", type=int, default=1)
    parser.add_argument("--f1-beta", type=float)
    parser.add_argument("--disable-f3", action="store_true")
    parser.add_argument("--hotspot-manifest", type=Path,
                        help="Offline controlled F3 target/time; never supplied to an online algorithm")
    args = parser.parse_args()
    if args.audit and args.fault_mode != "generate":
        parser.error("audit requires generate")
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("refusing to overwrite an existing output directory")
    output.mkdir(parents=True)
    arguments = ["satcompute", "--simulationDuration=1000", "--orbitStartOffset=0",
                 f"--computeProfile={INPUT}/compute-profile.json", f"--taskTrace={args.task_trace}",
                 "--constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv",
                 "--computeDeadlineFactor=1.3", "--islBandwidthBps=10000000000", "--linkMetrics=1",
                 "--delayMode=fixed", "--fixedDelay=0.008", "--networkUpdateInterval=20",
                 "--routingMode=global-capacity-aware-hrw", "--transferChunkMode=size-aware",
                 "--islMtuBytes=64028", "--islQueueBytes=1500000", "--receiverRcvBufBytes=131072",
                 "--linkMetricsInterval=1", "--maxIslDistance=6171353",
                 f"--randomSeed={args.seed}", f"--randomRun={args.run}", "--ecmpHashSeed=1",
                 f"--faultMode={args.fault_mode}", f"--outputDir={output}"]
    if args.fault_mode == "generate":
        arguments += [f"--faultTrace={output}/fault-trace.json", "--taskCompletionPolicy=report",
                      "--faultEnableF1=1", "--faultEnableF2=1", f"--faultEnableF3={int(not args.disable_f3)}",
                      f"--faultProbabilityAudit={int(args.audit)}"]
        if args.f1_beta is not None:
            arguments += [f"--faultF1Beta={args.f1_beta}"]
        if args.hotspot_manifest is not None and not args.disable_f3:
            f3 = json.loads(args.hotspot_manifest.read_text())["f3"]
            seconds, ns = divmod(f3["time_ns"], 10**9)
            arguments += ["--faultF3Mode=controlled", f"--faultF3Node={f3['node_id']}",
                          f"--faultF3Time={seconds}.{ns:09d}"]
    command = [str(ROOT / "ns3"), "run", "--no-build", shlex.join(arguments)]
    identity = {"command": command, "seed": args.seed, "run": args.run, "fault_mode": args.fault_mode,
                "task_trace": args.task_trace, "f1_beta_override": args.f1_beta,
                "f3_disabled": args.disable_f3, "hotspot_manifest": str(args.hotspot_manifest),
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
