#!/usr/bin/env python3
"""Coverage-first historical START audit; no simulator, predictor surrogate or selector.

The reviewed historical schema lacks the complete causal prediction/state inputs.
This entry deliberately stops at reconstruction coverage, even when a scalar P_F
is recoverable. Outcome labels never enter the feature builder.
"""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import runpy
import shlex

HISTORY = runpy.run_path(str(Path(__file__).with_name("jit_offline_audit.py")))
rows, require, unique, stamp = (HISTORY[k] for k in ("rows", "require", "unique", "stamp"))
NS = 1_000_000_000
EXECUTION = "367f23f393c45205cf87fbee003bfb668f453d5e"
ROLES = {"V6START": "CompFRR-JIT-V6START", "FULL_V7": "CompFRR-JIT-V7",
         "DEFERRED": "reference-R7-deferred-relocate"}
TASK_FIELDS = ("task_id", "task_profile", "source_node_id", "compute_node_id", "result_node_id",
               "input_bytes", "output_bytes", "compute_work_units", "compute_rate_work_units_per_second",
               "arrival_time_ns", "compute_start_time_ns", "baseline_compute_time_ns",
               "compute_deadline_budget_ns", "compute_deadline_time_ns")
DECISION_FIELDS = ("task_id", "fault_epoch_time_ns", "decision_trigger", "progress_work",
                   "local_node", "remote_node", "recovery_rate", "committed_delta_permille", "committed_n",
                   "first_sample_time_ns", "q_comp_snapshot", "p_fail_before_finish",
                   "input_bandwidth_bytes_per_s", "backup_bandwidth_bytes_per_s", "t_init_s")
UNKNOWN_TRAJECTORY = (
    "No complete per-check trajectory or START F1/F2 model-state snapshot in this historical schema; "
    "scalar risk does not identify the sequence. No validated prefix-state replay is available.")
UNKNOWN_CHECKPOINT = (
    "Initial cadence is known, but no causal fixed-cadence initialization/state/tail forecast is logged. "
    "Actual later commits and ON updates are not permitted inputs. The recovery estimator only joins supplied times.")


def command_controls(execution):
    pairs = [arg.split("=", 1) for arg in shlex.split(execution["command"][-1])[1:]]
    require(all(len(p) == 2 for p in pairs) and len(dict(pairs)) == len(pairs), "ambiguous controls")
    return dict(pairs)


def role_check(execution, role, task_count):
    require(role in ROLES, "unknown source role")
    require(execution["commit"] == EXECUTION and task_count == 800, "execution/task identity mismatch")
    controls = command_controls(execution)
    expected = "deferred" if role == "DEFERRED" else "jit"
    require(execution["input_staging_policy"] == expected and
            controls.get("--inputStagingPolicy") == expected, "source role/mode mismatch")
    if role != "DEFERRED":
        enabled = role == "FULL_V7"
        require(execution.get("jit_start_benefit") is enabled and
                controls.get("--jitStartBenefit") == str(int(enabled)), "V6START/Full-V7 role mismatch")
    else:
        require(controls.get("--jitStartBenefit", "0") == "0", "Deferred anchor has JIT START benefit")
    require(controls.get("--randomSeed") == "1" and controls.get("--randomRun") == "11" and
            controls.get("--simulationDuration") == "1300" and controls.get("--faultMode") == "generate" and
            controls.get("--faultProbabilityAudit") == "0", "historical controls mismatch")


