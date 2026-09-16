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
CONFIG_ARGUMENTS = runpy.run_path(str(Path(__file__).with_name('config_arguments.py')))
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


def canonical_input_arguments(argv):
    """Read historical command evidence; never install legacy aliases in the CLI.

    Only the exact eager/deferred + none and deferred + SER mappings are equivalent.
    Reject duplicate/conflicting controls rather than hiding them in an audit.
    """
    values = {}
    other = []
    for token in argv:
        key, separator, value = token.partition('=')
        if key in ('--inputPolicy', '--inputStagingPolicy', '--inputAdmissionPolicy'):
            if not separator or key in values:
                raise ValueError('duplicate/malformed INPUT evidence option')
            values[key] = value
        else:
            other.append(token)
    if '--inputPolicy' in values:
        if len(values) != 1:
            raise ValueError('conflicting old/new INPUT evidence options')
        mode = values['--inputPolicy']
    else:
        mode = values.get('--inputStagingPolicy', 'eager')
        admission = values.get('--inputAdmissionPolicy', 'none')
        if admission != 'none':
            if (mode, admission) != ('deferred', 'ser-break-even'):
                raise ValueError('retired or invalid historical INPUT selector')
            mode = 'selective'
    if mode not in ('eager', 'deferred', 'selective'):
        raise ValueError('unknown INPUT evidence policy')
    return other + [f'--inputPolicy={mode}']


def canonical_experiment_arguments(argv):
    """Normalize old/new command evidence without altering stored historical files."""
    has_new = any(x.lstrip('-').partition('=')[0] in CONFIG_ARGUMENTS['NEW'] for x in argv[1:])
    if has_new:
        if any(x.partition('=')[0] in ('--inputStagingPolicy', '--inputAdmissionPolicy') for x in argv):
            raise ValueError('conflicting old/new INPUT evidence options')
    else:
        argv = canonical_input_arguments(argv)
    return CONFIG_ARGUMENTS['canonical_protection_arguments'](argv)


def historical_comparison_arguments(argv):
    """Read-only legacy-shaped controls for older cross-scheme paired audits.

    Expand inert legacy descriptors only for comparing those recorded tables;
    they are not active resource settings and must never be launched as argv.
    Current experiment identity uses canonical_experiment_arguments instead.
    """
    normalized = canonical_experiment_arguments(argv)
    values = dict(x.removeprefix('--').split('=', 1) for x in normalized[1:])
    protected = CONFIG_ARGUMENTS['NEW'] | CONFIG_ARGUMENTS['SHARED']
    other = {k: v for k, v in values.items() if k not in protected}
    scheme = values['protectionScheme']
    mode = {'cb-sat': 'checkbullet'}.get(scheme, scheme)
    if scheme == 'compfrr' and values['compfrrCheckpointPolicy'] == 'fixed':
        mode = 'fixed'
    placement = values.get('compfrrPlacementPolicy', values.get('testBaselinePlacement', 'fa-ffp'))
    pressure = values.get('compfrrPressureModel', 'cumulative')
    variant = {'idle-aware': 'rational-U', 'historical-only:recent-U': 'recent-U'}.get(pressure)
    if variant is None:
        variant = values.get('compfrrPlacementAblation', 'none')
        variant = 'full' if variant == 'none' else variant
    other.update(protectionMode=mode, placementMode='n5c' if placement == 'compfrr' else placement,
        n5cVariant=variant, inputPolicy=values.get('compfrrInputPolicy', 'eager'),
        remoteBusyRecoveryPolicy=values.get('compfrrRecoveryPolicy', values.get('testCbSatBusyPolicy', 'relocate')),
        lrlRecoveryWeight=values.get('testLrlRecoveryWeight', '1'),
        fixedProtectionDelta=values.get('compfrrFixedDelta', '0.05'),
        fixedProtectionBatchN=values.get('compfrrFixedBatchN', '4'),
        backupStorageBytesPerNode=values.get('backupStorageBytesPerNode', '10000000000'))
    return ['satcompute', *[f'--{key}={value}' for key, value in sorted(other.items())]]


