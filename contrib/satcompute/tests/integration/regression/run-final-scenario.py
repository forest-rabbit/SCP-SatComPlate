#!/usr/bin/env python3
"""Run only the frozen final scene: none, generate, or generate plus G4 shadow."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[5]
SCENE = "contrib/satcompute/input/experiments/leo-66"


def arguments(output, fault_mode="generate", audit=False, shadow=False,
              validation_trace=None, protection_mode="off", placement_mode="ffp", lrl_weight=1,
              remote_busy_recovery_policy="relocate"):
    if protection_mode not in ("off", "fixed", "compfrr", "recompute", "one-plus-one") or placement_mode not in ("ffp", "lrl"):
        raise ValueError("unsupported protection/placement mode")
    if placement_mode == "lrl" and protection_mode not in ("fixed", "compfrr"):
        raise ValueError("LRL requires fixed or CompFRR protection")
    if remote_busy_recovery_policy not in ("recompute", "relocate"):
        raise ValueError("unsupported remote-busy recovery policy")
    if lrl_weight != 1:
        raise ValueError("G3 freezes LRL lambda=1; no weight sweep")
    if protection_mode == "compfrr" and fault_mode != "generate":
        raise ValueError("CompFRR formal evaluation requires online generate")
    if fault_mode not in ("none", "generate", "validation-replay") or (fault_mode != "generate" and (audit or shadow)):
        raise ValueError("audit/shadow require generate")
    if (fault_mode == "validation-replay") != (validation_trace is not None):
        raise ValueError("validation-replay requires an explicit frozen trace")
    if validation_trace is not None:
        validation_trace = Path(validation_trace).resolve()
        if not validation_trace.is_file():
            raise ValueError("BLOCKED: frozen evidence missing; never generate a replacement")
        if output.resolve() == validation_trace.parent:
            raise ValueError("refusing to overwrite frozen evidence")
    manifest = json.loads((ROOT / SCENE / "fault/f3-manifest.json").read_text())
    f3 = manifest["f3"]
    if (manifest["simulation_duration_s"], f3["node_id"], f3["time_ns"]) != (1300, 62, 1027055770726):
        raise ValueError("frozen F3 node/time/horizon differs")
    result = ["satcompute", "--simulationDuration=1300", "--orbitStartOffset=0",
              f"--computeProfile={SCENE}/compute/compute-profile.json", f"--taskTrace={SCENE}/workload/task-trace.json",
              f"--constellationConfig={SCENE}/topology/constellation.csv",
              "--computeDeadlineFactor=1.3", "--islBandwidthBps=10000000000", "--linkMetrics=1",
              "--delayMode=fixed", "--fixedDelay=0.001", "--networkUpdateInterval=20",
              "--routingMode=global-capacity-aware-hrw", "--transferChunkMode=size-aware",
              "--islMtuBytes=64028", "--islQueueBytes=1500000", "--receiverRcvBufBytes=131072",
              "--linkMetricsInterval=1", "--maxIslDistance=6171353",
              "--randomSeed=1", "--randomRun=11", "--ecmpHashSeed=1",
              f"--faultMode={fault_mode}", f"--outputDir={output}",
              f"--compfrr-shadow={int(shadow)}", f"--faultProbabilityAudit={int(audit)}"]
    if fault_mode == "generate":
        result += [f"--faultTrace={output}/fault-trace.json", "--taskCompletionPolicy=report",
                   "--faultEnableF1=1", "--faultEnableF2=1", "--faultEnableF3=1",
                   "--faultF3Mode=controlled", "--faultF3Node=62", "--faultF3Time=1027.055770726"]
    elif fault_mode == "validation-replay":
        result += [f"--validationFaultTrace={validation_trace}", f"--faultTrace={output}/fault-trace.json",
                   "--taskCompletionPolicy=report"]
    else:
        result += ["--taskCompletionPolicy=strict"]
    if protection_mode != "off":
        result += [f"--protectionMode={protection_mode}", "--backupStorageBytesPerNode=10000000000",
                   "--fixedProtectionDelta=0.05", "--fixedProtectionBatchN=4",
                   f"--placementMode={placement_mode}", f"--lrlRecoveryWeight={lrl_weight}",
                   f"--remoteBusyRecoveryPolicy={remote_busy_recovery_policy}"]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--fault-mode", choices=("none", "generate", "validation-replay"), default="generate")
    parser.add_argument("--validation-trace", type=Path)
    parser.add_argument("--protection-mode", choices=("off", "fixed", "compfrr", "recompute", "one-plus-one"), default="off")
    parser.add_argument("--placement-mode", choices=("ffp", "lrl"), default="ffp")
    parser.add_argument("--remote-busy-recovery-policy", choices=("relocate", "recompute"), default="relocate")
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--shadow", action="store_true", help="Read-only G4 validation, not real backup")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    try:
        command = [str(ROOT / "ns3"), "run", "--no-build",
                   shlex.join(arguments(output, args.fault_mode, args.audit, args.shadow,
                                        args.validation_trace, args.protection_mode, args.placement_mode,
                                        remote_busy_recovery_policy=args.remote_busy_recovery_policy))]
        if output.exists():
            raise ValueError("refusing to overwrite an existing output directory")
    except (ValueError, OSError, KeyError) as error:
        parser.error(str(error))
    output.mkdir(parents=True)
    identity = {"command": command, "seed": 1, "run": 11, "fault_mode": args.fault_mode,
                "validation_fault_trace": str(args.validation_trace.resolve()) if args.validation_trace else None,
                "protection_mode": args.protection_mode,
                "placement_mode": args.placement_mode, "lrl_recovery_weight": 1,
                "remote_busy_recovery_policy": args.remote_busy_recovery_policy,
                "task_trace": f"{SCENE}/workload/task-trace.json", "f3_manifest": f"{SCENE}/fault/f3-manifest.json",
                "audit": args.audit, "shadow": args.shadow, "simulation_duration_s": 1300,
                "fixed_delay_seconds": 0.001,
                "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "worktree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT))}
    (output / "execution.json").write_text(json.dumps(identity, indent=2)+"\n")
    started = time.monotonic()
    with (output / "run.log").open("w") as log:
        result = subprocess.run(["/usr/bin/time", "-v", "-o", str(output / "time.txt"), *command],
                                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    record = {"returncode": result.returncode, "elapsed_wall_s": time.monotonic()-started}
    (output / "execution-result.json").write_text(json.dumps(record, indent=2)+"\n")
    print(json.dumps({"output_dir": str(output), **record}), flush=True)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