def identity_audit(roots):
    require(set(roots) == set(ROLES) and len(set(roots.values())) == 3, "cohort roots must be distinct")
    tasks, identity = {}, {}
    for role, root in roots.items():
        require(root.name == ROLES[role], "historical directory role mismatch")
        tasks[role] = unique(rows(root, "task-summary.csv"), "task_id")
        execution = json.loads((root / "execution.json").read_text())
        role_check(execution, role, len(tasks[role]))
        identity[role] = dict(root=str(root), execution=execution, task_count=len(tasks[role]))
    anchor = roots["DEFERRED"]
    # Fixed historical batch: configuration, actual fault identity and static workload
    # must match. Task dispatch/absolute deadlines can differ causally; report them,
    # and require exact absolute deadline equality for each fault label join below.
    faults = rows(anchor, "fault-events.csv")
    for role in ("V6START", "FULL_V7"):
        evidence = HISTORY["fairness"](roots[role], anchor, tasks[role], tasks["DEFERRED"])
        require(rows(roots[role], "fault-events.csv") == faults, "actual fault identity mismatch")
        identity[role].update(same_nonpolicy_controls=evidence["same_nonpolicy_controls"],
                              same_static_workload_and_deadline_budgets=True, same_fault_events=True,
                              dispatch_deadline_differences=[dict(task_id=t, fields=[k for k in
                                  ("compute_start_time_ns", "compute_deadline_time_ns")
                                  if tasks[role][t][k] != tasks["DEFERRED"][t][k]])
                                  for t in tasks[role] if any(tasks[role][t][k] != tasks["DEFERRED"][t][k]
                                  for k in ("compute_start_time_ns", "compute_deadline_time_ns"))])
    return tasks, identity


def evidence_metadata(roots):
    """Read-only change detector, not a checksum/security or reproducibility layer."""
    return {str(p): (p.stat().st_size, p.stat().st_mtime_ns)
            for root in roots for p in root.rglob("*") if p.is_file()}


def candidate_starts(decisions, events, selections):
    committed = [r for r in decisions if r["proposed_action"] == "START" and r["decision_committed"] == "1"]
    unique(committed, "task_id")
    physical = unique([r for r in events if r["event"] == "START"], "task_id")
    admitted = unique([r for r in selections if r["actual_admission"] == "ACCEPTED"], "task_id")
    committed_ids = {r["task_id"] for r in committed}
    require(physical.keys() <= committed_ids and admitted.keys() <= physical.keys(), "orphan START/admission")
    accepted, excluded = [], []
    for line, r in enumerate(decisions, 2):
        if r["proposed_action"] != "START" or r["decision_committed"] != "1":
            continue
        t = r["task_id"]
        if t not in physical or t not in admitted:
            excluded.append(dict(task_id=t, reason="COMMITTED_BUT_NOT_PHYSICALLY_ADMITTED"))
            continue
        require(r["phase_before"] == "OFF" and r["actual_fault_hit"] == "0", "invalid admitted START phase/survival")
        require(r["actual_fault_sampled"] == ("1" if r["decision_trigger"] == "FAULT_EPOCH" else "0"),
                "START trigger/sample mismatch")
        for record in (physical[t], admitted[t]):
            require(record["time_ns"] == r["fault_epoch_time_ns"] and
                    all(record[k] == r[k] for k in ("local_node", "remote_node")), "START time/pair mismatch")
        require(physical[t]["attempt_generation"] == "0", "nonprimary START")
        accepted.append(dict(r, csv_line=line))
    return accepted, excluded


def post_commit_window(now, finish, first, trigger, interval=NS):
    require(interval > 0 and 0 <= now < finish and first >= now, "invalid window")
    require(first % interval == 0, "noncanonical sample")
    if trigger == "FAULT_EPOCH":
        require(first == now and now > 0, "epoch missing current sample")
        return now + interval, True
    require(trigger in ("TASK_RUNNING", "CAPACITY_RELEASE"), "unknown decision trigger")
    # A pending same-time check must be retained. Do not manufacture a sample.
    require((first == now and now > 0) or first == now + interval - now % interval,
            "non-epoch first sample is not the canonical pending/next check")
    return first, False


def future_mass(total, current, excluded, finish, interval=NS):
    if total is None:
        return None, "Missing scalar prediction"
    require(math.isfinite(total) and 0 <= total <= 1, "invalid probability")
    if not excluded:
        return total, "QueryTaskPrediction already uses the pending/next grid and finish-exclusive window"
    if finish % interval == 0:
        return None, "Epoch prediction includes the finish endpoint; its probability was not logged separately"
    if current is None:
        return None, "Missing current sample probability"
    require(math.isfinite(current) and 0 <= current <= 1, "invalid current probability")
    if current == 1:
        return None, "Cannot condition on a zero-probability survivor"
    result = (total - current) / (1 - current)
    require(-1e-12 <= result <= 1 + 1e-12, "inconsistent total/current probability")
    return min(1.0, max(0.0, result)), "P_future=(P_logged-q_survived)/(1-q_survived); no certainty normalization"


