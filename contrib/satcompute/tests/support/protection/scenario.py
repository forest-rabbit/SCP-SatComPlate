#!/usr/bin/env python3
"""Run only the frozen final scene: none, generate, or generate plus G4 shadow."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time
import runpy

ROOT = Path(__file__).resolve().parents[5]
SCENE = "contrib/satcompute/input/experiments/leo-66"
CONTROLLED_F3_TASK_ID = 120
REFERENCE_ISL_BANDWIDTH_BPS = 10_000_000_000


def _serialization_ns(byte_count, bandwidth_bps):
    if type(byte_count) is not int or byte_count < 0:
        raise ValueError("byte count must be a nonnegative integer")
    if type(bandwidth_bps) is not int or bandwidth_bps <= 0:
        raise ValueError("ISL bandwidth must be a positive integer")
    bits_ns = byte_count * 8 * 1_000_000_000
    return (bits_ns + bandwidth_bps - 1) // bandwidth_bps


def bandwidth_normalized_task_trace(destination, bandwidth_bps):
    """Materialize a derived trace with task 120 aligned to its 10 Gbps phase.

    This explicit experiment-scene transformation never mutates the frozen input.
    Propagation cancels between bandwidths; nominal serialization uses integer ceil.
    Every non-controlled task and every non-arrival field remain unchanged.
    """
    source = ROOT / SCENE / "workload/task-trace.json"
    data = json.loads(source.read_text())
    matches = [task for task in data["tasks"] if task["task_id"] == CONTROLLED_F3_TASK_ID]
    if len(matches) != 1:
        raise ValueError("controlled F3 task identity changed")
    task = matches[0]
    reference_arrival_ns = task["arrival_time_ns"]
    reference_serialization_ns = _serialization_ns(task["input_bytes"], REFERENCE_ISL_BANDWIDTH_BPS)
    target_serialization_ns = _serialization_ns(task["input_bytes"], bandwidth_bps)
    task["arrival_time_ns"] = reference_arrival_ns - (
        target_serialization_ns - reference_serialization_ns
    )
    if task["arrival_time_ns"] < 0:
        raise ValueError("bandwidth normalization places task before simulation start")
    destination = Path(destination)
    if destination.exists():
        raise ValueError("refusing to overwrite bandwidth-normalized task trace")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(data, indent=2) + "\n")
    return {
        "task_id": CONTROLLED_F3_TASK_ID,
        "reference_bandwidth_bps": REFERENCE_ISL_BANDWIDTH_BPS,
        "target_bandwidth_bps": bandwidth_bps,
        "reference_arrival_time_ns": reference_arrival_ns,
        "normalized_arrival_time_ns": task["arrival_time_ns"],
        "reference_serialization_ns": reference_serialization_ns,
        "target_serialization_ns": target_serialization_ns,
        "task_trace": str(destination),
    }



# These names are read-only compatibility exports for existing evidence readers.
# The current command path below does not call the historical argument generator.
_HISTORICAL = runpy.run_path(str(Path(__file__).with_name('historical_scenario.py')))
canonical_input_arguments = _HISTORICAL['canonical_input_arguments']
canonical_experiment_arguments = _HISTORICAL['canonical_experiment_arguments']
historical_comparison_arguments = _HISTORICAL['historical_comparison_arguments']
arguments = _HISTORICAL['arguments']


def platform_arguments(output, fault_mode="generate", audit=False, shadow=False,
                       validation_trace=None, random_run=11):
    if type(random_run) is not int or not 1 <= random_run < 2**63:
        raise ValueError("random run must be a positive integer below 2^63")
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
              "--randomSeed=1", f"--randomRun={random_run}", "--ecmpHashSeed=1",
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
    return result


def execution_profile_options(protection_mode, placement_mode=None, input_policy=None):
    """Current formal defaults; historical descriptions live in a separate module."""
    return (placement_mode if placement_mode is not None else ('compfrr' if protection_mode == 'compfrr' else 'fa-ffp'),
            input_policy if input_policy is not None else ('selective' if protection_mode == 'compfrr' else 'eager'))


def current_arguments(output, fault_mode="generate", audit=False, shadow=False,
                      validation_trace=None, protection_mode="compfrr", placement_mode=None,
                      remote_busy_recovery_policy="relocate", input_policy=None,
                      pressure_model="cumulative", placement_ablation="none", random_run=11):
    placement, policy = execution_profile_options(protection_mode, placement_mode, input_policy)
    if protection_mode not in ("off", "fixed", "compfrr", "recompute", "one-plus-one", "checkbullet"):
        raise ValueError("unsupported protection mode")
    if placement not in ("ffp", "lrl", "fa-ffp", "fa-lrl", "compfrr"):
        raise ValueError("unsupported placement policy")
    if placement == "compfrr" and protection_mode != "compfrr":
        raise ValueError("CompFRR-P requires adaptive CompFRR")
    if pressure_model not in ("cumulative", "idle-aware") or placement_ablation not in ("none", "noR", "noU", "noM"):
        raise ValueError("invalid formal pressure policy or ablation")
    if (placement != "compfrr" and (pressure_model != "cumulative" or placement_ablation != "none")) or (
            pressure_model == "idle-aware" and placement_ablation != "none"):
        raise ValueError("unsupported pressure/ablation combination")
    if remote_busy_recovery_policy not in ("relocate", "recompute"):
        raise ValueError("unsupported recovery policy")
    if policy not in ("eager", "deferred", "selective") or (policy != "eager" and protection_mode != "compfrr"):
        raise ValueError("deferred/selective INPUT requires adaptive CompFRR")
    if protection_mode == "compfrr" and fault_mode != "generate":
        raise ValueError("CompFRR formal evaluation requires online generate")
    if placement in ("lrl", "fa-lrl") and protection_mode == "off":
        raise ValueError("LRL requires protection")
    result = platform_arguments(output, fault_mode, audit, shadow, validation_trace, random_run)
    scheme = {"fixed": "compfrr", "checkbullet": "cb-sat"}.get(protection_mode, protection_mode)
    result += [f"--protectionScheme={scheme}"]
    if scheme in ("compfrr", "cb-sat"):
        result += ["--backupStorageBytesPerNode=10000000000"]
    if scheme == "compfrr":
        result += [f"--compfrrCheckpointPolicy={'fixed' if protection_mode == 'fixed' else 'adaptive'}",
                   f"--compfrrPlacementPolicy={placement}", f"--compfrrInputPolicy={policy}",
                   f"--compfrrRecoveryPolicy={remote_busy_recovery_policy}"]
        if placement == "compfrr":
            result += [f"--compfrrPressureModel={pressure_model}", f"--compfrrPlacementAblation={placement_ablation}"]
        if protection_mode == "fixed":
            result += ["--compfrrFixedDelta=0.05", "--compfrrFixedBatchN=4"]
    elif scheme != "off":
        # Explicit baseline-placement experiment override, never a public production flag.
        if placement != "fa-ffp":
            result[0] = "satcompute-protection-config-driver"
            result += [f"--testBaselinePlacement={placement}"]
        if scheme == "cb-sat" and remote_busy_recovery_policy != "recompute":
            result[0] = "satcompute-protection-config-driver"
            result += [f"--testCbSatBusyPolicy={remote_busy_recovery_policy}"]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--fault-mode", choices=("none", "generate", "validation-replay"), default="generate")
    parser.add_argument("--validation-trace", type=Path)
    parser.add_argument("--protection-mode", choices=("off", "fixed", "compfrr", "recompute", "one-plus-one", "checkbullet"), default="compfrr")
    parser.add_argument("--placement-mode", choices=("ffp", "lrl", "fa-ffp", "fa-lrl", "compfrr"),
                        help="Default compfrr for formal CompFRR; fa-ffp for baselines")
    parser.add_argument("--pressure-model", choices=("cumulative", "idle-aware"), default="cumulative")
    parser.add_argument("--placement-ablation", choices=("none", "noR", "noU", "noM"), default="none")
    parser.add_argument("--random-run", type=int, default=11, help="Explicit replicate; frozen default remains 11")
    parser.add_argument("--remote-busy-recovery-policy", choices=("relocate", "recompute"), default="relocate")
    parser.add_argument("--input-policy", choices=("eager", "deferred", "selective"),
                        help="Default selective for formal CompFRR; eager for baselines")
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--shadow", action="store_true", help="Read-only G4 validation, not real backup")
    args = parser.parse_args()
    args.placement_mode, args.input_policy = execution_profile_options(
        args.protection_mode, args.placement_mode, args.input_policy)
    output = args.output_dir.resolve()
    try:
        command = [str(ROOT / "ns3"), "run", "--no-build",
                   shlex.join(current_arguments(output, args.fault_mode, args.audit, args.shadow,
                                        args.validation_trace, args.protection_mode, args.placement_mode,
                                        remote_busy_recovery_policy=args.remote_busy_recovery_policy,
                                        input_policy=args.input_policy, pressure_model=args.pressure_model,
                                        placement_ablation=args.placement_ablation,
                                        random_run=args.random_run))]
        if output.exists():
            raise ValueError("refusing to overwrite an existing output directory")
    except (ValueError, OSError, KeyError) as error:
        parser.error(str(error))
    output.mkdir(parents=True)
    identity = {"command": command, "seed": 1, "run": args.random_run, "fault_mode": args.fault_mode,
                "validation_fault_trace": str(args.validation_trace.resolve()) if args.validation_trace else None,
                "protection_mode": args.protection_mode,
                "placement_mode": args.placement_mode, "lrl_recovery_weight": 1,
                "remote_busy_recovery_policy": args.remote_busy_recovery_policy,
                "input_policy": args.input_policy,
                # Historical metadata key describes layout only, not a second public switch.
                "input_staging_policy": "eager" if args.input_policy == "eager" else "deferred",
                "task_trace": f"{SCENE}/workload/task-trace.json", "f3_manifest": f"{SCENE}/fault/f3-manifest.json",
                "audit": args.audit, "shadow": args.shadow, "simulation_duration_s": 1300,
                "fixed_delay_seconds": 0.001,
                "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "worktree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT))}
    if args.placement_mode == "compfrr":
        identity["pressure_model"] = args.pressure_model
        identity["placement_ablation"] = args.placement_ablation
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
