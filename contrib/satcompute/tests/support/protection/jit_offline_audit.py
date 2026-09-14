#!/usr/bin/env python3
"""Historical V7 lifecycle diagnosis, not a production JIT implementation or simulator."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import runpy
import shlex

STAGING = runpy.run_path(str(Path(__file__).with_name("input_staging_audit.py")))
BASE = STAGING["BASE"]
rows, require = STAGING["rows"], STAGING["require"]
NS = STAGING["NS"]


def stamp(row, key):
    """Missing/negative sentinel is unknown, never time zero."""
    value = row.get(key)
    return int(value) if value not in (None, "") and int(value) >= 0 else None


def actual(row, key):
    return BASE["actual_integer"](row, key, f"task {row.get('task_id', '?')}")


def unique(records, key):
    result = {r[key]: r for r in records}
    require(len(result) == len(records), f"duplicate {key}: unsupported ambiguous historical identity")
    return result


def summary(values):
    values = sorted(values)
    result = STAGING["distribution"](values)
    x = (len(values) - 1) * .95
    result.update(mean=sum(values) / len(values) if values else None,
                  p95=(values[math.floor(x)] + (values[math.ceil(x)] - values[math.floor(x)]) *
                       (x - math.floor(x))) if values else None)
    return result


def dependency_join(recovery):
    """Use recovery acceptance/receiver timestamps, not physical prefetch completion."""
    received = stamp(recovery, "input_received_time_ns")
    state = stamp(recovery, "state_ready_time_ns")
    compute = stamp(recovery, "recovery_compute_start_time_ns")
    if received is None or state is None or compute is None:
        return dict(dependency_join_known=False, input_critical_wait_ns=None,
                    was_input_on_critical_path=None, input_dependency_ready_time_ns=received)
    require(compute == max(received, state), "recovery compute disagrees with dependency barrier")
    return dict(dependency_join_known=True, input_critical_wait_ns=max(0, received - state),
                was_input_on_critical_path=received > state, input_dependency_ready_time_ns=received)


def classify(used, did_fault, same_target, critical):
    """Exclusive accounting priority, NOT additive causal attribution."""
    if used:
        return "USED_UNKNOWN" if critical is None else "USED_CRITICAL" if critical else "USED_NON_CRITICAL"
    if not did_fault:
        return "NO_FAULT"
    if same_target is False:
        return "WRONG_TARGET"
    return "FAILED_CANCELLED"


def lifecycle_row(life, task, recovery, flows, decisions):
    total, before, after, used_bytes, unused = (actual(life, key) for key in (
        "prefetch_total_sent_bytes", "prefetch_before_fault_sent_bytes", "prefetch_after_fault_sent_bytes",
        "prefetch_used_bytes", "prefetch_unused_bytes"))
    size = actual(life, "input_bytes")
    require(total == before + after == used_bytes + unused and total <= size, "prefetch bytes not conserved")
    require(size == actual(task, "input_bytes") and life["source_node"] == task["source_node_id"],
            "lifecycle input identity mismatch")
    used = life["used"] == "1"
    require(used_bytes == (total if used else 0), "used byte flag disagrees with actual lifecycle")
    requested, registered, ready, fault, released = (stamp(life, k) for k in (
        "requested_ns", "registered_ns", "ready_ns", "fault_ns", "released_ns"))
    require(requested is not None and released is not None, "unsettled historical lifecycle")
    established = registered is not None
    local = life["source_node"] == life["holder_node"]
    require(life["delivery_mode"] == ("LOCAL" if local else "NETWORK"), "local delivery mode mismatch")
    own_flows = [f for f in flows if f["kind"] == "PREFETCH_INPUT"]
    require(len(own_flows) == int(established and not local), "missing/duplicate prefetch flow or local pseudo UDP")
    if local or not established:
        require(total == 0 and life["input_transfer_id"] == "0", "local/unadmitted lifecycle sent network bytes")
    if established:
        require(life["input_object_id"] != "0" and requested <= registered <= released,
                "invalid established object lifetime")
    if own_flows:
        flow = own_flows[0]
        require((flow["transfer_id"], flow["storage_object_id"], flow["source_node"], flow["destination_node"]) ==
                (life["input_transfer_id"], life["input_object_id"], life["source_node"], life["holder_node"]),
                "prefetch physical flow/object identity mismatch")
        require(actual(flow, "bytes") == size and actual(flow, "sent_bytes") == total,
                "physical/lifecycle bytes disagree")
        require(flow["state"] in ("COMPLETED", "FAILED", "CANCELLED"), "unsettled prefetch flow")
        if ready is not None:
            require(flow["state"] == "COMPLETED" and actual(flow, "received_bytes") == size and
                    stamp(flow, "received_time_ns") == ready, "READY without complete receiver evidence")
    if ready is not None:
        require(established and registered <= ready <= released, "invalid READY lifetime")
    r = recovery or {}
    did_fault = recovery is not None
    actual_fault = stamp(r, "fault_time_ns")
    require(fault is None or fault == actual_fault, "lifecycle/recovery fault timestamp mismatch")
    if actual_fault is None:
        require(after == 0, "postfault bytes without recovery fault")
    else:
        require(requested < actual_fault, "prefetch first requested after fault")
        if own_flows:
            last_sent = stamp(own_flows[0], "sender_finished_time_ns")
            if last_sent is not None and last_sent < actual_fault:
                require(after == 0, "sender finished before fault but postfault bytes recorded")
    target = stamp(r, "recovery_node")
    same = (int(life["holder_node"]) == target) if target is not None else None
    reused = r.get("input_reused") == "1"
    if reused:
        require(same and life["adopted"] == "1" and
                r["reused_input_object_id"] == life["input_object_id"] and
                r["reused_input_transfer_id"] == life["input_transfer_id"], "reused INPUT identity mismatch")
        require(not any(f["kind"] == "RECOVERY_INPUT" for f in flows), "reused INPUT duplicated by recovery flow")
    join = dependency_join(r)
    compute = stamp(r, "recovery_compute_start_time_ns")
    require(used == bool(reused and compute is not None), "adoption mistaken for actual recovery consumption")
    if used:
        require(ready is not None and ready <= compute, "used INPUT has no receiver READY evidence")
        accepted = stamp(r, "recovery_accept_time_ns")
        require(accepted is not None, "used INPUT without recovery acceptance")
        if join["input_dependency_ready_time_ns"] is not None:
            require(join["input_dependency_ready_time_ns"] == max(accepted, ready),
                    "reuse dependency differs from real receiver/acceptance")
    state_at_fault = r.get("input_state_at_fault")
    if state_at_fault in ("READY", "IN_FLIGHT"):
        require((r["input_object_at_fault"], r["input_transfer_at_fault"]) ==
                (life["input_object_id"], life["input_transfer_id"]), "fault snapshot object/flow mismatch")
        require(registered is not None and registered < actual_fault and released >= actual_fault,
                "fault snapshot outside active lifecycle")
        if state_at_fault == "READY":
            require(ready is not None and ready < actual_fault, "READY must precede fault strictly")
        else:
            require(ready is None or ready >= actual_fault, "IN_FLIGHT contradicts receiver READY")
    admission = [d for d in decisions if d["admitted"] == "1" and
                 stamp(d, "time_ns") == requested and d["input_object_id"] == life["input_object_id"]]
    require(len(admission) == 1, "lifecycle request lacks unique decision provenance")
    decision = admission[0]
    require(decision["trigger"] in ("INITIALIZATION_COMMITTED", "FAULT_EPOCH_SURVIVED", "CAPACITY_RELEASE"),
            "unexpected historical JIT trigger")
    start, work, rate = stamp(task, "compute_start_time_ns"), actual(task, "compute_work_units"), actual(
        task, "compute_rate_work_units_per_second")
    require(start is not None and requested >= start and rate > 0, "missing primary execution input")
    remaining = max(0, work - (requested - start) * rate // NS) / rate
    p_on, transfer_s = float(decision["p_on"]), float(decision["input_transfer_s"])
    require(math.isfinite(p_on) and 0 <= p_on <= 1 and math.isfinite(transfer_s) and transfer_s >= 0,
            "invalid recorded prediction")
    return dict(task_id=int(task["task_id"]), task_profile=task["task_profile"],
        prefetch_target=int(life["holder_node"]), prefetch_request_time_ns=requested,
        prefetch_register_time_ns=registered, prefetch_complete_time_ns=ready, input_bytes=size,
        prefetch_sent_bytes_before_fault=before, prefetch_sent_bytes_after_fault=after,
        prefetch_total_sent_bytes=total, prefetch_used_bytes=used_bytes, prefetch_unused_bytes=unused,
        delivery_mode=life["delivery_mode"], input_object_id=int(life["input_object_id"]),
        input_transfer_id=int(life["input_transfer_id"]), did_fault=did_fault, fault_time_ns=actual_fault,
        actual_recovery_target=target, same_recovery_target=same, input_state_at_fault=state_at_fault,
        input_ready_before_fault=(ready is not None and ready < actual_fault) if did_fault else None,
        input_ready_time_ns=ready, stream_reused=bool(reused and r.get("input_state_at_acceptance") == "IN_FLIGHT"
                                                  and not local),
        object_reused=reused, actually_used=used, adopted=life["adopted"] == "1",
        chosen_recovery_path=r.get("chosen_path"), relocation_attempted=r.get("checkpoint_relocation_attempted") == "1",
        state_ready_time_ns=stamp(r, "state_ready_time_ns"), recovery_compute_start_time_ns=compute, **join,
        task_completed=task["task_success"] == "1", terminal_reason=life["terminal_reason"],
        task_terminal_reason=task["failure_reason"], recovery_terminal_reason=r.get("terminal_reason"),
        category=classify(used, did_fault, same, join["was_input_on_critical_path"]),
        flow_failed_or_cancelled=bool(own_flows and own_flows[0]["state"] in ("FAILED", "CANCELLED")),
        lifecycle_failed=life["failed"] == "1", lifecycle_established=established,
        fraction_sent_before_fault=before / size if did_fault and size else None,
        trigger=decision["trigger"], p_on=p_on, input_transfer_s=transfer_s,
        # Model expectation may contain sub-ns decimals; preserve its original text.
        representative_fault_time_ns=decision["representative_fault_time_ns"],
        next_jit_evaluation_ns=stamp(decision, "next_jit_evaluation_ns"), remaining_primary_compute_s=remaining)


def fairness(v7, deferred, vtasks, dtasks):
    a, b = (json.loads((p / "execution.json").read_text()) for p in (v7, deferred))
    require(a["commit"] == b["commit"], "historical execution commits differ")
    for p, e, mode in ((v7, a, "jit"), (deferred, b, "deferred")):
        require((e["seed"], e["run"], e["simulation_duration_s"], e["input_staging_policy"],
                 e["protection_mode"], e["placement_mode"], e["remote_busy_recovery_policy"]) ==
                (1, 11, 1300, mode, "compfrr", "fa-lrl", "relocate"), "not the historical run11 cohort")
        require(not e["worktree_dirty"] and not e["audit"] and not e["shadow"] and e["fault_mode"] == "generate"
                and json.loads((p / "execution-result.json").read_text())["returncode"] == 0,
                "unfinished or nonformal historical execution")
    ignored = {"--outputDir", "--faultTrace", "--inputStagingPolicy", "--jitStartBenefit"}
    def controls(e):
        pairs = [arg.split("=", 1) for arg in shlex.split(e["command"][-1])[1:]]
        require(all(len(pair) == 2 for pair in pairs), "ambiguous command controls")
        require(len(dict(pairs)) == len(pairs), "duplicate command control")
        return {k: v for k, v in pairs if k not in ignored}
    require(controls(a) == controls(b), "nonpolicy execution controls differ")
    fields = ("source_node_id", "compute_node_id", "result_node_id", "input_bytes", "output_bytes",
              "compute_work_units", "compute_rate_work_units_per_second", "arrival_time_ns", "task_profile",
              "compute_deadline_budget_ns")
    require(vtasks.keys() == dtasks.keys(), "task cohort differs")
    require(all(all(vtasks[t][f] == dtasks[t][f] for f in fields) for t in vtasks), "logical workload differs")
    return dict(execution_commit=a["commit"], v7_execution=a, deferred_execution=b,
                same_nonpolicy_controls=True, same_logical_workload=True,
                policy_control_exceptions=sorted(ignored),
                note="Historical V7 includes its JIT START benefit; not corrected N5R performance or a pure timing ablation.")


def recovery_input_check(task, recovery, flows):
    """Non-reused INPUT still uses real cross-node flow or logical LocalDelivery."""
    input_flows = [f for f in flows if f["kind"] == "RECOVERY_INPUT"]
    accepted = stamp(recovery, "recovery_accept_time_ns")
    if accepted is None:
        require(not input_flows, "unaccepted recovery sent INPUT")
        return
    if recovery["input_reused"] == "1":
        require(not input_flows, "reused prefetch duplicated by recovery INPUT")
        return
    local = task["source_node_id"] == recovery["recovery_node"]
    require(recovery["input_delivery_mode"] == ("LOCAL" if local else "NETWORK") and
            len(input_flows) == int(not local), "recovery LocalDelivery/physical INPUT mismatch")
    if input_flows:
        flow = input_flows[0]
        require((flow["source_node"], flow["destination_node"], flow["bytes"]) ==
                (task["source_node_id"], recovery["recovery_node"], task["input_bytes"]),
                "recovery INPUT not original full input to actual target")
        received = stamp(recovery, "input_received_time_ns")
        if received is not None:
            require(flow["state"] == "COMPLETED" and actual(flow, "received_bytes") == actual(task, "input_bytes") and
                    stamp(flow, "received_time_ns") == received, "recovery INPUT readiness lacks receiver evidence")


def paired_comparison(vrows, drows):
    a, b = unique(vrows, "task_id"), unique(drows, "task_id")
    pairs, rejected = [], []
    fields = ("fault_time_ns", "fault_type", "recovery_node", "chosen_path", "original_deadline_ns")
    for task_id in sorted(a.keys() | b.keys(), key=int):
        x, y = a.get(task_id), b.get(task_id)
        reasons = ["missing_task_recovery"] if x is None or y is None else [f for f in fields if x[f] != y[f]]
        if not reasons and (stamp(x, "actual_T_catch_ns") is None or stamp(y, "actual_T_catch_ns") is None or
                            x["logical_completion"] != "1" or y["logical_completion"] != "1"):
            reasons.append("missing_successful_actual_catch")
        if reasons:
            rejected.append(dict(task_id=int(task_id), reasons=reasons))
        else:
            v, d = int(x["actual_T_catch_ns"]), int(y["actual_T_catch_ns"])
            pairs.append(dict(task_id=int(task_id), fault_time_ns=int(x["fault_time_ns"]),
                recovery_target=int(x["recovery_node"]), recovery_path=x["chosen_path"],
                v7_catch_ns=v, deferred_catch_ns=d, delta_catch_paired_ns=d - v))
    return dict(count=len(pairs), pairs=pairs, rejected=rejected,
                rejection_reasons=dict(Counter(r for x in rejected for r in x["reasons"])),
                delta_catch_paired_ns=summary(p["delta_catch_paired_ns"] for p in pairs),
                note="Descriptive matched contrast, NOT an exact no-prefetch counterfactual or isolated causal effect.")


def analyze(v7, deferred):
    task_rows = rows(v7, "task-summary.csv")
    tasks = unique(task_rows, "task_id")
    dtasks = unique(rows(deferred, "task-summary.csv"), "task_id")
    identity = fairness(v7, deferred, tasks, dtasks)
    recovery_rows = rows(v7, "recovery-summary.csv")
    recoveries = unique(recovery_rows, "task_id")
    life_rows = rows(v7, "input-staging-lifecycles.csv")
    unique(life_rows, "task_id")
    flows = rows(v7, "protection-transfers.csv")
    unique(flows, "transfer_id")
    decisions = rows(v7, "jit-input-decisions.csv")
    flow_by_task, decision_by_task = defaultdict(list), defaultdict(list)
    for f in flows:
        flow_by_task[f["task_id"]].append(f)
    for d in decisions:
        decision_by_task[d["task_id"]].append(d)
    lifecycles = [lifecycle_row(l, tasks[l["task_id"]], recoveries.get(l["task_id"]),
        flow_by_task[l["task_id"]], decision_by_task[l["task_id"]]) for l in life_rows]
    require(sum(x["lifecycle_established"] and x["delivery_mode"] == "NETWORK" for x in lifecycles) ==
            sum(f["kind"] == "PREFETCH_INPUT" for f in flows), "orphan physical prefetch flow")
    require(sum(d["admitted"] == "1" for d in decisions) == len(lifecycles), "admitted decision lacks lifecycle")
    by_task = {str(x["task_id"]): x for x in lifecycles}
    fault_tasks = []
    for r in recovery_rows:
        task_id = r["task_id"]
        recovery_input_check(tasks[task_id], r, flow_by_task[task_id])
        l = by_task.get(task_id)
        require(r["input_state_at_fault"] in ("READY", "IN_FLIGHT", "ABSENT"), "unknown INPUT state")
        if not l:
            require(r["input_state_at_fault"] == "ABSENT" and r["input_reused"] == "0",
                    "fault snapshot/reuse lacks lifecycle")
        join = dependency_join(r)
        accepted = stamp(r, "recovery_accept_time_ns")
        state = stamp(r, "state_ready_time_ns")
        received = stamp(r, "input_received_time_ns")
        last = [d for d in decision_by_task[task_id] if int(d["time_ns"]) < int(r["fault_time_ns"])]
        last = max(last, key=lambda d: int(d["time_ns"])) if last else None
        fault_tasks.append(dict(task_id=int(task_id), task_profile=tasks[task_id]["task_profile"],
            input_state_at_fault=r["input_state_at_fault"], input_bytes=actual(tasks[task_id], "input_bytes"),
            bytes_before_fault=l["prefetch_sent_bytes_before_fault"] if l else 0,
            bytes_after_fault=l["prefetch_sent_bytes_after_fault"] if l else 0,
            actually_used=l["actually_used"] if l else False, stream_reused=l["stream_reused"] if l else False,
            **join, state_dependency_wait_ns=state - accepted if state is not None and accepted is not None else None,
            input_dependency_wait_ns=received - accepted if received is not None and accepted is not None else None,
            actual_catch_ns=stamp(r, "actual_T_catch_ns"), chosen_recovery_path=r["chosen_path"],
            recovery_target=stamp(r, "recovery_node"), fault_time_ns=int(r["fault_time_ns"]),
            task_completed=tasks[task_id]["task_success"] == "1", terminal_reason=r["terminal_reason"],
            prefetch_lifecycle_present=l is not None,
            last_jit_decision_time_ns=stamp(last or {}, "time_ns"),
            last_jit_decision_reason=last["jit_reason"] if last else "NO_PREFAULT_JIT_DECISION",
            last_jit_admission_reason=last["admission_reason"] if last else None))
    network = BASE["physical_network"](v7, task_rows, extra_kinds=("PREFETCH_INPUT",))
    dnetwork = BASE["physical_network"](deferred, list(dtasks.values()))
    total = sum(x["prefetch_total_sent_bytes"] for x in lifecycles)
    require(network["by_kind"]["PREFETCH_INPUT"]["sent_bytes"] == total, "physical prefetch total disagrees")
    categories = ("NO_FAULT", "WRONG_TARGET", "USED_CRITICAL", "USED_NON_CRITICAL", "FAILED_CANCELLED")
    if any(x["category"] == "USED_UNKNOWN" for x in lifecycles):
        categories += ("USED_UNKNOWN",)
    conservation = []
    for category in categories:
        members = [x for x in lifecycles if x["category"] == category]
        amount = sum(x["prefetch_total_sent_bytes"] for x in members)
        conservation.append(dict(category=category, lifecycle_count=len(members), bytes=amount,
                                 GB=amount / 1e9, fraction_of_prefetch=amount / total if total else None))
    require(sum(x["bytes"] for x in conservation) == total, "exclusive categories do not conserve bytes")
    outcomes = []
    for state in ("READY", "IN_FLIGHT", "ABSENT"):
        members = [x for x in fault_tasks if x["input_state_at_fault"] == state]
        waits = summary(x["input_critical_wait_ns"] for x in members if x["dependency_join_known"])
        outcomes.append(dict(input_state_at_fault=state, task_count=len(members),
            bytes_before_fault=sum(x["bytes_before_fault"] for x in members),
            reused_count=sum(x["actually_used"] for x in members),
            critical_count=sum(x["was_input_on_critical_path"] is True for x in members),
            unknown_join_count=sum(not x["dependency_join_known"] for x in members),
            critical_wait_mean_ns=waits["mean"], critical_wait_p95_ns=waits["p95"],
            critical_wait_sum_ns=waits["sum"]))
    pools = rows(v7, "protection-node-storage-summary.csv")
    require(all(actual(p, "final_used_bytes") == actual(p, "final_reserved_bytes") == 0 for p in pools),
            "unreleased final storage")
    paired = paired_comparison(recovery_rows, rows(deferred, "recovery-summary.csv"))
    return dict(status="PASS" if all(x["dependency_join_known"] for x in fault_tasks) else "INCOMPLETE_DEPENDENCY_EVIDENCE",
        identity=identity, raw_roots=dict(v7=str(v7), deferred=str(deferred)),
        lifecycle_count=len(lifecycles), fault_task_count=len(fault_tasks),
        total_prefetch_sent_bytes=total, prefetch_before_fault_sent_bytes=sum(x["prefetch_sent_bytes_before_fault"] for x in lifecycles),
        prefetch_after_fault_sent_bytes=sum(x["prefetch_sent_bytes_after_fault"] for x in lifecycles),
        used_bytes=sum(x["prefetch_used_bytes"] for x in lifecycles), unused_bytes=sum(x["prefetch_unused_bytes"] for x in lifecycles),
        local_lifecycle_count=sum(x["delivery_mode"] == "LOCAL" for x in lifecycles),
        flow_failed_cancelled_count=sum(x["flow_failed_or_cancelled"] for x in lifecycles),
        admission_reasons=dict(Counter(d["admission_reason"] for d in decisions if d["admitted"] != "1")),
        absent_last_jit_reasons=dict(Counter(x["last_jit_decision_reason"] for x in fault_tasks if x["input_state_at_fault"] == "ABSENT")),
        completed=sum(t["task_success"] == "1" for t in task_rows), deferred_completed=sum(t["task_success"] == "1" for t in dtasks.values()),
        network=network, deferred_network=dnetwork, byte_conservation=conservation, fault_outcomes=outcomes,
        v7_actual_catch_ns=summary(x["actual_catch_ns"] for x in fault_tasks if x["actual_catch_ns"] is not None),
        paired_comparison=paired, lifecycles=lifecycles, fault_tasks=fault_tasks,
        limitations=["Historical V7, not corrected N5R. No new simulation or production code used.",
            "Used/non-critical means no extra INPUT wait at actual recovery; early prefetch may have caused that benefit.",
            "Sent fraction is not receiver READY; READY cutoff strictly precedes fault.",
            "Category priority is USED, NO_FAULT, WRONG_TARGET, residual FAILED_CANCELLED; flags may overlap.",
            "Unknown dependency evidence is null, never an inferred zero. Empty IN_FLIGHT cohort proves no general timing guarantee.",
            "Paired differences include other causal policy/trajectory effects; no exact counterfactual is constructed."])


def write_csv(path, records):
    if not records:
        return
    with path.open("x", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)


def output_guard(output, roots):
    output = output.resolve()
    require(not output.exists() and not any(output.is_relative_to(p.resolve()) or p.resolve().is_relative_to(output)
                                           for p in roots), "refusing to overwrite or nest output with raw evidence")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--v7", type=Path, required=True)
    parser.add_argument("--deferred", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    v7, deferred, output = (p.resolve() for p in (args.v7, args.deferred, args.output_dir))
    output_guard(output, (v7, deferred))
    result = analyze(v7, deferred)
    output.mkdir(parents=True, exist_ok=False)
    for name, data in (("v7-prefetch-lifecycle", result["lifecycles"]), ("v7-fault-tasks", result["fault_tasks"]),
                       ("v7-byte-conservation", result["byte_conservation"]), ("v7-fault-outcomes", result["fault_outcomes"]),
                       ("v7-deferred-paired-catch", result["paired_comparison"]["pairs"])):
        write_csv(output / (name + ".csv"), data)
    (output / "v7-offline-audit.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k not in
                      ("lifecycles", "fault_tasks", "identity", "network", "deferred_network", "paired_comparison")}, indent=2))
    return 0 if result["status"] == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
