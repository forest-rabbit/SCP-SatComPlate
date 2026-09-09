#!/usr/bin/env python3
"""Opt-in C800 run with reproducible inputs; never schedules CI or calibration."""

import argparse
import json
import math
from pathlib import Path
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[5]
INPUT = "contrib/satcompute/input/examples/leo-66-1000s-n4c"
SCENE = "contrib/satcompute/input/examples/leo-66-1300s-n4c-g3-truncnormal-v3"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--fault-mode", choices=("none", "generate"), default="generate")
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--shadow", action="store_true", help="G4 read-only analytical decisions; no real backup")
    parser.add_argument("--task-trace", default=f"{SCENE}/task-trace.json")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--run", type=int, default=11)
    parser.add_argument("--simulation-seconds", type=int, default=1300)
    parser.add_argument("--fixed-delay-seconds", type=float, default=0.001,
                        help="One-way per-link fixed delay; default 0.001 s; use 0.008 for historical runs")
    parser.add_argument("--f1-beta", type=float)
    parser.add_argument("--f1-gamma", type=float)
    parser.add_argument("--disable-f3", action="store_true")
    parser.add_argument("--hotspot-manifest", type=Path,
                        help="Offline controlled F3 target/time; never supplied to an online algorithm")
    args = parser.parse_args()
    if args.hotspot_manifest is None and args.task_trace == f"{SCENE}/task-trace.json":
        args.hotspot_manifest = ROOT / SCENE / "f3-manifest.json"
    if args.simulation_seconds <= 0:
        parser.error("simulation-seconds must be positive")
    if not math.isfinite(args.fixed_delay_seconds) or args.fixed_delay_seconds <= 0:
        parser.error("fixed-delay-seconds must be finite and positive")
    if args.hotspot_manifest is not None:
        manifest = json.loads(args.hotspot_manifest.read_text())
        if manifest.get("simulation_duration_s", 1000) != args.simulation_seconds:
            parser.error("simulation horizon differs from hotspot manifest")
    if args.audit and args.fault_mode != "generate":
        parser.error("audit requires generate")
    if args.shadow and args.fault_mode != "generate":
        parser.error("shadow requires generate")
    if args.fault_mode == "generate" and args.hotspot_manifest is None and not args.disable_f3:
        parser.error("custom workloads require a controlled F3 manifest or --disable-f3")
    f3 = None
    if args.fault_mode == "generate" and args.hotspot_manifest is not None and not args.disable_f3:
        f3 = json.loads(args.hotspot_manifest.read_text()).get("f3")
        if f3 is None:
            parser.error("pure hotspot placement has no controlled F3 plan; use --disable-f3 or a final manifest")
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("refusing to overwrite an existing output directory")
    output.mkdir(parents=True)
    arguments = ["satcompute", f"--simulationDuration={args.simulation_seconds}", "--orbitStartOffset=0",
                 f"--computeProfile={INPUT}/compute-profile.json", f"--taskTrace={args.task_trace}",
                 "--constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv",
                 "--computeDeadlineFactor=1.3", "--islBandwidthBps=10000000000", "--linkMetrics=1",
                 "--delayMode=fixed", f"--fixedDelay={args.fixed_delay_seconds}", "--networkUpdateInterval=20",
                 "--routingMode=global-capacity-aware-hrw", "--transferChunkMode=size-aware",
                 "--islMtuBytes=64028", "--islQueueBytes=1500000", "--receiverRcvBufBytes=131072",
                 "--linkMetricsInterval=1", "--maxIslDistance=6171353",
                 f"--randomSeed={args.seed}", f"--randomRun={args.run}", "--ecmpHashSeed=1",
                 f"--faultMode={args.fault_mode}", f"--outputDir={output}",
                 f"--compfrr-shadow={int(args.shadow)}"]
    if args.fault_mode == "generate":
        arguments += [f"--faultTrace={output}/fault-trace.json", "--taskCompletionPolicy=report",
                      "--faultEnableF1=1", "--faultEnableF2=1", f"--faultEnableF3={int(not args.disable_f3)}",
                      f"--faultProbabilityAudit={int(args.audit)}"]
        if args.f1_beta is not None:
            arguments += [f"--faultF1Beta={args.f1_beta}"]
        if args.f1_gamma is not None:
            arguments += [f"--faultF1Gamma={args.f1_gamma}"]
        if f3 is not None:
            seconds, ns = divmod(f3["time_ns"], 10**9)
            arguments += ["--faultF3Mode=controlled", f"--faultF3Node={f3['node_id']}",
                          f"--faultF3Time={seconds}.{ns:09d}"]
    else:
        arguments += ["--taskCompletionPolicy=strict"]
    command = [str(ROOT / "ns3"), "run", "--no-build", shlex.join(arguments)]
    identity = {"command": command, "seed": args.seed, "run": args.run, "fault_mode": args.fault_mode,
                "task_trace": args.task_trace, "f1_beta_override": args.f1_beta,
                "f1_gamma_override": args.f1_gamma,
                "f3_disabled": args.disable_f3, "hotspot_manifest": str(args.hotspot_manifest),
                "audit": args.audit, "shadow": args.shadow, "simulation_duration_s": args.simulation_seconds,
                "fixed_delay_seconds": args.fixed_delay_seconds,
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
