#!/usr/bin/env python3
"""Read-only Gate A diagnosis; never choose a model, replay faults, or tune thresholds."""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
RUN = runpy.run_path(str(HERE / "run-n5c-u-audit.py"))
V4 = runpy.run_path(str(HERE / "analyze-n5c-placement.py"))
rows, require, NS = V4["rows"], V4["require"], 10**9


def write_csv(path, data):
    require(data, f"no rows for {path.name}")
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(data[0]))
        writer.writeheader()
        writer.writerows(data)


class History:
    """Offline actual-service intervals. Every query clips at its as-of timestamp."""
    def __init__(self, normal, recovery, failures):
        self.normal, self.recovery, self.failures = normal, recovery, failures
        for node in set(normal) | set(recovery):
            intervals = sorted(normal.get(node, []) + recovery.get(node, []))
            previous = 0
            for begin, end in intervals:
                require(0 <= begin < end and begin >= previous, "overlapping/invalid actual compute intervals")
                require(end <= failures.get(node, end), "compute continued after permanent F3")
                previous = end

    @classmethod
    def from_output(cls, directory):
        normal, recovery = defaultdict(list), defaultdict(list)
        recovered = {r["task_id"]:r for r in rows(directory, "recovery-summary.csv")}
        for task in rows(directory, "task-summary.csv"):
            begin = int(task["compute_start_time_ns"])
            if begin < 0:
                continue
            event = recovered.get(task["task_id"])
            end = int(event["fault_time_ns"]) if event else int(task["compute_complete_time_ns"])
            if end < 0:
                end = int(task["failure_time_ns"])
            require(end >= begin, "missing primary actual end")
            if end > begin:
                normal[int(task["compute_node_id"])].append((begin, end))
        for r in recovered.values():
            duration = int(r["actual_recovery_service_ns"])
            if duration:
                require(bool(r["recovery_compute_start_time_ns"]), "actual recovery without start")
                start = int(r["recovery_compute_start_time_ns"])
                recovery[int(r["recovery_node"])].append((start, start + duration))
        faults = json.loads((directory / "fault-trace.json").read_text())["faults"]
        failures = {int(f["node_id"]):int(f["start_time_ns"]) for f in faults
                    if f["fault_type"] == "satellite" and f["fault_occurred"]}
        return cls(dict(normal), dict(recovery), failures)

    def query(self, node, time):
        require(time >= 0, "negative as-of time")
        normal, recovery = self.normal.get(node, []), self.recovery.get(node, [])
        integral = lambda spans: sum(max(0, min(end, time)-begin) for begin,end in spans if begin < time)
        end = max((end for begin,end in normal+recovery if end <= time), default=None)
        active = any(begin < time < finish for begin,finish in normal+recovery)
        boundary = any(time in (begin, finish) for begin,finish in normal+recovery)
        n, r = integral(normal), integral(recovery)
        exposure = min(time, self.failures.get(node, time))
        require(n+r <= exposure, "actual history exceeds live exposure")
        return dict(normal_busy_ns=n, recovery_busy_ns=r, exposure_ns=exposure,
            cumulative_U=(n+r)/exposure if exposure else None, last_busy_end_ns=end,
            idle_duration_ns=None if active or boundary else time-(end or 0),
            active_before_asof=active, same_ns_boundary=boundary)


def snapshot_no_u(candidates):
    legal = [r for r in candidates if r["feasible"] == "1"]
    if not legal:
        return None
    return min(legal, key=lambda r:(max(float(r["recovery_conflict"]), float(r["storage_pressure"])),
                                   int(r["propagation_ns"]), int(r["candidate_node"])))


