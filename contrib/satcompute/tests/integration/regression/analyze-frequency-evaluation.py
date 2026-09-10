#!/usr/bin/env python3
"""N5B-G3 descriptive A/B/C accounting. No simulation, score change or statistical claims."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import runpy
import shlex

ACCOUNTING = runpy.run_path(str(Path(__file__).with_name("analyze-protection-accounting.py")))
rows, require = ACCOUNTING["rows"], ACCOUNTING["require"]
PROFILES = ACCOUNTING["PROFILES"]
NS = 10**9


def verify_pair_retries(decisions, waits):
    retries = set()
    for r in decisions:
        if not r.get("pair_candidates_total"):
            continue  # Historical rows and fixed-ON pair records.
        n = lambda k: int(r[k])
        require(n("pair_candidates_total") == n("pair_node_feasible") + n("pair_skip_node"), "node pair counts differ")
        require(n("pair_node_feasible") == n("pair_path_feasible") + sum(n(k) for k in
                ("pair_skip_no_route", "pair_skip_no_capacity", "pair_skip_other")), "path pair counts differ")
        require(n("pair_hard_checked") == n("pair_hard_feasible") + n("pair_skip_storage") + n("pair_skip_deadline")
                and n("pair_hard_checked") <= n("pair_path_feasible") and n("pair_hard_feasible") <= 1,
                "hard-feasibility ranked prefix differs")
        if r.get("decision_trigger") == "CAPACITY_RELEASE":
            key = (r["task_id"], r["fault_epoch_time_ns"])
            require(key not in retries and r["phase_before"] == "OFF" and
                    r["actual_fault_sampled"] == r["actual_fault_hit"] == "0", "invalid or duplicate capacity retry")
            retries.add(key)
            require((r["capacity_retry_success"] == "1") ==
                    (r["decision_committed"] == "1" and r["proposed_action"] == "START"), "retry success differs")
    intervals = defaultdict(list)
    for r in waits:
        a, b = int(r["start_time_ns"]), int(r["end_time_ns"])
        require(b - a == int(r["duration_ns"]) >= 0, "invalid protection capacity wait")
        intervals[r["task_id"]].append((a, b))
    for values in intervals.values():
        values.sort()
        require(all(a[1] <= b[0] for a, b in zip(values, values[1:])), "overlapping capacity waits")


def stats(values):
    values = sorted(values)
    def quantile(p):
        if not values:
            return None
        x = (len(values) - 1) * p
        low, high = math.floor(x), math.ceil(x)
        return values[low] + (values[high] - values[low]) * (x - low)
    return dict(count=len(values), sum=sum(values), max=max(values, default=None),
                mean=sum(values) / len(values) if values else None,
                p10=quantile(.1), p50=quantile(.5), p90=quantile(.9))


def concentration(root):
    nodes = rows(root, "placement-node-summary.csv")
    events = rows(root, "placement-load-events.csv")
    loads, totals, peaks = defaultdict(Counter), defaultdict(Counter), defaultdict(Counter)
    owners = {"backup": {}, "recovery": {}}
    previous = -1
    for e in events:
        time, node, task = int(e["time_ns"]), e["node_id"], e["task_id"]
        require(time >= previous, "placement event time regression")
        previous = time
        kind = "backup" if e["event"].startswith("ASSIGNMENT") else "recovery"
        active = e["event"] in ("ASSIGNMENT_ESTABLISHED", "RECOVERY_ACCEPTED")
        if active:
            require(task not in owners[kind], "duplicate active placement")
            owners[kind][task] = node
            loads[node][kind] += 1
            totals[node][kind] += 1
            peaks[node][kind] = max(peaks[node][kind], loads[node][kind])
        else:
            require(owners[kind].pop(task, None) == node, "placement release without ownership")
            loads[node][kind] -= 1
        require(loads[node]["backup"] == int(e["active_backup_assignments"]) and
                loads[node]["recovery"] == int(e["active_recoveries"]), "placement event count mismatch")
    require(not owners["backup"] and not owners["recovery"], "live placement leak")
    for r in nodes:
        node = r["node_id"]
        for kind, total, peak, final in (
            ("backup", "backup_assignment_count_total", "peak_active_backup_assignments", "active_backup_assignments"),
            ("recovery", "accepted_recovery_count", "peak_active_recoveries", "active_recoveries")):
            require(int(r[total]) == totals[node][kind] and int(r[peak]) == peaks[node][kind]
                    and int(r[final]) == 0, "placement summary mismatch")
    result = {"nodes": nodes, "final_counts_zero": True, "events_reconciled": len(events)}
    for name, column in (("backup", "backup_assignment_count_total"), ("recovery", "accepted_recovery_count")):
        counts = [int(r[column]) for r in nodes]
        result[name] = stats(counts)
        result[name]["top3_share"] = sum(sorted(counts, reverse=True)[:3]) / sum(counts) if sum(counts) else None
    return result


def active_weight(configurations, start, stop, pauses):
    """Weight committed settings only over physically ON, unpaused wall time."""
    weight = delta = batch = 0
    for index, (time, d, n) in enumerate(configurations):
        begin = max(start, time)
        end = min(stop, configurations[index+1][0] if index+1 < len(configurations) else stop)
        if end <= begin:
            continue
        duration = end - begin
        for a, b in pauses:
            duration -= max(0, min(end, b) - max(begin, a))
        require(duration >= 0, "overlapping pause intervals")
        weight += duration
        delta += d * duration
        batch += n * duration
    return weight, delta, batch


def frequency(root, task_profiles, protected, recoveries):
    decisions = rows(root, "frequency-decisions.csv", True)
    intervals = rows(root, "frequency-pause-intervals.csv", True)
    capacity_waits = rows(root, "frequency-capacity-waits.csv", True)
    verify_pair_retries(decisions, capacity_waits)
    for r in capacity_waits:
        require(int(r["end_time_ns"]) - int(r["start_time_ns"]) == int(r["duration_ns"]) >= 0,
                "invalid protection capacity wait")
    on = {r["task_id"]: int(r["time_ns"]) for r in rows(root, "protection-events.csv")
          if r["event"] == "INIT_COST_COMMITTED" and r["attempt_generation"] == "0"}
    pauses, configs = defaultdict(list), defaultdict(list)
    for r in intervals:
        start, end = int(r["start_time_ns"]), int(r["end_time_ns"])
        require(end - start == int(r["duration_ns"]) >= 0, "invalid pause duration")
        require(end <= int(protected[r["task_id"]]["stop_time_ns"]), "pause after protection stopped")
        pauses[r["task_id"]].append((start, end))
    for values in pauses.values():
        values.sort()
        require(all(a[1] <= b[0] for a, b in zip(values, values[1:])), "overlapping pause intervals")
    for r in decisions:
        if r["decision_committed"] == "1" and r["proposed_action"] in ("START", "UPDATE"):
            configs[r["task_id"]].append((int(r["fault_epoch_time_ns"]),
                int(r["committed_delta_permille"]) / 1000, int(r["committed_n"])))

    def summarize(ids):
        ds = [r for r in decisions if r["task_id"] in ids]
        committed = [r for r in ds if r["decision_committed"] == "1"]
        starts = [r for r in committed if r["proposed_action"] == "START"]
        updates = [r for r in committed if r["proposed_action"] == "UPDATE"]
        chosen = starts + updates
        ps = [r for r in intervals if r["task_id"] in ids]
        pause_episodes = 0
        for task in ids:
            end = -1
            for a, b in pauses[task]:
                if a > end:
                    pause_episodes += 1
                end = b
        weighted = [active_weight(configs[task], on[task], int(protected[task]["stop_time_ns"]), pauses[task])
                    for task in ids if task in on and configs[task]]
        weight, delta, batch = (sum(w[i] for w in weighted) for i in range(3))
        reasons = Counter(r["proposal_reason"] for r in ds)
        return {
            "tasks": len(ids), "decisions": len(ds), "start_count": len(starts),
            "never_start_count": len(ids - {r["task_id"] for r in starts}),
            "fault_phases": dict(Counter(r["phase_at_fault"] for r in recoveries if r["task_id"] in ids)),
            "update_count": len(updates), "changed_update_count": sum(
                (r["current_delta_permille"], r["current_n"]) != (r["committed_delta_permille"], r["committed_n"])
                for r in updates),
            "delta": stats([int(r["committed_delta_permille"]) / 1000 for r in chosen]),
            "n": stats([int(r["committed_n"]) for r in chosen]),
            "unpaused_on_duration_ns": weight,
            "time_weighted_delta": delta / weight if weight else None,
            "time_weighted_n": batch / weight if weight else None,
            "pause_decision_count": sum(r["proposed_action"] == "PAUSE" for r in committed),
            "pause_count": pause_episodes, "pause_duration_ns": sum(int(r["duration_ns"]) for r in ps),
            "pause_reason_intervals": dict(Counter(r["reason"] for r in ps)),
            "pause_reason_duration_ns": {reason: sum(int(r["duration_ns"]) for r in ps if r["reason"] == reason)
                                         for reason in sorted({r["reason"] for r in ps})},
            "start_p_finish": stats([float(r["p_fail_before_finish"]) for r in starts]),
            "start_margin": stats([float(r["j_off"]) - float(r["j_start"]) for r in starts if r["j_off"]]),
            "start_off_unavailable": sum(not r["j_off"] for r in starts),
            "task_start_decisions": sum(r.get("decision_trigger") == "TASK_RUNNING" for r in ds),
            "immediate_starts": sum(r.get("decision_trigger") == "TASK_RUNNING" for r in starts),
            "start_by_trigger": dict(Counter(r.get("decision_trigger") for r in starts)),
            "capacity_retry_count": sum(r.get("decision_trigger") == "CAPACITY_RELEASE" for r in ds),
            "capacity_retry_success": sum(r.get("capacity_retry_success") == "1" for r in ds),
            "capacity_wait_tasks": len({r["task_id"] for r in capacity_waits if r["task_id"] in ids}),
            "capacity_wait_ns": stats([int(r["duration_ns"]) for r in capacity_waits if r["task_id"] in ids]),
            "pair_counts_at_off_decisions": {k: stats([int(r[k]) for r in ds if r.get(k)]) for k in
                ("pair_candidates_total", "pair_node_feasible", "pair_path_feasible", "pair_hard_checked", "pair_hard_feasible")},
            "proposal_reasons": dict(reasons),
            "tasks_ever_storage_rejected": len({r["task_id"] for r in ds if r["proposal_reason"] == "STORAGE_INFEASIBLE"}),
        }
    return {"summary": summarize(set(task_profiles)),
            "profiles": {p: summarize({t for t, v in task_profiles.items() if v == p}) for p in PROFILES}}


def analyze(root):
    result = ACCOUNTING["analyze"](root)
    result.pop("task_rows")
    tasks = rows(root, "task-summary.csv")
    profiles = {r["task_id"]: r["task_profile"] for r in tasks}
    recoveries = rows(root, "recovery-summary.csv")
    protected = {r["task_id"]: r for r in rows(root, "protection-task-summary.csv")}
    result["label"] = "N5B-G3 single paired seed/run; C is placement diagnostic, not N5C"
    result["concentration"] = concentration(root)
    require(result["concentration"]["recovery"]["sum"] == result["summary"]["recovery_accepted"],
            "accepted recovery ledger differs")
    result["frequency"] = frequency(root, profiles, protected, recoveries)
    result["frequency"]["dynamic_policy"] = (root / "frequency-decisions.csv").exists()
    if not result["frequency"]["dynamic_policy"]:
        # Fixed has no probability decisions. Do not label its protected tasks as never START.
        for profile, summary in [(None, result["frequency"]["summary"]),
                                 *result["frequency"]["profiles"].items()]:
            ps = [r for task, r in protected.items() if profile is None or profiles[task] == profile]
            summary["start_count"] = len(ps)
            summary["never_start_count"] = summary["tasks"] - len(ps)
            summary["delta"] = stats([int(r["delta_permille"]) / 1000 for r in ps])
            summary["n"] = stats([int(r["batch_n"]) for r in ps])
    result["recovery_diagnostics"] = {
        "checkpoint_state_exists": sum(r["checkpoint_state_exists"] == "1" for r in recoveries),
        "remote_eligible_at_fault": sum(r["remote_eligible_at_fault"] == "1" for r in recoveries),
        "remote_busy_at_fault": sum(r["remote_busy_at_fault"] == "1" for r in recoveries),
        "fallback_reasons": dict(Counter(r["checkpoint_fallback_reason"] for r in recoveries if r["checkpoint_fallback_reason"])),
        "recompute_reasons": dict(Counter(r["checkpoint_fallback_reason"] for r in recoveries if r["chosen_path"] == "RECOMPUTE")),
        "relocation_evaluated": sum(r.get("checkpoint_relocation_attempted") == "1" for r in recoveries),
        "relocation_paths": dict(Counter(r["chosen_path"] for r in recoveries if r["chosen_path"].startswith("MIGRATE_"))),
        "relocation_state_bytes": sum(int(r.get("checkpoint_relocation_bytes") or 0) for r in recoveries),
        "relocation_failure_reasons": dict(Counter(r["checkpoint_relocation_failure_reason"] for r in recoveries if r.get("checkpoint_relocation_failure_reason"))),
        "T_catch_s": stats([int(r["actual_T_catch_ns"]) / NS for r in recoveries if r["actual_T_catch_ns"]]),
        "no_observed_catchup": sum(not r["actual_T_catch_ns"] for r in recoveries),
        "work_units": {k: stats([int(r[k]) for r in recoveries]) for k in (
            "planned_catchup_redo_wu", "planned_post_catchup_wu", "planned_total_recovery_wu",
            "actual_catchup_redo_wu", "actual_post_catchup_wu", "actual_total_recovery_wu")},
        "local_input_logical_bytes": sum(int(next(t["input_bytes"] for t in tasks if t["task_id"] == r["task_id"]))
            for r in recoveries if r["input_delivery_mode"] == "LOCAL"),
    }
    impacts = rows(root, "fault-task-impact.csv")
    direct = [r for r in impacts if r["impact_type"].startswith("RUNNING_INTERRUPTED")]
    result["fault_identity"].update(direct_victims=len({r["task_id"] for r in direct}),
        direct_victim_incidents=len({(r["task_id"], r["fault_id"]) for r in direct}),
        all_impact_types=dict(Counter(r["impact_type"] for r in impacts)))
    result["storage"]["conservative_model_observation"] = (
        result["frequency"]["summary"]["tasks_ever_storage_rejected"] > 0
        and result["storage"]["max_node_peak_bytes"] < 5_000_000_000)
    if (root / "execution.json").exists():
        result["execution"] = json.loads((root / "execution.json").read_text())
        result["execution_result"] = json.loads((root / "execution-result.json").read_text())
    return result


def fairness(runs):
    identities = [r["execution"] for r in runs]
    expected = [("compfrr", "ffp"), ("compfrr", "lrl")]
    if len(runs) == 3: expected.insert(0, ("fixed", "ffp"))
    require([(r["protection_mode"], r["placement_mode"]) for r in identities] == expected,
            "paired policies differ")
    ignored = {"outputDir", "faultTrace", "protectionMode", "placementMode"}
    commands = []
    for r in identities:
        require(r["fault_mode"] == "generate" and r["lrl_recovery_weight"] == 1 and
                not r["worktree_dirty"] and not r["audit"] and not r["shadow"], "formal run identity differs")
        commands.append({arg.split("=", 1)[0][2:]: arg.split("=", 1)[1]
                         for arg in shlex.split(r["command"][-1])[1:]
                         if arg.split("=", 1)[0][2:] not in ignored})
    require(all(c == commands[0] for c in commands), "unpaired scenario arguments")
    require(len({r["commit"] for r in identities}) == 1, "formal runs use different code")
    count = 801 if len(runs) == 2 else 800
    require(all(r["execution_result"]["returncode"] == 0 and r["summary"]["tasks"] == count
                and r["execution"]["simulation_duration_s"] == 1300 for r in runs), "formal run incomplete")
    return {"same_code_and_arguments_except_policy_and_output": True,
            "same_fault_trace_required": False, "lambda": 1, "seed": 1, "run": 11,
            "task_count": count, "groups": "B/C" if len(runs) == 2 else "A/B/C"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", nargs="+", required=True, type=Path, help="B C (801 tasks), historical A B C (800), or one diagnostic run")
    args = parser.parse_args()
    runs = [analyze(root) for root in args.runs]
    paired = fairness(runs) if len(runs) in (2, 3) else None
    for root, result in zip(args.runs, runs):
        (root / "frequency-evaluation.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({"run": str(root), "summary": result["summary"], "faults": result["fault_identity"]}))
    if paired:
        (args.runs[0].parent / "paired-evaluation.json").write_text(json.dumps({"fairness": paired,
            "runs": {root.name: r for root, r in zip(args.runs, runs)}}, indent=2) + "\n")


if __name__ == "__main__":
    main()