def feature_snapshot(role, task, decision):
    """Allowlisted static/at-START data only; no fault, recovery or later checkpoint API."""
    task = {k: task[k] for k in TASK_FIELDS}
    row = {k: decision[k] for k in DECISION_FIELDS}
    start = int(row["fault_epoch_time_ns"])
    begin = int(task["compute_start_time_ns"])
    duration = int(task["baseline_compute_time_ns"])
    require(0 <= begin <= start < begin + duration, "START outside primary compute window")
    remaining = begin + duration - start
    first, excluded = post_commit_window(start, begin + duration, int(row["first_sample_time_ns"]),
                                        row["decision_trigger"])
    local = task["source_node_id"] == row["remote_node"]
    snapshot = dict(cohort=role, task_id=task["task_id"], start_time_ns=start,
                    frequency_csv_line=decision["csv_line"], **{k: v for k, v in task.items() if k != "task_id"},
                    **{k: v for k, v in row.items() if k != "task_id"})
    snapshot.update(delivery_mode="LOCAL" if local else "NETWORK", checkpoint_ready_at_decision=False)
    for name in ("input_bandwidth_bytes_per_s", "backup_bandwidth_bytes_per_s", "t_init_s"):
        snapshot["proposal_" + name] = snapshot.pop(name)
    features = dict(cohort=role, task_id=task["task_id"])
    audit = []

    def field(name, value, status, provenance, reason):
        require((value is None) == (status == "UNKNOWN"), "UNKNOWN must stay null")
        features[name] = value
        audit.append(dict(cohort=role, task_id=task["task_id"], feature=name, value=value,
                          status=status, provenance=provenance, reason=reason))

    logged = "frequency-decisions.csv:" + str(decision["csv_line"])
    field("input_bytes", int(task["input_bytes"]), "DIRECT_LOGGED", "task-summary.csv:input_bytes", "Static task input")
    field("profile", task["task_profile"], "DIRECT_LOGGED", "task-summary.csv:task_profile", "Static task profile")
    for key in ("local_node", "remote_node", "committed_delta_permille", "committed_n", "progress_work"):
        field(key, int(row[key]), "DIRECT_LOGGED", logged + ":" + key, "Committed START, not final ON cadence")
    field("recovery_rate", float(row["recovery_rate"]), "DIRECT_LOGGED", logged + ":recovery_rate", "Fixed designated remote rate")
    field("deadline_ns", int(task["compute_deadline_time_ns"]), "DIRECT_LOGGED", "task-summary.csv",
          "Original compute deadline established at primary dispatch")
    field("remaining_compute_s", remaining / NS, "CAUSALLY_RECONSTRUCTIBLE", "task-summary.csv+START time",
          "GetRunningTaskSnapshot: original service duration minus elapsed time, no actual future completion")
    field("first_future_sample_ns", first, "CAUSALLY_RECONSTRUCTIBLE", logged + ":first_sample_time_ns",
          "Exclude survived epoch; preserve pending sample on non-epoch triggers")
    def number(key):
        return float(row[key]) if row[key] != "" else None
    mass, reason = future_mass(number("p_fail_before_finish"), number("q_comp_snapshot"), excluded, begin + duration)
    field("P_F", mass, "UNKNOWN" if mass is None else "CAUSALLY_RECONSTRUCTIBLE", logged, reason)
    count = max(0, (begin + duration - 1 - first) // NS + 1)
    field("future_sample_count", count, "CAUSALLY_RECONSTRUCTIBLE", "canonical grid + original service finish",
          "Finish-exclusive count after conditioning on current survival")
    next_q = mass if count == 1 else number("q_comp_snapshot") if first == start else None
    trajectory = ([dict(time_ns=first, q_comp=mass, first_failure_mass=mass)]
                  if count == 1 and mass is not None else [] if count == 0 else None)
    field("q_next", next_q, "UNKNOWN" if next_q is None else "CAUSALLY_RECONSTRUCTIBLE", logged,
          "One-check window has q_next=P_F; a pending current check has the logged snapshot q" if next_q is not None
          else UNKNOWN_TRAJECTORY)
    field("future_first_failure_trajectory", json.dumps(trajectory) if trajectory is not None else None,
          "UNKNOWN" if trajectory is None else "CAUSALLY_RECONSTRUCTIBLE", logged,
          "Empty or one-check union-probability trajectory is uniquely determined; no F1/F2 decomposition inferred"
          if trajectory is not None else UNKNOWN_TRAJECTORY)
    field("checkpoint_state_tail_forecast", None, "UNKNOWN", "checkpoint manager + recovery estimator", UNKNOWN_CHECKPOINT)
    field("initialization_estimate_at_proposal_s", number("t_init_s"), "DIRECT_LOGGED", logged + ":t_init_s",
          "Proposal estimate only, not actual future receiver-ready or state-only recovery wait")
    bandwidth = number("input_bandwidth_bytes_per_s")
    # Non-epoch Evaluate -> AfterEpoch is synchronous in this pinned historical
    # controller; FAULT_EPOCH can have a full batch/other admissions in between.
    known_path = not excluded and bandwidth is not None and math.isfinite(bandwidth) and bandwidth > 0
    ti = 0.0 if local else int(task["input_bytes"]) / bandwidth if known_path else None
    field("input_serialization_s", ti, "UNKNOWN" if ti is None else "CAUSALLY_RECONSTRUCTIBLE", logged,
          "LocalDelivery, no UDP" if local else "S/B: synchronous proposal-to-commit, not receiver-ready" if known_path
          else "No source-to-actual-remote post-batch path/rate snapshot; proposal bandwidth is not that evidence")
    field("input_to_remaining_ratio", ti / (remaining / NS) if ti is not None else None,
          "UNKNOWN" if ti is None else "CAUSALLY_RECONSTRUCTIBLE", "input_serialization_s/remaining_compute_s",
          "Serialization estimate only")
    for key in ("P_Iimpact", "G_I_s", "P_Iddl"):
        field(key, 0.0 if local else None, "CAUSALLY_RECONSTRUCTIBLE" if local else "UNKNOWN",
              "LocalDelivery identity" if local else "historical schema inspection",
              "Both INPUT waits are zero; no INPUT-only gain or deadline rescue" if local else
              "Requires per-check risk, initialization validity and fixed-cadence legal state/tail inputs; " + UNKNOWN_CHECKPOINT)
    field("D_I_ms_per_MB", None, "UNKNOWN", "not applicable" if local else "G_I_s/input_bytes",
          "LocalDelivery excluded from network byte density" if local else "G_I is unknown")
    field("planned_network_input_bytes", 0 if local else int(task["input_bytes"]), "CAUSALLY_RECONSTRUCTIBLE",
          "static task size+designated pair", "Hypothetical full staging payload, not admitted or actual transmitted bytes")
    return snapshot, features, audit


def primary_impacts(records):
    return unique([r for r in records if r["impact_type"] in
                   ("RUNNING_INTERRUPTED", "RUNNING_INTERRUPTED_PERMANENT")], "task_id")


def label_candidate(task, anchor_task, impact, anchor_impact, recovery, anchor_recovery):
    """Outcome-only join; absence of a recovery alone is not proof of NO_FAULT."""
    result = dict(label="UNKNOWN", input_critical_wait_ns=None, reason="Incomplete matched outcome evidence")
    if impact is None and anchor_impact is None and recovery is None and anchor_recovery is None:
        if task["task_success"] == anchor_task["task_success"] == "1":
            result.update(label="NO_FAULT", reason="Both complete; neither has a primary running interruption or recovery")
        return result
    if any(x is None for x in (impact, anchor_impact, recovery, anchor_recovery)):
        return result
    keys = ("task_id", "fault_id", "fault_type", "fault_time_ns", "fault_node_id", "compute_deadline_time_ns")
    if any(impact[k] != anchor_impact[k] for k in keys):
        return result
    for t, i, r in ((task, impact, recovery), (anchor_task, anchor_impact, anchor_recovery)):
        if not (r["task_id"] == i["task_id"] == t["task_id"] and r["fault_id"] == i["fault_id"] and
                r["fault_time_ns"] == i["fault_time_ns"] and r["primary_node"] == i["fault_node_id"] == t["compute_node_id"] and
                r["original_deadline_ns"] == i["compute_deadline_time_ns"] == t["compute_deadline_time_ns"]):
            return result
    if any(recovery[k] != anchor_recovery[k] for k in ("fault_type", "recovery_node", "chosen_path")):
        return result
    if stamp(anchor_recovery, "recovery_accept_time_ns") is None:
        return result
    join = HISTORY["dependency_join"](anchor_recovery)
    if not join["dependency_join_known"]:
        return result
    return dict(label="NEEDED" if join["input_critical_wait_ns"] > 0 else "FAULT_NONCRITICAL",
                input_critical_wait_ns=join["input_critical_wait_ns"], reason="Matched primary fault/deadline/target/path and accepted Deferred dependency join")


def coverage_gate(audit):
    coverage = defaultdict(Counter)
    for r in audit:
        coverage[r["feature"]][r["status"]] += 1
    required = ("P_F", "future_first_failure_trajectory", "input_serialization_s",
                "checkpoint_state_tail_forecast", "P_Iimpact", "G_I_s", "P_Iddl")
    missing = [name for name in required if not coverage[name] or coverage[name]["UNKNOWN"]]
    return dict(status="STOP_RECONSTRUCTION_INSUFFICIENT" if missing else "COVERAGE_COMPLETE_REVIEW_REQUIRED",
                coverage=dict(coverage), missing_required_features=missing,
                score_sweep="SKIPPED_RECONSTRUCTION_GATE", reference_points="SKIPPED_RECONSTRUCTION_GATE")


def analyze(roots):
    tasks, identity = identity_audit(roots)
    selections = {}
    for role, root in roots.items():
        selections[role] = candidate_starts(rows(root, "frequency-decisions.csv"),
            rows(root, "protection-events.csv"), rows(root, "placement-selections.csv"))
    anchor = roots["DEFERRED"]
    anchor_starts = {r["task_id"]: r for r in selections["DEFERRED"][0]}
    anchor_impacts = primary_impacts(rows(anchor, "fault-task-impact.csv"))
    anchor_recovery = unique(rows(anchor, "recovery-summary.csv"), "task_id")
    snapshots, features, audits, labels, cohorts = [], [], [], [], {}
    for role in ("V6START", "FULL_V7"):
        root = roots[role]
        # Reuse lifecycle/physical-byte/receiver audit, not a second accounting system.
        history = HISTORY["analyze"](root, anchor)
        lifecycle = {str(r["task_id"]): r for r in history["lifecycles"]}
        paired = {str(r["task_id"]): r for r in history["paired_comparison"]["pairs"]}
        impacts = primary_impacts(rows(root, "fault-task-impact.csv"))
        recovery = unique(rows(root, "recovery-summary.csv"), "task_id")
        own_labels, own_audit, differences = [], [], []
        for start in selections[role][0]:
            tid = start["task_id"]
            s, f, a = feature_snapshot(role, tasks[role][tid], start)
            snapshots.append(s); features.append(f); audits.extend(a); own_audit.extend(a)
            peer = anchor_starts.get(tid)
            diff = [k for k in ("local_node", "remote_node", "fault_epoch_time_ns", "committed_delta_permille", "committed_n")
                    if peer is None or start[k] != peer[k]]
            if diff:
                differences.append(dict(task_id=tid, fields=diff,
                    candidate_values={k: start[k] for k in diff},
                    anchor_values={k: peer[k] for k in diff} if peer else None))
            label = label_candidate(tasks[role][tid], tasks["DEFERRED"][tid], impacts.get(tid),
                                    anchor_impacts.get(tid), recovery.get(tid), anchor_recovery.get(tid))
            life, r, pair = lifecycle.get(tid), recovery.get(tid), paired.get(tid)
            out = dict(cohort=role, task_id=tid, **label, historical_prefetched=bool(life),
                historical_used=life["actually_used"] if life else False,
                historical_state_at_fault=r["input_state_at_fault"] if r else None,
                historical_fault_source=impacts[tid]["fault_type"] if tid in impacts else None,
                paired_catch_delta_ns=pair["delta_catch_paired_ns"] if pair else None,
                paired_catch_outcome=("IMPROVED" if pair["delta_catch_paired_ns"] > 0 else
                                      "WORSE" if pair["delta_catch_paired_ns"] < 0 else "EQUAL") if pair else None,
                wrong_target=life["same_recovery_target"] is False if life else False,
                relocate=r.get("checkpoint_relocation_attempted") == "1" if r else False,
                recompute=r["chosen_path"] == "RECOMPUTE" if r else False,
                designated_pair_matches_anchor=peer is not None and not diff,
                planned_network_input_bytes=f["planned_network_input_bytes"])
            labels.append(out); own_labels.append(out)
        cohorts[role] = dict(candidate_count=len(own_labels), all_tasks=len(tasks[role]),
            labels={name: sum(r["label"] == name for r in own_labels)
                    for name in ("NEEDED", "FAULT_NONCRITICAL", "NO_FAULT", "UNKNOWN")},
            faulted_among_starts=sum(r["task_id"] in impacts for r in own_labels),
            historical_prefetched=sum(r["historical_prefetched"] for r in own_labels),
            historical_used=sum(r["historical_used"] for r in own_labels),
            local_delivery_candidates=sum(r["planned_network_input_bytes"] == 0 for r in own_labels),
            decision_triggers=dict(Counter(r["decision_trigger"] for r in selections[role][0])),
            excluded_commits=selections[role][1], anchor_start_differences=differences,
            dependency_known_pairs=history["paired_comparison"]["count"],
            gate=coverage_gate(own_audit))
    return dict(identity=identity, snapshots=snapshots, features=features, audit=audits, labels=labels,
                summary=dict(status=cohorts["V6START"]["gate"]["status"], cohorts=cohorts,
                    raw_access="READ_ONLY", production_changed=False, simulations_started=0,
                    reason="Historical evidence coverage does not establish selector separability. No runtime-information impossibility claim.",
                    skipped=["score-sweeps.csv", "reference-points.csv", "separability", "runtime Phase 2"]))


def write_outputs(output, result):
    output.mkdir(parents=True, exist_ok=False)
    for name, key in (("source-identity", "identity"), ("summary", "summary")):
        with (output / (name + ".json")).open("x") as stream:
            json.dump(result[key], stream, indent=2, allow_nan=False)
            stream.write("\n")
    for name, key in (("candidate-start-snapshots", "snapshots"), ("feature-reconstruction-audit", "audit"),
                      ("candidate-labels", "labels"), ("candidate-features", "features")):
        HISTORY["write_csv"](output / (name + ".csv"), result[key])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    roots = {role: (args.history_root / name).resolve() for role, name in ROLES.items()}
    output = args.output_dir.resolve()
    HISTORY["output_guard"](output, [args.history_root, *roots.values()])
    before = evidence_metadata(roots.values())
    result = analyze(roots)
    require(before == evidence_metadata(roots.values()), "raw evidence changed during analysis")
    result["summary"]["raw_evidence_metadata_unchanged"] = True
    result["summary"]["raw_evidence_file_count"] = len(before)
    write_outputs(output, result)
    print(json.dumps(result["summary"], indent=2, allow_nan=False))
    # A valid negative coverage audit is distinct from a source/contract exception.
    return 2 if result["summary"]["status"] == "STOP_RECONSTRUCTION_INSUFFICIENT" else 0


if __name__ == "__main__":
    raise SystemExit(main())