def remaining_ns(task, time):
    work, rate = int(task["compute_work_units"]), int(task["compute_rate_work_units_per_second"])
    start = int(task["compute_start_time_ns"])
    require(rate > 0 and 0 <= start <= time, "invalid current primary task clock")
    return max(0, (work*NS+rate-1)//rate - (time-start))


def selection_audit(directory, run, group):
    tasks = {r["task_id"]:r for r in rows(directory, "task-summary.csv")}
    history = History.from_output(directory)
    groups = defaultdict(list)
    for r in rows(directory, "n5c-placement-decisions.csv"):
        groups[r["decision_id"]].append(r)
    details, committed, snapshots = [], {}, {}
    for decision_id, candidates in groups.items():
        first = candidates[0]
        task, time = first["task_id"], int(first["time_ns"])
        left = remaining_ns(tasks[task], time)
        selected = [r for r in candidates if r["selected"] == "1"]
        require(len(selected) <= 1, "multiple selected candidates")
        for r in candidates:
            if r["feasible"] == "1":
                observed = history.query(int(r["candidate_node"]), time)
                require(all(int(r[k]) == observed[k] for k in
                    ("normal_busy_ns", "recovery_busy_ns", "exposure_ns")), "causal history differs from candidate snapshot")
                V4["near"](float(r["historical_utilization"]), observed["cumulative_U"] or 0, "cumulative U reconstruction")
        if not selected:
            continue
        winner = selected[0]
        alternate = snapshot_no_u(candidates)
        rivals = [r for r in candidates if r["feasible"] == "1" and r != winner]
        rival = min(rivals, key=lambda r:(float(r["bottleneck"]), int(r["propagation_ns"]),
                    int(r["candidate_node"]))) if rivals else None
        observed = history.query(int(alternate["candidate_node"]), time)
        changed = winner["candidate_node"] != alternate["candidate_node"]
        recent_idle = (observed["idle_duration_ns"] is not None and observed["idle_duration_ns"] >= left)
        item = dict(run=run, group=group, task_id=task, decision_id=decision_id,
            decision_time_ns=time, remaining_compute_time_ns=left, committed=winner["committed"] == "1",
            selected_remote=int(winner["candidate_node"]), selected_U=float(winner["historical_utilization"]),
            best_competing_remote=int(rival["candidate_node"]) if rival else None,
            best_competing_U=float(rival["historical_utilization"]) if rival else None,
            same_snapshot_noU_remote=int(alternate["candidate_node"]), same_snapshot_remote_changed=changed,
            alternate_U=float(alternate["historical_utilization"]),
            alternate_last_busy_end_ns=observed["last_busy_end_ns"],
            alternate_idle_duration_ns=observed["idle_duration_ns"],
            alternate_idle_at_least_remaining_time=recent_idle,
            same_ns_history_boundary=observed["same_ns_boundary"],
            historical_inertia_diagnostic=changed and recent_idle and
                float(alternate["historical_utilization"]) > float(winner["historical_utilization"]),
            alternate_minus_selected_propagation_ns=int(alternate["propagation_ns"])-int(winner["propagation_ns"]),
            alternate_minus_selected_R=float(alternate["recovery_conflict"])-float(winner["recovery_conflict"]),
            alternate_minus_selected_M=float(alternate["storage_pressure"])-float(winner["storage_pressure"]))
        details.append(item)
        if item["committed"]:
            require(task not in committed, "multiple actual START commitments for one task")
            committed[task] = dict(row=winner, detail=item)
            snapshots[task] = candidates
    return dict(details=details, committed=committed, snapshots=snapshots, history=history, tasks=tasks)


def task_diff(full, nou, run):
    require(full["tasks"].keys() == nou["tasks"].keys(), "different task populations")
    output = []
    for task in sorted(full["tasks"], key=int):
        a, b = full["committed"].get(task), nou["committed"].get(task)
        match = ("NEITHER_COMMITTED" if not a and not b else "FULL_ONLY" if not b else
                 "NOU_ONLY" if not a else "BOTH_SAME_TIME" if a["row"]["time_ns"] == b["row"]["time_ns"]
                 else "BOTH_DIFFERENT_TIME")
        r = dict(run=run, task_id=task, match=match,
            actual_remote_changed=(a["row"]["candidate_node"] != b["row"]["candidate_node"]) if a and b else None,
            local_changed=(a["row"]["reference_local_node"] != b["row"]["reference_local_node"]) if a and b else None,
            configuration_changed=any(a["row"][k] != b["row"][k] for k in ("delta_permille", "batch_n")) if a and b else None)
        for name, v in (("full",a), ("noU",b)):
            for key in ("time_ns", "candidate_node", "reference_local_node", "delta_permille", "batch_n",
                        "historical_utilization", "recovery_conflict", "storage_pressure", "propagation_ns"):
                r[name+"_"+key] = v["row"][key] if v else None
            r[name+"_outcome"] = (full if name == "full" else nou)["tasks"][task]["final_state"]
        for key in ("same_snapshot_noU_remote", "same_snapshot_remote_changed", "historical_inertia_diagnostic"):
            r[key] = a["detail"][key] if a else None
        for key in ("recovery_conflict", "storage_pressure", "propagation_ns"):
            r["cross_trajectory_noU_minus_full_"+key] = float(b["row"][key])-float(a["row"][key]) if a and b else None
        output.append(r)
    return output


def paired_catch(first, second, labels=("full", "noU")):
    first_label, second_label = labels
    require(first_label != second_label, "paired group labels must differ")
    def latency(values):
        # Signed paired differences have no meaningful HHI/concentration shares.
        values = list(values)
        result = V4["distribution"](values)
        return dict({k:result[k] for k in ("count", "mean", "p50", "p95", "max")},
                    min=min(values, default=None))
    def index(values):
        result = {(r["task_id"], r["fault_time_ns"], r["fault_type"]):r for r in values}
        require(len(result) == len(values), "duplicate recovery identity")
        return result
    a, b = index(first), index(second)
    common = a.keys() & b.keys()
    observed = [k for k in common if a[k]["actual_T_catch_ns"] and b[k]["actual_T_catch_ns"]]
    return dict(common_faults=len(common), paired_catch_count=len(observed),
        common_without_two_catches=len(common)-len(observed), **{
        first_label+"_only_faults":len(a.keys()-b.keys()),
        second_label+"_only_faults":len(b.keys()-a.keys()),
        first_label:latency(int(a[k]["actual_T_catch_ns"])/NS for k in observed),
        second_label:latency(int(b[k]["actual_T_catch_ns"])/NS for k in observed),
        f"delta_{second_label}_minus_{first_label}_seconds":latency(
            (int(b[k]["actual_T_catch_ns"])-int(a[k]["actual_T_catch_ns"]))/NS for k in observed)})


def summary_row(result, run, group, source, selection=None):
    s, p = result["summary"], result["placement_recovery"]
    a = result["resource_concentration"]["backup_assignment_count"]
    storage = result["resource_concentration"]["backup_storage_time_integral_byte_ns"]
    r = dict(run=run, group=group, source=str(source), execution_commit=result["execution"]["commit"],
        completed=s["completed"], deadline_success=s["on_time"], failed=s["failed"],
        recovery_attempted=p["recovery_attempted"], recovery_failed=p["recovery_failed_count"],
        designated_at_fault=p["designated_at_fault"], busy_at_fault=p["backup_busy_at_fault"], busy_rate=p["backup_busy_ratio"],
        direct=p["direct_count"], relocate=p["relocate_count"], recompute=p["recompute_count"],
        catch_count=p["catch_seconds"]["count"], without_catch=p["without_catch"],
        catch_mean_s=p["catch_seconds"]["mean"], catch_p50_s=p["catch_seconds"]["p50"], catch_p95_s=p["catch_seconds"]["p95"],
        actual_execution_waste_wu=s["task_execution_waste_wu"], actual_catchup_wu=s["actual_catchup_wu"],
        primary_work_at_fault_wu=sum(int(x["actual_work_units"]) for x in rows(source,"recovery-summary.csv")),
        uncheckpointed_fault_work_wu=sum(int(x["actual_work_units"])-int(x["local_work_units"]) for x in rows(source,"recovery-summary.csv")),
        total_eq_waste=s["w_waste_actual"], active_eq_cost=s["task_execution_waste_wu"]+s["normal_protection_eq_wu"],
        normal_protection_eq_wu=s["normal_protection_eq_wu"], reserved_idle_eq_wu=s["reserved_idle_eq_wu"],
        extra_application_bytes=result["network"]["extra_sent_bytes"], mean_link_utilization_percent=result["links"]["mean_utilization_percent"],
        assignment_count=a["sum"], assignment_top1=a["top1_share"], assignment_top5=a["top5_share"],
        assignment_hhi=a["hhi"], assignment_gini=a["gini"], storage_hhi=storage["hhi"], storage_top1=storage["top1_share"],
        peak_active_backups=result["resource_concentration"]["peak_active_backups"]["max"],
        peak_backup_storage_bytes=result["resource_concentration"]["peak_backup_storage_bytes"]["max"],
        F1=result["fault_counts"]["F1"], F2=result["fault_counts"]["F2"], F3=result["fault_counts"]["F3"])
    dominant = result["n5c"]["selected_dominant"] if result["n5c"] else {}
    for name in ("RECOVERY_CONFLICT", "COMPUTE_HISTORY", "STORAGE"):
        r[name+"_dominant_proposals"] = dominant.get(name, 0)
    actual = [x for x in selection["details"] if x["committed"]] if selection else []
    r["same_snapshot_noU_changes"] = sum(x["same_snapshot_remote_changed"] for x in actual) if selection else None
    r["historical_inertia_diagnostics"] = sum(x["historical_inertia_diagnostic"] for x in actual) if selection else None
    return r


def task140_diagnostic(full, nou):
    current = full["committed"].get("140")
    other = nou["committed"].get("140")
    result = dict(full_winner=int(current["row"]["candidate_node"]) if current else None,
        noU_winner=int(other["row"]["candidate_node"]) if other else None, recentU_winner=None,
        note="Gate A: recent-U is not implemented; null is N/A, never zero probability/load.")
    if not current:
        result["status"] = "NO_FULL_COMMITMENT"
        return result
    time = int(current["row"]["time_ns"])
    result.update(status="DIAGNOSTIC_ONLY", decision_time_ns=time,
        remaining_task_time_s=current["detail"]["remaining_compute_time_ns"]/NS,
        noU_decision_time_ns=int(other["row"]["time_ns"]) if other else None,
        same_snapshot_noU_winner=current["detail"]["same_snapshot_noU_remote"])
    for node in (13,15):
        r = next((r for r in full["snapshots"]["140"] if int(r["candidate_node"]) == node), None)
        value = full["history"].query(node,time)
        value.update(recent_U=None, candidate_present=bool(r), feasible=r["feasible"] == "1" if r else None,
            R=float(r["recovery_conflict"]) if r and r["feasible"] == "1" else None,
            M=float(r["storage_pressure"]) if r and r["feasible"] == "1" else None,
            propagation_ns=int(r["propagation_ns"]) if r and r["feasible"] == "1" else None)
        result[f"node_{node}"] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    plan = json.loads((root / "execution-plan.json").read_text())
    require(plan["runs"] == list(RUN["RUNS"]) and plan["groups"] == list(RUN["GROUPS"]), "changed audit matrix")
    results, table, diffs, decisions, paired = {}, [], [], [], {}
    for run in RUN["RUNS"]:
        selection, directories, generated = {}, {}, {}
        for group in RUN["GROUPS"]:
            directory = Path(plan["reference_run11"][group]) if run == 11 else root / f"run-{run}" / group
            expected = RUN["OLD_EXECUTION"] if run == 11 else plan["commit"]
            RUN["verify_execution"](directory, group, run, expected)
            r = V4["analyze"](directory)
            require(json.loads((directory / "run-summary.json").read_text())["total_compute_work_units"] == 352513119,
                    "frozen total task WU differs")
            if group != "fa-ffp":
                selection[group] = selection_audit(directory,run,group)
                decisions.extend(selection[group]["details"])
            table.append(summary_row(r,run,group,directory,selection.get(group)))
            results[f"{run}/{group}"] = r
            directories[group] = directory
            generated[group] = json.loads((directory / "fault-trace.json").read_text())
            print("AUDITED", run, group, flush=True)
        pair = paired_catch(rows(directories["full"],"recovery-summary.csv"), rows(directories["noU"],"recovery-summary.csv"))
        pair["versus_fa_ffp"] = {g:paired_catch(rows(directories["fa-ffp"],"recovery-summary.csv"),
            rows(directories[g],"recovery-summary.csv"), labels=("fa_ffp",g)) for g in ("full","noU")}
        current_diff = task_diff(selection["full"], selection["noU"], run)
        diffs.extend(current_diff)
        pair.update(selection_matching=dict(Counter(r["match"] for r in current_diff)),
            actual_remote_changed=sum(r["actual_remote_changed"] is True for r in current_diff),
            fault_trace_exactly_equal={g:generated[g] == generated["full"] for g in RUN["GROUPS"]})
        signature = lambda f: sorted((x["node_id"],x["fault_type"],x["start_time_ns"],x["duration_ns"],
            x["f1_occurred"],x["f2_occurred"]) for x in f["faults"] if x["fault_occurred"])
        pair["actual_fault_events_equal"] = {g:signature(generated[g]) == signature(generated["full"]) for g in RUN["GROUPS"]}
        base_events = set(signature(generated["full"]))
        pair["actual_fault_event_differences"] = {g:dict(
            only_in_group=sorted(set(signature(generated[g]))-base_events),
            only_in_full=sorted(base_events-set(signature(generated[g])))) for g in RUN["GROUPS"]}
        paired[run] = pair
        output = root / f"run-{run}"
        output.mkdir(exist_ok=True)
        (output / "task140-u-diagnostic.json").write_text(json.dumps(task140_diagnostic(selection["full"],selection["noU"]), indent=2)+"\n")
    write_csv(root/"summary.csv",table)
    write_csv(root/"full-vs-noU-task-diff.csv",diffs)
    write_csv(root/"u-decision-audit.csv",decisions)
    (root/"audit-results.json").write_text(json.dumps(results,indent=2)+"\n")
    (root/"paired-comparison.json").write_text(json.dumps(paired,indent=2)+"\n")
    result = dict(status="GATE_A_AUDIT_PASS", groups=len(table), new_executions=12, reused_executions=3,
        model_changed=False, gate_b="AWAITING_JOINT_REVIEW", thresholds_added=False,
        notes=["All history queries are clipped at placement time; no future result enters policy.",
               "Same-snapshot counterfactuals isolate ranking only; cross-run placement differences include downstream effects.",
               "Catch populations and busy denominators vary by group; no missing catch is imputed as zero.",
               "Uncheckpointed WU, actual execution waste, normal equivalent cost and reserved-idle are distinct."])
    (root/"audit-status.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result),flush=True)


if __name__ == "__main__":
    main()
