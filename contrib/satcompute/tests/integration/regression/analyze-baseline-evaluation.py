#!/usr/bin/env python3
"""Pre-N5C engineering comparison: causal ledgers only, no simulation or parameter tuning."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import runpy
import shlex

ACCOUNTING = runpy.run_path(str(Path(__file__).with_name("analyze-protection-accounting.py")))
rows, require = ACCOUNTING["rows"], ACCOUNTING["require"]
PROFILES = ACCOUNTING["PROFILES"]
GROUPS = {
    "R0-recompute-ffp": ("recompute", "relocate"),
    "R1-one-plus-one-ffp": ("one-plus-one", "relocate"),
    "R2-fixed-ffp-recompute-busy": ("fixed", "recompute"),
    "R3-fixed-ffp-relocate-busy": ("fixed", "relocate"),
    "R4-compfrr-ffp-recompute-busy": ("compfrr", "recompute"),
    "R5-compfrr-ffp-relocate-busy": ("compfrr", "relocate"),
}
NS = 10**9


def number(row, key):
    return int(row.get(key) or 0)


def stats(values):
    values = sorted(values)
    def quantile(p):
        if not values:
            return None
        x = (len(values)-1)*p
        a, b = math.floor(x), math.ceil(x)
        return values[a] + (values[b]-values[a])*(x-a)
    return dict(count=len(values), sum=sum(values), p50=quantile(.5), p90=quantile(.9),
                max=max(values, default=None))


def strict_equivalence(reference, candidate):
    """All frozen CSV bytes, six state JSONs; only two wall-clock fields are excluded."""
    checks = {}
    for path in sorted(reference.glob("*.csv")):
        other = candidate / path.name
        checks[path.name] = other.is_file() and path.read_bytes() == other.read_bytes()
    require(checks and "recovery-summary.csv" in checks, "missing frozen R5 CSV evidence")
    for name in (*ACCOUNTING["BUSINESS_JSON"], "protection-finalization.json"):
        if not (candidate / name).is_file():
            checks[name] = False
            continue
        a, b = (json.loads((p / name).read_text()) for p in (reference, candidate))
        if name == "run-summary.json":
            for key in ("wall_clock_ns", "wall_clock_s"):
                a.pop(key, None)
                b.pop(key, None)
        checks[name] = a == b
    return dict(reference=str(reference), candidate=str(candidate), checks=checks,
                csv_byte_identical_count=sum(v for k, v in checks.items() if k.endswith(".csv")),
                json_equivalent_count=sum(v for k, v in checks.items() if k.endswith(".json")),
                ignored_fields={"run-summary.json": ["wall_clock_ns", "wall_clock_s"]},
                passed=all(checks.values()))


def physical_network(root, tasks):
    """Union by real transfer ID; losing primary RESULT is not in checkpoint flow CSV."""
    flows = {}
    input_ids = {t["input_transfer_id"] for t in tasks}
    def add(identity, kind, sent, received, declared, business, terminal):
        require(terminal in ("COMPLETED", "FAILED", "CANCELLED"), "unsettled physical flow")
        value = dict(kind=kind, sent_bytes=int(sent), received_bytes=int(received),
                     declared_bytes=int(declared), business=business)
        if identity in flows:
            require(all(flows[identity][k] == value[k] for k in
                        ("sent_bytes", "received_bytes", "declared_bytes", "business")),
                    "duplicate flow evidence disagrees")
        flows[identity] = value
    for r in rows(root, "transfer-summary.csv"):
        add(r["transfer_id"], "INPUT" if r["transfer_id"] in input_ids else "RESULT",
            r["sent_application_bytes"], r["received_application_bytes"], r["declared_size_bytes"], True, r["terminal_state"])
    for r in rows(root, "protection-transfers.csv", True):
        add(r["transfer_id"], r["kind"], r["sent_bytes"], r["received_bytes"], r["bytes"], False, r["state"])
    for r in rows(root, "replica-transfers.csv", True):
        add(r["transfer_id"], "RESULT" if r["kind"] == "PRIMARY_RESULT" else r["kind"],
            r["sent_bytes"], r["received_bytes"], r["declared_bytes"], r["business_result"] == "1", r["state"])
    by_kind = {k: dict(flows=0, declared_bytes=0, sent_bytes=0, received_bytes=0) for k in
               ("INPUT", "RESULT", "INIT_BASE", "INIT_STATE", "L1", "REMOTE_BATCH", "RECOVERY_INPUT",
                "RECOVERY_STATE", "RECOVERY_TAIL", "REPLICA_INPUT", "REPLICA_RESULT")}
    for f in flows.values():
        total = by_kind[f["kind"]]
        total["flows"] += 1
        for key in ("declared_bytes", "sent_bytes", "received_bytes"):
            total[key] += f[key]
    business = sum(f["sent_bytes"] for f in flows.values() if f["business"])
    extra = sum(f["sent_bytes"] for f in flows.values() if not f["business"])
    return dict(by_kind=by_kind, business_sent_bytes=business, extra_sent_bytes=extra,
                total_physical_application_sent_bytes=business+extra, physical_flow_count=len(flows),
                note="Physical application payload, not hop-summed wire bytes; each flow counted once.")


def planned_wait(r):
    if r.get("planned_input_wait_ns"):
        return number(r, "planned_input_wait_ns")
    key = {"RECOMPUTE": "estimated_recompute_ns", "TAIL": "estimated_tail_ns",
           "MIGRATE_TAIL": "estimated_migrate_tail_ns", "MIGRATE_REDO": "estimated_migrate_redo_ns"}.get(r.get("chosen_path"))
    if r.get("chosen_path") == "REMOTE_REDO" and r.get("recovery_accept_time_ns"):
        return 0
    rate = number(r, "recovery_rate_wu_per_s")
    if key and r.get(key) and rate:
        work = number(r, "planned_catchup_redo_wu")
        return max(0, number(r, key) - (work*NS + rate-1)//rate)
    return None


def replica_check(r, attempts):
    total = number(r, "primary_executed_wu") + number(r, "replica_executed_wu")
    require(total == number(r, "total_executed_wu"), "replica WU partition")
    success = r["terminal_state"] == "COMPLETED"
    require(number(r, "redundant_actual_wu") == (total-number(r, "total_work_units") if success else 0),
            "replica redundant WU or failed negative charge")
    require(number(r, "failed_raw_executed_wu") == (0 if success else total), "failed raw execution missing")
    if success:
        ACCOUNTING["close"](float(r["w_waste_actual"]), number(r, "redundant_actual_wu") +
                            float(r["replica_reserved_idle_eq_wu"]), "replica waste")
    for a in attempts:
        require(number(a, "actual_work_units") == min(number(r, "total_work_units"),
            number(a, "actual_service_ns")*number(a, "rate_wu_per_s")//NS), "actual attempt WU not service-based")
        require(a["stage"] in ("COMPLETED", "FAILED", "CANCELLED"), "active replica attempt")
        if a["attempt_generation"] == "1":
            end = a["compute_start_time_ns"] or a["terminal_time_ns"]
            require(number(a, "reserved_idle_ns") == int(end)-number(a, "reserved_time_ns"), "replica reserved wait")
            require(bool(a["takeover_time_ns"]) == (a["compute_fault_immune"] == "1"), "early replica immunity")


def busy_summary(recoveries, transfers):
    busy = [r for r in recoveries if r["phase_at_fault"] == "ON" and r["checkpoint_state_exists"] == "1" and
            r["remote_busy_at_fault"] == "1" and (r["checkpoint_fallback_reason"] == "REMOTE_BUSY" or
                                                  r["checkpoint_relocation_trigger"] == "REMOTE_BUSY")]
    ids = {r["task_id"] for r in busy}
    return dict(events=len(busy), valid_checkpoint_events=len(busy), task_ids=sorted(ids, key=int),
        paths=dict(Counter(r["chosen_path"] for r in busy)),
        completed=sum(r["terminal_state"] == "COMPLETED" for r in busy),
        failed=sum(r["terminal_state"] == "FAILED" for r in busy),
        T_catch_s=stats([number(r, "actual_T_catch_ns")/NS for r in busy if r["actual_T_catch_ns"]]),
        no_observed_catchup=sum(not r["actual_T_catch_ns"] for r in busy),
        actual_catchup_wu=sum(number(r, "actual_catchup_redo_wu") for r in busy),
        reserved_idle_eq_wu=sum(float(r["recovery_reserved_idle_eq_wu"]) for r in busy),
        input_state_tail_sent_bytes={kind: sum(number(f, "sent_bytes") for f in transfers
            if f["task_id"] in ids and f["kind"] == kind) for kind in ("RECOVERY_INPUT", "RECOVERY_STATE", "RECOVERY_TAIL")},
        rows=busy)


def analyze(root):
    identity = json.loads((root / "execution.json").read_text())
    execution = json.loads((root / "execution-result.json").read_text())
    tasks = rows(root, "task-summary.csv")
    require(len(tasks) == 800 and all(t["final_state"] in ("COMPLETED", "FAILED") for t in tasks), "formal tasks not terminal")
    terminals = Counter(e["task_id"] for e in rows(root, "task-events.csv") if e["to_state"] in ("COMPLETED", "FAILED"))
    require(terminals == Counter({t["task_id"]: 1 for t in tasks}), "logical terminal count")
    protected = {r["task_id"]: r for r in rows(root, "protection-task-summary.csv", True)}
    recovery = rows(root, "recovery-summary.csv", True)
    recovery_by_id = {r["task_id"]: r for r in recovery}
    replicas = {r["task_id"]: r for r in rows(root, "replica-summary.csv", True)}
    attempts = rows(root, "replica-attempts.csv", True)
    is_replica = identity["protection_mode"] == "one-plus-one"
    if not is_replica:
        ACCOUNTING["analyze"](root)  # Existing event-based costs, relocation bytes and service conservation.
    else:
        require(not protected and not recovery, "replica created checkpoint/recompute pseudo-state")
        for task_id, r in replicas.items():
            replica_check(r, [a for a in attempts if a["task_id"] == task_id])
    task_rows = []
    for t in tasks:
        task_id = t["task_id"]
        p, r, b = protected.get(task_id, {}), recovery_by_id.get(task_id, {}), replicas.get(task_id, {})
        normal = float(p.get("normal_protection_eq_wu") or 0)
        idle = float((b.get("replica_reserved_idle_eq_wu") if is_replica else r.get("recovery_reserved_idle_eq_wu")) or 0)
        catch = number(r, "actual_catchup_redo_wu")
        redundant = number(b, "redundant_actual_wu")
        wait = planned_wait(b if is_replica else r)
        planned_idle = (float(b["planned_reserved_idle_eq_wu"]) if b.get("planned_reserved_idle_eq_wu") else None) if is_replica else (
            None if wait is None else wait*number(r, "recovery_rate_wu_per_s")/NS)
        recovered = r.get("terminal_state") == "COMPLETED" if not is_replica else b.get("primary_faulted") == "1" and b.get("winner") == "replica"
        task_rows.append(dict(task_id=task_id, profile=t["task_profile"], completed=t["final_state"] == "COMPLETED",
            failed=t["final_state"] == "FAILED", on_time=t["final_state"] == "COMPLETED" and t["compute_deadline_met"] == "1",
            deadline_miss=t["failure_reason"] == "COMPUTE_DEADLINE_EXCEEDED", normal_protection_eq_wu=normal,
            replica_redundant_wu=redundant, replica_failed_raw_wu=number(b, "failed_raw_executed_wu"),
            planned_catchup_wu=number(r, "planned_catchup_redo_wu"), actual_catchup_wu=catch,
            reserved_idle_eq_wu=idle, planned_reserved_idle_eq_wu=planned_idle,
            actual_post_catchup_wu=number(r, "actual_post_catchup_wu"), actual_total_recovery_wu=number(r, "actual_total_recovery_wu"),
            w_waste_actual=normal+idle+catch+redundant,
            recovery_attempted=bool(r) if not is_replica else b.get("primary_faulted") == "1",
            recovery_accepted=bool(r.get("recovery_accept_time_ns")) if not is_replica else bool(b.get("takeover_time_ns")),
            recovery_success=recovered))
    def aggregate(values):
        keys = ("completed", "failed", "on_time", "deadline_miss", "normal_protection_eq_wu", "replica_redundant_wu",
                "replica_failed_raw_wu", "planned_catchup_wu", "actual_catchup_wu", "reserved_idle_eq_wu",
                "actual_post_catchup_wu", "actual_total_recovery_wu", "w_waste_actual", "recovery_attempted",
                "recovery_accepted", "recovery_success")
        totals = {k: sum(t[k] for t in values) for k in keys}
        totals.update(tasks=len(values), recovery_failure=totals["recovery_attempted"]-totals["recovery_success"])
        return totals
    network = physical_network(root, tasks)
    run = json.loads((root / "run-summary.json").read_text())
    require(network["business_sent_bytes"] == run["sent_application_bytes"], "business totals differ")
    # FlowMonitor tx bytes include the same actual payload plus IPv4/UDP headers once per source packet.
    flow = rows(root, "network-flow-metrics.csv")[0]
    require(number(flow, "tx_bytes")-28*number(flow, "tx_packets") == network["total_physical_application_sent_bytes"],
            "physical payload union differs from FlowMonitor")
    faults = json.loads((root / "fault-trace.json").read_text())["faults"]
    impacts = rows(root, "fault-task-impact.csv")
    direct = [r for r in impacts if "INTERRUPTED" in r["impact_type"]]
    pools = rows(root, "protection-node-storage-summary.csv")
    require(all(number(p, "final_used_bytes") == number(p, "final_reserved_bytes") == 0 for p in pools), "storage leak")
    final = json.loads((root / "protection-finalization.json").read_text())
    require(final["quiescent"], "runtime ownership leak")
    if identity["protection_mode"] in ("recompute", "one-plus-one"):
        require(not protected and all(number(p, "peak_total_bytes") == 0 for p in pools), "baseline allocated checkpoint state")
    capacity = json.loads((root / "capacity-aware-summary.json").read_text())
    require(all(value == 0 for key, value in capacity.items() if key.endswith("_at_end")), "network reservation leak")
    links = rows(root, "link-summary.csv")
    available = sum(float(r["available_time_s"]) for r in links)
    link_time = sum(float(r["measurement_duration_s"]) for r in links)
    return dict(execution=identity, execution_result=execution, summary=aggregate(task_rows),
        profiles={p: aggregate([t for t in task_rows if t["profile"] == p]) for p in PROFILES},
        fault_counts=dict(F1=sum(bool(f["f1_occurred"]) for f in faults), F2=sum(bool(f["f2_occurred"]) for f in faults),
            F3=sum(f["fault_type"] == "satellite" for f in faults), direct_victims=len({r["task_id"] for r in direct}),
            incidents=len({(r["task_id"], r["fault_id"]) for r in direct})),
        network=network, storage=dict(max_node_peak_bytes=max((number(p, "peak_total_bytes") for p in pools), default=0),
            local_peak_bytes=max((number(p, "local_peak_bytes") for p in protected.values()), default=0),
            remote_peak_bytes=max((number(p, "remote_peak_bytes") for p in protected.values()), default=0),
            allocation_failures=sum(number(p, "allocation_failures") for p in pools), quiescent=True,
            active_working_set_quantified=False),
        links=dict(mean_utilization_percent=100*sum(float(r["tx_busy_time_s"]) for r in links)/link_time,
            available_utilization_percent=100*sum(float(r["available_tx_busy_time_s"]) for r in links)/available,
            serialized_bits=sum(number(r, "serialized_bits") for r in links)), capacity=capacity,
        busy=busy_summary(recovery, rows(root, "protection-transfers.csv", True)),
        T_catch_s=stats([number(r, "actual_T_catch_ns")/NS for r in recovery if r["actual_T_catch_ns"]]),
        recovery_paths=dict(Counter(r["chosen_path"] for r in recovery)),
        task_failure_reasons=dict(Counter(t["failure_reason"] for t in tasks if t["final_state"] == "FAILED")),
        recovery_reasons=dict(Counter(r["terminal_reason"] for r in recovery)),
        replica=dict(requested=len(replicas), admitted=sum(r["replica_admitted"] == "1" for r in replicas.values()),
            winners=dict(Counter(r["winner"] or "none" for r in replicas.values())),
            admission_reasons=dict(Counter(r["admission_reason"] for r in replicas.values())),
            actual_primary_wu=sum(number(r, "primary_executed_wu") for r in replicas.values()),
            actual_replica_wu=sum(number(r, "replica_executed_wu") for r in replicas.values())),
        planned_reserved_idle_eq_wu=stats([t["planned_reserved_idle_eq_wu"] for t in task_rows if t["planned_reserved_idle_eq_wu"] is not None]),
        note="Single controlled seed/run. 1+1 catchup/recovery WU fields are N/A (zero placeholders); "
             "its waste includes successful-task redundant WU and all replica idle, with failed raw WU separately reported.",
        task_rows=task_rows, recovery_rows=recovery, replica_rows=list(replicas.values()))


def busy_comparison(before, after):
    """R4 minus R5, with causal fault identity required for per-event pairing."""
    a, b = before["busy"], after["busy"]
    def index(value):
        return {(r["task_id"], r["fault_time_ns"], r["fault_type"]): r for r in value["rows"]}
    x, y = index(a), index(b)
    paired = []
    for key in sorted(x.keys() & y.keys(), key=lambda k: (int(k[1]), int(k[0]))):
        first, second = x[key], y[key]
        paired.append(dict(task_id=key[0], fault_time_ns=key[1], fault_type=key[2],
            before_path=first["chosen_path"], after_path=second["chosen_path"],
            before_terminal=first["terminal_state"], after_terminal=second["terminal_state"],
            before_reason=first["terminal_reason"], after_reason=second["terminal_reason"],
            T_catch_difference_s=(number(first, "actual_T_catch_ns")-number(second, "actual_T_catch_ns"))/NS
                if first["actual_T_catch_ns"] and second["actual_T_catch_ns"] else None,
            catchup_wu_difference=number(first, "actual_catchup_redo_wu")-number(second, "actual_catchup_redo_wu"),
            idle_eq_wu_difference=float(first["recovery_reserved_idle_eq_wu"])-float(second["recovery_reserved_idle_eq_wu"])))
    return dict(direction="recompute-on-busy minus relocate-on-busy", matched_events=paired,
        before_unmatched=[list(k) for k in x.keys()-y.keys()], after_unmatched=[list(k) for k in y.keys()-x.keys()],
        actual_catchup_wu_difference=a["actual_catchup_wu"]-b["actual_catchup_wu"],
        reserved_idle_eq_wu_difference=a["reserved_idle_eq_wu"]-b["reserved_idle_eq_wu"],
        recovery_sent_bytes_difference={k: a["input_state_tail_sent_bytes"][k]-b["input_state_tail_sent_bytes"][k]
                                       for k in a["input_state_tail_sent_bytes"]})


def fairness(runs):
    controls = []
    for name, result in runs.items():
        e = result["execution"]
        scheme, busy = GROUPS[name]
        require((e["protection_mode"], e["remote_busy_recovery_policy"], e["placement_mode"]) == (scheme, busy, "ffp"), "group policy mismatch")
        require(e["fault_mode"] == "generate" and not e["audit"] and not e["shadow"] and not e["worktree_dirty"], "nonformal identity")
        require((e["seed"], e["run"], e["simulation_duration_s"], result["execution_result"]["returncode"]) == (1, 11, 1300, 0), "formal run incomplete")
        ignored = {"outputDir", "faultTrace", "protectionMode", "remoteBusyRecoveryPolicy"}
        controls.append({a.split("=", 1)[0]: a.split("=", 1)[1] for a in shlex.split(e["command"][-1])[1:]
                         if a.split("=", 1)[0].removeprefix("--") not in ignored})
    require(all(c == controls[0] for c in controls), "unpaired nonpolicy parameters")
    require(len({r["execution"]["commit"] for r in runs.values()}) == 1, "formal execution commits differ")
    return dict(same_code_and_controls=True, same_fault_trace_required=False, groups=len(runs))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.reference or args.candidate:
        if not (args.reference and args.candidate):
            parser.error("--reference and --candidate must be supplied together")
        result = strict_equivalence(args.reference, args.candidate)
    else:
        if args.root is None: parser.error("provide --root or --reference/--candidate")
        runs = {name: analyze(args.root / name) for name in GROUPS}
        result = dict(fairness=fairness(runs), runs=runs,
            R4_minus_R5=busy_comparison(runs["R4-compfrr-ffp-recompute-busy"], runs["R5-compfrr-ffp-relocate-busy"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    if result.get("passed") is False:
        raise SystemExit("STOP: R5 differs from frozen B: " + ", ".join(k for k, v in result["checks"].items() if not v))
    print(json.dumps({k: v for k, v in result.items() if k != "runs"}, indent=2))


if __name__ == "__main__":
    main()
