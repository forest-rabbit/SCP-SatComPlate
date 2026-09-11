#!/usr/bin/env python3
"""N5A execution/accounting validation; no simulator, fault generation or algorithm claims."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path

BUSINESS_CSV = ("task-summary.csv", "task-events.csv", "transfer-summary.csv",
                "compute-node-summary.csv", "fault-events.csv", "fault-task-impact.csv",
                "ecmp-route-events.csv", "size-aware-reservation-events.csv",
                "network-flow-metrics.csv", "network-flow-details.csv", "link-summary.csv",
                "link-window-metrics.csv", "network-link-window-metrics.csv")
BUSINESS_JSON = ("run-summary.json", "fault-trace.json", "fault-summary.json",
                 "capacity-aware-summary.json", "size-aware-summary.json")
PROFILES = ("dense-image", "sparse-inference", "compression", "llm")


def require(value, message):
    if not value:
        raise ValueError(message)


def rows(root, name, optional=False):
    if optional and not (root / name).exists():
        return []
    with (root / name).open() as stream:
        return list(csv.DictReader(stream))


def read(root, name):
    return json.loads((root / name).read_text())


def number(row, key):
    return int(row.get(key) or 0)


def close(a, b, label):
    require(math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-8), label)


def stats(values):
    values = sorted(values)
    def percentile(p):
        if not values:
            return None
        x = (len(values) - 1) * p
        lo, hi = math.floor(x), math.ceil(x)
        return values[lo] + (values[hi] - values[lo]) * (x - lo)
    return {"count": len(values), "sum": sum(values),
            "mean": sum(values) / len(values) if values else None,
            "p50": percentile(.5), "p90": percentile(.9)}


def compare(reference, candidate):
    """Only wall-clock fields are ignored; missing/different business output fails."""
    result = {}
    for name in BUSINESS_CSV:
        result[name] = (reference / name).read_bytes() == (candidate / name).read_bytes()
    for name in BUSINESS_JSON:
        a, b = read(reference, name), read(candidate, name)
        if name == "run-summary.json":
            for key in ("wall_clock_ns", "wall_clock_s"):
                a.pop(key, None)
                b.pop(key, None)
        result[name] = a == b
    require(all(result.values()), f"business equivalence failed: {[k for k,v in result.items() if not v]}")
    return {"files": result, "allowlist": {"run-summary.json": ["wall_clock_ns", "wall_clock_s"]},
            "excluded_non_business_outputs": "online model probability audit and shadow are not replayed"}


def recovery_check(row):
    n = lambda key: number(row, key)
    planned = n("planned_total_recovery_wu")
    actual = n("actual_total_recovery_wu")
    catch = n("actual_catchup_redo_wu")
    require(planned == n("planned_catchup_redo_wu") + n("planned_post_catchup_wu"), "planned partition")
    require(actual == catch + n("actual_post_catchup_wu") <= planned, "actual partition/bound")
    require(actual == min(planned, n("actual_recovery_service_ns") * n("recovery_rate_wu_per_s") // 10**9),
            "ComputeService integer work mismatch")
    if row["recovery_compute_complete_time_ns"]:
        require(actual == planned, "completed work missing")
    if row["catchup_time_ns"]:
        require(catch == n("planned_catchup_redo_wu"), "catchup work mismatch")
    else:
        require(n("actual_post_catchup_wu") == 0, "post work without catchup")
    if row["recovery_accept_time_ns"]:
        end = n("recovery_compute_start_time_ns") if row["recovery_compute_start_time_ns"] else n("terminal_time_ns")
        require(n("reserved_idle_ns") == end - n("recovery_accept_time_ns"), "reserved-idle boundaries")
    idle = n("reserved_idle_ns") * n("recovery_rate_wu_per_s") / 1e9
    normal = n("normal_protection_cost_ns") * n("primary_rate_wu_per_s") / 1e9
    close(float(row["normal_protection_eq_wu"]), normal, "normal WU")
    close(float(row["recovery_reserved_idle_eq_wu"]), idle, "idle WU")
    close(float(row["w_waste_actual"]), normal + idle + catch, "waste/cR double count")
    return {"planned": planned, "actual": actual, "execution_ratio": actual / planned if planned else None}


def relocation_check(task, protected, recovery, flows):
    """Check actual migrated bytes/receive barriers, not just a new path label."""
    if not recovery["chosen_path"].startswith("MIGRATE_"):
        return
    r = recovery
    work, remote = number(task, "compute_work_units"), number(r, "remote_work_units")
    size = number(task, "input_bytes")
    deferred = r.get("input_staging_policy") == "deferred"
    state = (remote // 100 * 114688 if task["task_profile"] == "llm" else
             (0 if deferred else size - size * remote // work) + number(protected, "variable_state_bytes") * remote // work)
    require(number(r, "checkpoint_state_bytes") == number(r, "checkpoint_relocation_bytes") == state,
            "relocation did not use exact committed state bytes")
    require(r["old_remote_node"] != r["new_recovery_node"] and
            (bool(r["input_start_time_ns"]) if deferred else not r["input_start_time_ns"]),
            "migration used original node or wrong INPUT staging policy")
    state_flows = [f for f in flows if f["kind"] == "RECOVERY_STATE"]
    require(len(state_flows) == int(state > 0), "missing/duplicate migration state flow")
    if state_flows:
        f = state_flows[0]
        require(number(f, "bytes") == state and f["source_node"] == r["old_remote_node"] and
                f["destination_node"] == r["new_recovery_node"], "migration flow ownership/bytes")
    if r["recovery_compute_start_time_ns"]:
        require(r["state_received_time_ns"] and number(r, "recovery_compute_start_time_ns") >=
                number(r, "state_received_time_ns"), "compute started before state received")
        if r["chosen_path"] == "MIGRATE_TAIL":
            require(r["tail_received_time_ns"] and number(r, "tail_commit_time_ns") ==
                    max(number(r, "state_received_time_ns"), number(r, "tail_received_time_ns")) +
                    number(protected, "cR_ns"), "migration omitted/doubled cR or receiver barrier")


def analyze(root):
    tasks = {r["task_id"]: r for r in rows(root, "task-summary.csv")}
    protected = {r["task_id"]: r for r in rows(root, "protection-task-summary.csv", True)}
    recoveries = {r["task_id"]: r for r in rows(root, "recovery-summary.csv", True)}
    events = rows(root, "protection-events.csv", True)
    costs = defaultdict(Counter)
    for e in events:
        if e["attempt_generation"] == "0":
            costs[e["task_id"]][e["event"]] += 1
    terminals = Counter(e["task_id"] for e in rows(root, "task-events.csv")
                        if e["to_state"] in ("COMPLETED", "FAILED"))
    require(all(t["final_state"] in ("COMPLETED", "FAILED") for t in tasks.values()), "nonterminal logical task")
    require(terminals == Counter({k: 1 for k in tasks}), "logical terminal duplication/missing")
    for r in recoveries.values():
        recovery_check(r)
    network = defaultdict(lambda: {"declared_bytes": 0, "sent_bytes": 0, "received_bytes": 0, "flows": 0})
    for kind in ("INIT_BASE", "INIT_STATE", "L1", "REMOTE_BATCH", "RECOVERY_INPUT", "RECOVERY_TAIL", "RECOVERY_STATE", "RECOVERY_RESULT_NETWORK"):
        network[kind]
    task_network = Counter()
    task_flows = defaultdict(list)
    for flow in rows(root, "protection-transfers.csv", True):
        task_flows[flow["task_id"]].append(flow)
        require(flow["state"] in ("COMPLETED", "FAILED", "CANCELLED"), "active protection transfer")
        total = network[flow["kind"]]
        for key, column in (("declared_bytes", "bytes"), ("sent_bytes", "sent_bytes"), ("received_bytes", "received_bytes")):
            total[key] += number(flow, column)
        total["flows"] += 1
        task_network[flow["task_id"]] += number(flow, "sent_bytes")
    transfers = {r["transfer_id"]: r for r in rows(root, "transfer-summary.csv")}
    for r in recoveries.values():
        relocation_check(tasks[r["task_id"]], protected.get(r["task_id"], {}), r, task_flows[r["task_id"]])
        if r["result_transfer_id"]:
            flow = transfers[r["result_transfer_id"]]
            require(flow["terminal_state"] in ("COMPLETED", "FAILED", "CANCELLED"), "active recovery result")
            total = network["RECOVERY_RESULT_NETWORK"]
            total["flows"] += 1
            total["declared_bytes"] += number(r, "result_bytes")
            total["sent_bytes"] += number(flow, "sent_application_bytes")
            total["received_bytes"] += number(flow, "received_application_bytes")
        if r["result_delivery_mode"] == "LOCAL":
            require(not r["result_transfer_id"], "local result created network flow")
    task_rows = []
    for task_id, t in tasks.items():
        p, r = protected.get(task_id, {}), recoveries.get(task_id, {})
        c = costs[task_id]
        normal_ns = ((c["INIT_STATE_GENERATED"] + c["L1_GENERATED"]) * number(p, "cL_ns") +
                     (c["INIT_COST_COMMITTED"] + c["REMOTE_COST_COMMITTED"]) * number(p, "cR_ns"))
        require(normal_ns == number(p, "normal_protection_cost_ns"), "event-based normal cost")
        normal = normal_ns * number(p, "primary_rate_wu_per_s") / 1e9
        if p:
            close(normal, float(p["normal_protection_eq_wu"]), "normal rate conversion")
        if r:
            close(normal, float(r["normal_protection_eq_wu"]), "normal cost differs between ledgers")
        idle = float(r.get("recovery_reserved_idle_eq_wu", 0))
        catch = number(r, "actual_catchup_redo_wu")
        task_rows.append({"task_id": task_id, "profile": t["task_profile"], "completed": t["final_state"] == "COMPLETED",
            "failed": t["final_state"] == "FAILED", "started": bool(p), "on_at_fault": r.get("phase_at_fault") == "ON",
            "initializing_at_fault": r.get("phase_at_fault") == "INITIALIZING", "recovery_attempted": bool(r),
            "recovery_accepted": bool(r.get("recovery_accept_time_ns")), "recovered": r.get("terminal_state") == "COMPLETED",
            "recovery_failed": r.get("terminal_state") == "FAILED", "deadline_met": t["compute_deadline_met"] == "1",
            "deadline_missed": t["failure_reason"] == "COMPUTE_DEADLINE_EXCEEDED", "path": r.get("chosen_path", ""),
            "outcome": r.get("terminal_state", ""), "normal_protection_eq_wu": normal, "recovery_reserved_idle_eq_wu": idle,
            "recovery_catchup_actual_wu": catch, "w_waste_actual": normal + idle + catch,
            "planned_total_recovery_wu": number(r, "planned_total_recovery_wu"), "actual_total_recovery_wu": number(r, "actual_total_recovery_wu"),
            "backup_network_sent_bytes": task_network[task_id], "local_peak_bytes": number(p, "local_peak_bytes"),
            "remote_peak_bytes": number(p, "remote_peak_bytes")})
        planned = number(r, "planned_total_recovery_wu")
        task_rows[-1]["execution_ratio"] = number(r, "actual_total_recovery_wu") / planned if planned else None
    def aggregate(group):
        counts = ("completed", "failed", "started", "on_at_fault", "initializing_at_fault", "recovery_attempted",
                  "recovery_accepted", "recovered", "recovery_failed", "deadline_met", "deadline_missed",
                  "normal_protection_eq_wu", "recovery_reserved_idle_eq_wu", "recovery_catchup_actual_wu",
                  "w_waste_actual", "backup_network_sent_bytes")
        result = {key: sum(t[key] for t in group) for key in counts}
        result.update(tasks=len(group), paths=dict(Counter(t["path"] for t in group if t["path"])),
                      local_peak_bytes=max((t["local_peak_bytes"] for t in group), default=0),
                      remote_peak_bytes=max((t["remote_peak_bytes"] for t in group), default=0))
        for key in ("planned_total_recovery_wu", "actual_total_recovery_wu"):
            result[key] = stats([t[key] for t in group if t["recovery_attempted"]])
        result["execution_ratio"] = stats([t["execution_ratio"] for t in group if t["execution_ratio"] is not None])
        return result
    pools = rows(root, "protection-node-storage-summary.csv", True)
    require(all(number(p, "final_used_bytes") == number(p, "final_reserved_bytes") == 0 for p in pools), "storage leak")
    if pools:
        finalization = read(root, "protection-finalization.json")
        require(finalization["quiescent"], "requests/merges/locks/flows not quiescent")
    else:
        finalization = {"quiescent": True, "tasks_with_storage_failure": []}
        require(not protected and not recoveries, "missing storage ledger")
    summary = aggregate(task_rows)
    summary["recovery_reasons"] = dict(Counter(r["terminal_reason"] for r in recoveries.values()))
    summary["task_failure_reasons"] = dict(Counter(t["failure_reason"] for t in tasks.values() if t["final_state"] == "FAILED"))
    summary["waste_to_original_work_ratio"] = summary["w_waste_actual"] / sum(number(t, "compute_work_units") for t in tasks.values())
    faults = read(root, "fault-trace.json")["faults"] if (root / "fault-trace.json").exists() else []
    fault_counts = {"records": len(faults), "F1": sum(bool(f["f1_occurred"]) for f in faults),
                    "F2": sum(bool(f["f2_occurred"]) for f in faults), "F3": sum(f["fault_type"] == "satellite" for f in faults)}
    return {"label": ("FIXED" if pools else "OFF") + " EXECUTION VALIDATION — NOT CompFRR ALGORITHM RESULT", "summary": summary,
            "fault_identity": fault_counts,
            "profiles": {p: aggregate([t for t in task_rows if t["profile"] == p]) for p in PROFILES},
            "path_outcome": {f"{path}/{outcome}": aggregate([t for t in task_rows if (t["path"], t["outcome"]) == (path, outcome)])
                             for path, outcome in sorted({(t["path"], t["outcome"]) for t in task_rows if t["path"]})},
            "network": dict(network), "local_result_logical_bytes": sum(number(r, "result_bytes") for r in recoveries.values()
                if r["result_delivery_mode"] == "LOCAL" and r["logical_completion"] == "1"),
            "storage": {"node_peak_bytes": stats([number(p, "peak_total_bytes") for p in pools]),
                        "max_node_peak_bytes": max((number(p, "peak_total_bytes") for p in pools), default=0),
                        "allocation_failures": sum(number(p, "allocation_failures") for p in pools), **finalization},
            "task_rows": task_rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--reference", type=Path)
    args = parser.parse_args()
    result = analyze(args.run)
    if args.reference:
        result["off_equivalence"] = compare(args.reference, args.run)
    task_rows = result.pop("task_rows")
    with (args.run / "protection-accounting-by-task.csv").open("w") as out:
        writer = csv.DictWriter(out, fieldnames=list(task_rows[0]))
        writer.writeheader()
        writer.writerows(task_rows)
    (args.run / "protection-accounting.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result["summary"], indent=2))


if __name__ == "__main__":
    main()