# The historical command-description API retains recent-U for old evidence comparisons.
# Neither this CLI nor the current production binary can execute that archived variant.
def arguments(output, fault_mode="generate", audit=False, shadow=False,
              validation_trace=None, protection_mode="off", placement_mode="fa-ffp", lrl_weight=1,
              remote_busy_recovery_policy="relocate", input_policy="eager", n5c_variant="full",
              random_run=11):
    if type(random_run) is not int or not 1 <= random_run < 2**63:
        raise ValueError("random run must be a positive integer below 2^63")
    if protection_mode not in ("off", "fixed", "compfrr", "recompute", "one-plus-one", "checkbullet") or placement_mode not in ("ffp", "lrl", "fa-ffp", "fa-lrl", "n5c"):
        raise ValueError("unsupported protection/placement mode")
    if placement_mode == "n5c" and protection_mode != "compfrr":
        raise ValueError("N5C requires CompFRR")
    if n5c_variant not in ("full", "noR", "noU", "noM", "recent-U", "rational-U") or (placement_mode != "n5c" and n5c_variant != "full"):
        raise ValueError("invalid N5C ablation")
    if placement_mode in ("lrl", "fa-lrl") and protection_mode == "off":
        raise ValueError("LRL requires an enabled protection scheme")
    if remote_busy_recovery_policy not in ("recompute", "relocate"):
        raise ValueError("unsupported remote-busy recovery policy")
    if input_policy not in ("eager", "deferred", "selective") or (input_policy != "eager" and protection_mode != "compfrr"):
        raise ValueError("deferred/selective INPUT requires CompFRR")
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
    if protection_mode != "off":
        result += [f"--protectionMode={protection_mode}", "--backupStorageBytesPerNode=10000000000",
                   "--fixedProtectionDelta=0.05", "--fixedProtectionBatchN=4",
                   f"--placementMode={placement_mode}", f"--lrlRecoveryWeight={lrl_weight}",
                   f"--remoteBusyRecoveryPolicy={remote_busy_recovery_policy}"]
    if input_policy != "eager":
        result += [f"--inputPolicy={input_policy}"]
    if placement_mode == "n5c":
        result += [f"--n5cVariant={n5c_variant}"]
    return result


def execution_profile_options(protection_mode, placement_mode=None, input_policy=None):
    """Current formal CLI defaults; the historical arguments() API stays frozen."""
    return (placement_mode if placement_mode is not None else ('n5c' if protection_mode == 'compfrr' else 'fa-ffp'),
            input_policy if input_policy is not None else ('selective' if protection_mode == 'compfrr' else 'eager'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--fault-mode", choices=("none", "generate", "validation-replay"), default="generate")
    parser.add_argument("--validation-trace", type=Path)
    parser.add_argument("--protection-mode", choices=("off", "fixed", "compfrr", "recompute", "one-plus-one", "checkbullet"), default="compfrr")
    parser.add_argument("--placement-mode", choices=("ffp", "lrl", "fa-ffp", "fa-lrl", "n5c"),
                        help="Default n5c for formal CompFRR; fa-ffp for baselines")
    parser.add_argument("--n5c-variant", choices=("full", "noR", "noU", "noM", "rational-U"), default="full")
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
                   shlex.join(CONFIG_ARGUMENTS['execution_arguments'](arguments(output, args.fault_mode, args.audit, args.shadow,
                                        args.validation_trace, args.protection_mode, args.placement_mode,
                                        remote_busy_recovery_policy=args.remote_busy_recovery_policy,
                                        input_policy=args.input_policy, n5c_variant=args.n5c_variant,
                                        random_run=args.random_run)))]
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
    if args.placement_mode == "n5c":
        identity["n5c_variant"] = args.n5c_variant
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
