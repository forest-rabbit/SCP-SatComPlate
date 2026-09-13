#!/usr/bin/env python3
"""Ten independent off/F1/F2 pilots; pooled eligible-check exposure, never formal-run fitting."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
from cb_tools import (ROOT, PROFILE, SCENE_HELPER, batch, clean_head, flags, new_execution,
                      replace_flag, require, rows, scene_identity, utc, write_json)


def estimate(probabilities, states, faults, interval_ns):
    """Task-specific running rows intersected with actual node sampling eligibility."""
    require(isinstance(interval_ns, int) and interval_ns > 0, "invalid calibration clock")
    key = lambda row: (int(row["simulation_time_ns"]), int(row["node_id"]))
    state_map = {}
    duplicate_states = duplicate_probabilities = 0
    for row in states:
        k = key(row)
        if k in state_map:
            require(row == state_map[k], "conflicting duplicate state audit")
            duplicate_states += 1
        state_map[k] = row
    eligible = {}
    node_counts = defaultdict(lambda: dict(eligible_checks=0, joint_failures=0, q_sum=0.0))
    for row in probabilities:
        k = key(row)
        require(k in state_map, "probability missing model-state evidence")
        if int(row["remaining_compute_time_ns"]) <= 0:
            continue  # Completion wins at its inclusive same-ns boundary.
        state = state_map[k]
        require(state["sampling_eligible"] == "1", "running audit is not sampling eligible")
        q1, q2 = float(row["f1_step_failure_probability"]), float(row["f2_step_failure_probability"])
        q = float(row["combined_step_failure_probability"])
        require(all(math.isfinite(x) and 0 <= x <= 1 for x in (q1,q2,q)), "invalid sampling probability")
        require(math.isclose(q, 1-(1-q1)*(1-q2), abs_tol=1e-12) and
                math.isclose(q, float(state["p_compute"]), abs_tol=1e-12), "sampling q mismatch")
        if k in eligible:
            require(row == eligible[k], "conflicting duplicate task/node check")
            duplicate_probabilities += 1
            continue
        eligible[k] = row
        node_counts[k[1]]["eligible_checks"] += 1
        node_counts[k[1]]["q_sum"] += q
    observed = {}
    eligible_hits = []
    outside = 0
    for f in faults:
        if not f["fault_occurred"]:
            continue
        require(f["fault_type"] == "compute", "F3 present in F1/F2 pilot")
        require(f["f1_occurred"] or f["f2_occurred"], "compute event without F1/F2 provenance")
        k = (int(f["start_time_ns"]), int(f["node_id"]))
        if k in observed:
            require(observed[k] == f, "duplicate node/check has conflicting faults")
            continue
        observed[k] = f
        if k in eligible:
            node_counts[k[1]]["joint_failures"] += 1  # Simultaneous F1+F2 is ONE outage.
            eligible_hits.append(k)
        else:
            outside += 1
    checks = len(eligible)
    failures = sum(v["joint_failures"] for v in node_counts.values())
    exposure = checks * interval_ns / 1e9
    require(checks > 0, "no eligible ordinary running exposure")
    return dict(eligible_check_count=checks, joint_failure_count=failures,
                eligible_exposure_seconds=exposure, mtbf_seconds=exposure/failures if failures else None,
                outside_scope_fault_count=outside, duplicate_probability_rows=duplicate_probabilities,
                duplicate_state_rows=duplicate_states, nodes=dict(sorted(node_counts.items())),
                eligible_fault_keys=sorted(eligible_hits))


def analyze(root):
    pilots = []
    heads = set()
    for run in range(101, 111):
        directory = root / f"pilot-{run}"
        e = json.loads((directory / "execution.json").read_text())
        result = json.loads((directory / "execution-result.json").read_text())
        import shlex
        config = flags(shlex.split(e["command"][-1]))
        require(result["returncode"] == 0 and not e["worktree_dirty"], "pilot not a finished clean run")
        require(e["run"] == run and e["seed"] == 1 and e["protection_mode"] == "off" and
                config["faultEnableF3"] == "0" and config["faultMode"] == "generate" and
                config["faultEnableF1"] == config["faultEnableF2"] == config["faultProbabilityAudit"] == "1",
                "pilot configuration differs")
        require(e["scene"] == scene_identity(), "pilot workload evidence differs")
        state = rows(directory, "fault-model-state.csv")
        times = sorted({int(r["simulation_time_ns"]) for r in state})
        gaps = {b-a for a,b in zip(times, times[1:])}
        require(len(gaps) == 1 and next(iter(gaps)) == 1_000_000_000, "pilot clock changed")
        require(times[-1] >= 1298_000_000_000 and e["simulation_duration_s"] == 1300, "pilot truncated")
        evidence = estimate(rows(directory, "fault-model-probabilities.csv"), state,
                            json.loads((directory / "fault-trace.json").read_text())["faults"], gaps.pop())
        impacts = rows(directory, "fault-task-impact.csv")
        actual = {(int(r["fault_time_ns"]), int(r["fault_node_id"])) for r in impacts
                  if r["task_state_before_fault"] == "RUNNING" and r["progress_valid"] == "1" and
                  int(r["remaining_work_units_at_fault"]) > 0}
        require(actual == set(evidence["eligible_fault_keys"]), "eligible hits and primary victim ledger disagree")
        # Diagnostic continuous service clock is separate from the discrete sampling denominator.
        service_ns = 0
        for task in rows(directory, "task-summary.csv"):
            start = int(task["compute_start_time_ns"])
            if start < 0:
                continue
            end = int(task["compute_complete_time_ns"])
            if end < 0:
                end = int(task["failure_time_ns"])
            require(end >= start, "pilot service end missing")
            service_ns += end-start
        evidence.update(run=run, actual_primary_service_seconds=service_ns/1e9)
        pilots.append(evidence)
        heads.add(e["commit"])
    require(len(heads) == 1, "mixed execution commits in calibration")
    count = sum(p["eligible_check_count"] for p in pilots)
    failures = sum(p["joint_failure_count"] for p in pilots)
    profile = dict(scope="healthy_ordinary_running_primary_before_draw",
        clock="eligible_checks_times_check_interval", protection_mode="off", f3_enabled=False,
        seed=1, runs=list(range(101,111)), check_interval_ns=1_000_000_000,
        eligible_check_count=count, eligible_exposure_seconds=float(count), joint_failure_count=failures,
        mtbf_seconds=count/failures if failures else None, source_execution=str(root.relative_to(ROOT)),
        execution_commit=heads.pop(), created_utc=utc(), pilots=pilots)
    write_json(root / "calibration-summary.json", profile)
    require(not PROFILE.exists(), "frozen MTBF profile already exists; refusing replacement")
    write_json(PROFILE, profile)
    return profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", choices=("all", "run", "analyze"), default="all")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--root", type=Path)
    args = parser.parse_args()
    require(not PROFILE.exists(), "frozen MTBF already exists; no silent recalibration")
    require(args.stage != "analyze" or args.root, "analyze requires the existing pilot root")
    root = args.root.resolve() if args.root else new_execution("calibration")
    if args.stage in ("all", "run"):
        head, scene = clean_head(), scene_identity()
        root.mkdir(parents=True, exist_ok=True)
        commands = {}
        for run in range(101,111):
            name = f"pilot-{run}"
            command = SCENE_HELPER["arguments"](root / name, audit=True)
            command = replace_flag(command, "protectionMode", "off")
            command = replace_flag(command, "randomRun", run)
            commands[name] = replace_flag(command, "faultEnableF3", 0)
        batch(root, args.jobs, commands, head, dict(stage="calibration", scene=scene))
    if args.stage in ("all", "analyze"):
        profile = analyze(root)
        print(json.dumps({k:profile[k] for k in ("mtbf_seconds", "eligible_exposure_seconds", "joint_failure_count")}), flush=True)
    print(root, flush=True)


if __name__ == "__main__":
    main()
