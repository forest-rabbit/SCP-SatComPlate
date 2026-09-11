#!/usr/bin/env python3
"""Read-only 32-run feasibility ablation audit using the maintained actual-work/flow ledgers."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
MATRIX = runpy.run_path(str(HERE / "run-pre-n5c-placement-matrix.py"))
BASE = runpy.run_path(str(HERE / "analyze-baseline-evaluation.py"))
FREQ = runpy.run_path(str(HERE / "analyze-frequency-evaluation.py"))
RISK = runpy.run_path(str(HERE / "analyze-riskweighted-start.py"))
STAGING = RISK["API"]
rows, require, number = BASE["rows"], BASE["require"], BASE["number"]
NS = 10**9
SENTINELS = ("120", "399", "596", "447", "457", "114", "252", "574")


def distribution(values):
    values = list(values)
    result = FREQ["stats"](values)
    total = sum(values)
    ordered = sorted(values)
    result.update(top1_share=max(values, default=0)/total if total else None,
        top3_share=sum(ordered[-3:])/total if total else None,
        top10_share=sum(ordered[-10:])/total if total else None,
        gini=sum((2*i-len(values)-1)*v for i, v in enumerate(ordered, 1))/(len(values)*total) if total else None,
        coefficient_of_variation=(math.sqrt(sum((v-total/len(values))**2 for v in values)/len(values)) /
                                  (total/len(values))) if total else None)
    return result


def role_loads(root, protected, scheme):
    validated = FREQ["concentration"](root)
    nodes = [r["node_id"] for r in validated["nodes"]]
    events = rows(root, "placement-load-events.csv")
    result = {"final_counts_zero": True, "events_reconciled": validated["events_reconciled"]}
    for role in ("local", "remote", "single", "recovery"):
        if (role in ("local", "remote")) != (scheme in ("fixed", "compfrr")) and role != "recovery":
            result[role] = None
            continue
        totals, active, peaks = Counter(), Counter(), Counter()
        for e in events:
            recovery = e["event"].startswith("RECOVERY")
            if recovery != (role == "recovery" or (role == "single" and scheme == "recompute")):
                continue
            node = protected[e["task_id"]]["local_node"] if role == "local" else e["node_id"]
            established = e["event"] in ("ASSIGNMENT_ESTABLISHED", "RECOVERY_ACCEPTED")
            active[node] += 1 if established else -1
            require(active[node] >= 0, "role assignment underflow")
            if established:
                totals[node] += 1
                peaks[node] = max(peaks[node], active[node])
        require(not any(active.values()), "role assignment leak")
        result[role] = {**distribution(totals[n] for n in nodes),
            "active_peak": max(peaks.values(), default=0),
            "per_node": {n: {"count": totals[n], "active_peak": peaks[n]} for n in nodes}}
    return result


def occupation(root, tasks, recoveries):
    """Physical primary service and accepted recovery reservations; half-open intervals."""
    result = defaultdict(list)
    by_id = {r["task_id"]: r for r in recoveries}
    for t in tasks.values():
        start = number(t, "compute_start_time_ns")
        if start < 0:
            continue
        r = by_id.get(t["task_id"])
        stop = number(r, "fault_time_ns") if r else number(t, "compute_complete_time_ns")
        if stop < start:
            stop = number(t, "failure_time_ns")
        if stop >= start:
            result[t["compute_node_id"]].append(dict(task=t["task_id"], kind="ordinary",
                                                     start_ns=start, end_ns=stop))
    for r in recoveries:
        if not r["recovery_accept_time_ns"]:
            continue
        stop = r["recovery_compute_complete_time_ns"] or r["terminal_time_ns"]
        result[r["recovery_node"]].append(dict(task=r["task_id"], kind="recovery",
            start_ns=number(r, "recovery_accept_time_ns"), end_ns=int(stop)))
    return result


def overlapping(intervals, begin, end):
    return [r for r in intervals if r["start_ns"] < end and r["end_ns"] > begin]


def diagnostics(root, tasks, protected, recoveries, decisions, selections):
    pauses = rows(root, "frequency-pause-intervals.csv", True)
    occupiers = occupation(root, tasks, recoveries)
    fault_rows = []
    for r in recoveries:
        task, key = tasks[r["task_id"]], r["task_id"]
        p = protected.get(key)
        if not p:
            continue
        fault = number(r, "fault_time_ns")
        remote = p["remote_node"]
        own = [o for o in occupiers[remote] if o["task"] != key]
        at_fault = [o for o in own if o["start_ns"] <= fault < o["end_ns"]]
        configurations = [d for d in decisions if d["task_id"] == key and
            d["decision_committed"] == "1" and d["proposed_action"] in ("START", "UPDATE") and
            number(d, "fault_epoch_time_ns") < fault]
        delta = number(configurations[-1], "proposed_delta_permille") if configurations else number(p, "delta_permille")
        window_ns = math.ceil(number(task, "compute_work_units")*delta*NS /
                              (1000*number(task, "compute_rate_work_units_per_second")))
        begin = max(number(p, "start_time_ns"), fault-window_ns)
        during = overlapping(own, begin, fault)
        pause_ns = sum(max(0, min(fault, number(e, "end_time_ns"))-max(begin, number(e, "start_time_ns")))
                       for e in pauses if e["task_id"] == key)
        actual, local, committed = (number(r, k) for k in ("actual_work_units", "local_work_units", "remote_work_units"))
        work = number(task, "compute_work_units")
        fault_rows.append(dict(task_id=key, fault_time_ns=fault, local_node=p["local_node"], remote_node=remote,
            selection_time_ns=number(p, "start_time_ns"), selection_healthy_idle=True,
            fault_time_busy=r["remote_busy_at_fault"] == "1", occupants_at_fault=at_fault,
            pre_fault_window_start_ns=begin, pre_fault_window_end_ns=fault,
            pre_fault_occupants=during, pre_fault_busy=bool(during), pre_fault_pause_ns=pause_ns,
            later_occupants=overlapping(own, number(p, "start_time_ns"), fault),
            checkpoint_lag_wu=actual-local, remote_lag_wu=local-committed,
            x_minus_l=(actual-local)/work, l_minus_r=(local-committed)/work,
            final_state=task["final_state"], failure_reason=task["failure_reason"], chosen_path=r["chosen_path"]))
    reasons = sorted({r["reason"] for r in pauses})
    by_reason = {reason: FREQ["stats"](number(r, "duration_ns")/NS for r in pauses if r["reason"] == reason)
                 for reason in reasons}
    sentinels = {}
    for key in SENTINELS:
        t = tasks[key]
        ds = [d for d in decisions if d["task_id"] == key]
        start = next((d for d in ds if d["proposed_action"] == "START" and d["decision_committed"] == "1"), None)
        sentinels[key] = dict(final_state=t["final_state"], failure_reason=t["failure_reason"],
            selection_rows=[s for s in selections if s["task_id"] == key],
            protected=protected.get(key), recovery=next((r for r in recoveries if r["task_id"] == key), None),
            first_committed_start=start,
            pauses=[p for p in pauses if p["task_id"] == key],
            capacity_resumes=[d for d in ds if d["phase_before"] == "ON" and d["decision_trigger"] == "CAPACITY_RELEASE" and
                              d["decision_committed"] == "1" and d["proposed_action"] == "UPDATE"],
            contention=next((r for r in fault_rows if r["task_id"] == key), None))
    return dict(fault_rows=fault_rows,
        fault_time_busy_count=sum(r["fault_time_busy"] for r in fault_rows),
        fault_time_busy_by_ordinary=sum(r["fault_time_busy"] and any(o["kind"] == "ordinary" for o in r["occupants_at_fault"]) for r in fault_rows),
        fault_time_busy_by_recovery=sum(r["fault_time_busy"] and any(o["kind"] == "recovery" for o in r["occupants_at_fault"]) for r in fault_rows),
        pre_fault_busy_count=sum(r["pre_fault_busy"] for r in fault_rows), pause_by_reason=by_reason,
        checkpoint_lag_wu=FREQ["stats"](r["checkpoint_lag_wu"] for r in fault_rows),
        capacity_release_resume_count=sum(d["phase_before"] == "ON" and d["decision_trigger"] == "CAPACITY_RELEASE" and
            d["decision_committed"] == "1" and d["proposed_action"] == "UPDATE" for d in decisions),
        sentinels=sentinels,
        window_definition="[max(protection start, fault - latest configured delta*W/primary_rate), fault); "
                          "fault busy uses runtime snapshot; occupation intervals do not invent queued/transition ownership")


def analyze_run(root):
    value = BASE["analyze"](root)
    scheme = value["execution"]["protection_mode"]
    tasks = {t["task_id"]: t for t in rows(root, "task-summary.csv")}
    protected = {p["task_id"]: p for p in rows(root, "protection-task-summary.csv", True)}
    selections = rows(root, "placement-selections.csv")
    decisions = rows(root, "frequency-decisions.csv", True)
    require(len({(s["task_id"], s["time_ns"]) for s in selections}) == len(selections), "duplicate placement decision")
    mode = value["execution"]["placement_mode"]
    require(all(s["placement_mode"] == mode and s["selected_by_minimal_policy"] == str(int(not mode.startswith("fa-")))
                for s in selections), "selection identity mismatch")
    value["placement"] = role_loads(root, protected, scheme)
    value["selection"] = dict(count=len(selections), outcomes=dict(Counter(s["actual_admission"] for s in selections)),
                              reasons=dict(Counter(s["reason"] for s in selections)))
    if decisions:
        FREQ["verify_pair_retries"](decisions, rows(root, "frequency-capacity-waits.csv"))
        value["score_checked"] = sum(RISK["decision_check"](d, tasks[d["task_id"]],
            value["execution"]["input_staging_policy"] == "deferred") for d in decisions)
    else:
        value["score_checked"] = 0
    if scheme == "compfrr":
        value["staging"] = STAGING["staging_metrics"](root, value)
    physical = value["network"]
    if scheme == "one-plus-one":
        # Includes loser RESULT payload. Winner RESULT remains business even if it is a replica.
        physical["normal_ft_bytes"] = physical["extra_sent_bytes"]
        physical["fault_ft_bytes"] = 0
        physical["partition_note"] = "Replica provision/execution traffic; no new transfer operation is created by takeover."
    else:
        physical["normal_ft_bytes"] = sum(physical["by_kind"][k]["sent_bytes"] for k in STAGING["NORMAL"])
        physical["fault_ft_bytes"] = sum(physical["by_kind"][k]["sent_bytes"] for k in STAGING["FAULT"])
    require(physical["normal_ft_bytes"] + physical["fault_ft_bytes"] == physical["extra_sent_bytes"], "FT flow partition")
    pools = rows(root, "protection-node-storage-summary.csv")
    value["storage"].update(applicable=scheme in ("fixed", "compfrr"),
        node_peak_distribution=FREQ["stats"](number(p, "peak_total_bytes") for p in pools),
        rejection_reasons={k:v for k,v in value["selection"]["reasons"].items() if "STORAGE" in k or "RESERVATION" in k})
    value["links"]["max_single_link_full_mean_utilization_percent"] = max(
        100*float(r["tx_busy_time_s"])/float(r["measurement_duration_s"]) for r in rows(root, "link-summary.csv"))
    value["diagnostics"] = diagnostics(root, tasks, protected, value["recovery_rows"], decisions, selections)
    value["failed_task_ids"] = [t["task_id"] for t in value["task_rows"] if t["failed"]]
    return value


def validate_run(root, group, head=None):
    scenario, mode = group.split("-", 1)
    e = json.loads((root / "execution.json").read_text())
    require((e["protection_mode"], e["input_staging_policy"], e["remote_busy_recovery_policy"]) ==
            MATRIX["SCENARIOS"][scenario] and e["placement_mode"] == mode, "wrong matrix group")
    require(not e["worktree_dirty"] and e["seed"] == 1 and e["run"] == 11 and e["fault_mode"] == "generate" and
            not e["audit"] and not e["shadow"] and e["simulation_duration_s"] == 1300, "formal identity mismatch")
    if head:
        require(e["commit"] == head, "different formal HEAD")
    require(json.loads((root / "execution-result.json").read_text())["returncode"] == 0, "failed process")
    run = json.loads((root / "run-summary.json").read_text())
    require(run["simulation_duration_ns"] == 1300*NS and run["compute_node_count"] == 66, "incomplete/wrong topology run")
    ts = rows(root, "task-summary.csv")
    require(len(ts) == 800 and sum(number(t, "compute_work_units") for t in ts) == 352513119 and
            sum(number(t, "input_bytes") for t in ts) == 194119753287 and
            sum(number(t, "compute_work_units") for t in ts if t["task_profile"] == "llm") == 61333200,
            "frozen workload changed")
    return e["commit"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    results, head = {}, None
    for scenario in MATRIX["SCENARIOS"]:
        for mode in MATRIX["MODES"]:
            group = f"{scenario}-{mode}"
            head = validate_run(root / group, group, head)
            results[group] = analyze_run(root / group)
            print(f"AUDITED {group}", flush=True)
    compact = []
    for group, r in results.items():
        s, n, link = r["summary"], r["network"], r["links"]
        compact.append(dict(group=group, **{k:s[k] for k in ("completed", "failed", "recovery_attempted", "recovery_accepted",
            "recovery_success", "task_execution_waste_wu", "successful_extra_execution_wu", "failed_task_executed_wu",
            "normal_protection_eq_wu", "reserved_idle_eq_wu", "w_waste_actual", "actual_catchup_wu", "actual_post_catchup_wu")},
            failed_task_ids=";".join(r["failed_task_ids"]), **r["fault_counts"],
            normal_ft_bytes=n["normal_ft_bytes"], fault_ft_bytes=n["fault_ft_bytes"], total_ft_bytes=n["extra_sent_bytes"],
            mean_link_utilization_percent=link["mean_utilization_percent"],
            max_link_full_mean_percent=link["max_single_link_full_mean_utilization_percent"],
            max_backup_peak_bytes=r["storage"]["max_node_peak_bytes"],
            remote_top3_share=r["placement"]["remote"]["top3_share"] if r["placement"]["remote"] else None,
            fault_time_remote_busy=r["diagnostics"]["fault_time_busy_count"],
            pre_fault_remote_busy=r["diagnostics"]["pre_fault_busy_count"],
            capacity_resumes=r["diagnostics"]["capacity_release_resume_count"],
            T_catch_p50_s=r["T_catch_s"].get("p50"), T_catch_p90_s=r["T_catch_s"].get("p90")))
        if not r["storage"]["applicable"]:
            compact[-1]["max_backup_peak_bytes"] = None
        if r["execution"]["protection_mode"] == "one-plus-one":
            compact[-1]["actual_catchup_wu"] = compact[-1]["actual_post_catchup_wu"] = None
    with (root / "master-summary.csv").open("w") as f:
        writer = csv.DictWriter(f, fieldnames=list(compact[0]))
        writer.writeheader(); writer.writerows(compact)
    (root / "master-summary.json").write_text(json.dumps(dict(commit=head, groups=results, table=compact,
        note="Single controlled seed/run; all 32 use the same workload/model. No cross-version causal ranking. "
             "Checkpoint post-fault recovery ranking frozen. Placement metrics never enter policy."), indent=2) + "\n")
    print(json.dumps({"status":"PASS", "groups":len(results), "commit":head,
                      "scored_decisions":sum(r["score_checked"] for r in results.values())}), flush=True)


if __name__ == "__main__":
    main()
