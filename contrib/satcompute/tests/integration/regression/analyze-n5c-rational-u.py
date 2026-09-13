#!/usr/bin/env python3
"""Rational-U causal snapshot and independent audit; no tuning or fault replay."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
OLD = runpy.run_path(str(HERE / "analyze-n5c-u-audit.py"))
V4 = OLD["V4"]
rows, require, near = V4["rows"], V4["require"], V4["near"]
NS = 10**9


def service_history(directory):
    """Reconstruct actual service, not reservations; never open future fault inputs."""
    normal, recovery = defaultdict(list), defaultdict(list)
    recovered = {r["task_id"]: r for r in rows(directory, "recovery-summary.csv")}
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
    for event in recovered.values():
        duration = int(event["actual_recovery_service_ns"])
        if duration:
            start = int(event["recovery_compute_start_time_ns"])
            recovery[int(event["recovery_node"])].append((start, start + duration))
    return OLD["History"](dict(normal), dict(recovery), {})


def features(history, node, time, horizon, exposure, idle_candidate=True):
    """As-of prefixes only. A legal candidate's idle state resolves same-ns boundaries.

    Busy starting later in the same ns must not leak backwards into that proposal.
    A busy END at this timestamp gives I=0. Admission itself is never reconstructed.
    """
    require(time >= 0 and 0 <= exposure <= time and horizon > 0, "invalid Rational-U time domain")
    current = history.query(node, time)
    require(not idle_candidate or not current["active_before_asof"], "candidate executing actual service")
    busy = current["normal_busy_ns"] + current["recovery_busy_ns"]
    require(busy <= exposure, "actual service exceeds logged live exposure")
    idle = 0 if current["active_before_asof"] else time - (current["last_busy_end_ns"] or 0)
    g = busy / exposure if exposure else 0
    freshness = horizon / (horizon + idle)
    begin = max(0, time - horizon)
    previous = history.query(node, begin)
    recent_busy = busy - previous["normal_busy_ns"] - previous["recovery_busy_ns"]
    recent_exposure = max(0, exposure - begin)
    require(0 <= recent_busy <= recent_exposure, "recent prefix exposure mismatch")
    return dict(U_global=g, T_idle_ns=idle, T_remaining_ns=horizon,
        idle_to_remaining_ratio=idle / horizon, freshness=freshness, U_rational=g * freshness,
        U_recent=recent_busy / recent_exposure if recent_exposure else 0,
        history_unavailable=int(not exposure), same_ns_boundary=current["same_ns_boundary"])


def stats(values):
    values = sorted(values)
    def q(p):
        if not values:
            return None
        x = (len(values) - 1) * p
        return values[math.floor(x)] + (values[math.ceil(x)] - values[math.floor(x)]) * (x % 1)
    return dict(count=len(values), exact_zero_count=sum(v == 0 for v in values),
        abs_below_1e_12_count=sum(abs(v) < 1e-12 for v in values),
        abs_below_1e_9_count=sum(abs(v) < 1e-9 for v in values),
        p10=q(.1), p50=q(.5), p90=q(.9), p95=q(.95), max=max(values, default=None))


def winner(candidates, score):
    legal = [r for r in candidates if r["feasible"]]
    return min(legal, key=lambda r: (r[score], r["propagation_ns"], r["candidate_node"])) if legal else None


def snapshot(directory):
    history = service_history(directory)
    tasks = {r["task_id"]: r for r in rows(directory, "task-summary.csv")}
    groups = defaultdict(list)
    for r in rows(directory, "n5c-placement-decisions.csv"):
        groups[r["decision_id"]].append(r)
    candidates, changes = [], []
    max_rounding = 0
    for decision, items in groups.items():
        first = items[0]
        task, time = tasks[first["task_id"]], int(first["time_ns"])
        horizon = OLD["remaining_ns"](task, time)
        require(horizon > 0, "nonrunning task horizon")
        rate, work = int(task["compute_rate_work_units_per_second"]), int(task["compute_work_units"])
        done = (time - int(task["compute_start_time_ns"])) * rate // NS
        rounded = ((work - done) * NS + rate - 1) // rate
        max_rounding = max(max_rounding, abs(rounded - horizon))
        converted = []
        for r in items:
            node, legal = int(r["candidate_node"]), r["feasible"] == "1"
            f = features(history, node, time, horizon, int(r["exposure_ns"]))
            current = history.query(node, time)
            require(all(int(r[k]) == current[k] for k in ("normal_busy_ns", "recovery_busy_ns")),
                    "logged actual service differs from causal reconstruction")
            if legal:
                near(f["U_global"], float(r["historical_utilization"]), "global U mismatch")
            R, M = (float(r[k]) if legal else None for k in ("recovery_conflict", "storage_pressure"))
            converted.append(dict(decision_id=decision, task_id=r["task_id"], time_ns=time,
                candidate_node=node, feasible=legal, rejection=r["reason"], committed=r["committed"] == "1",
                **f, R=R, M=M,
                Phi_cumulative=max(R, M, f["U_global"]) if legal else None,
                Phi_rational=max(R, M, f["U_rational"]) if legal else None,
                Phi_noU=max(R, M) if legal else None,
                Phi_recent=max(R, M, f["U_recent"]) if legal else None,
                propagation_ns=int(r["propagation_ns"])))
        winners = {name: winner(converted, "Phi_" + name) for name in ("cumulative", "rational", "noU", "recent")}
        for r in converted:
            r.update({"winner_" + name: (w["candidate_node"] if w else None) for name, w in winners.items()})
        candidates.extend(converted)
        if not winners["cumulative"]:
            continue
        a, b, n = (winners[k] for k in ("cumulative", "rational", "noU"))
        if first["variant"] == "full":
            require([r["candidate_node"] for r in items if r["selected"] == "1"] == [str(a["candidate_node"])],
                    "snapshot failed to reproduce actual FULL selection")
        changes.append(dict(decision_id=decision, task_id=first["task_id"], time_ns=time,
            committed=first["committed"] == "1", **{"winner_" + name: w["candidate_node"] for name, w in winners.items()},
            changed_vs_cumulative=a["candidate_node"] != b["candidate_node"],
            same_as_noU=b["candidate_node"] == n["candidate_node"],
            stale_history_change=a["candidate_node"] != b["candidate_node"] and
                b["T_idle_ns"] >= horizon and b["U_global"] > a["U_global"],
            rational_U_dominant=b["U_rational"] > max(b["R"], b["M"]),
            rational_U_exceeds_RM_candidates=sum(r["feasible"] and r["U_rational"] > max(r["R"], r["M"])
                                                for r in converted)))
    require(changes, "vacuous snapshot")
    legal = [r for r in candidates if r["feasible"]]
    summary = dict(source=str(directory.resolve()), source_execution=json.loads((directory / "execution.json").read_text()),
        proposals=len(groups), feasible_proposals=len(changes), all_candidates=len(candidates), feasible_candidates=len(legal),
        changed_vs_cumulative=sum(r["changed_vs_cumulative"] for r in changes),
        same_as_noU=sum(r["same_as_noU"] for r in changes),
        stale_history_changes=sum(r["stale_history_change"] for r in changes),
        rational_selected_U_dominant=sum(r["rational_U_dominant"] for r in changes),
        rational_candidates_U_exceeds_RM=sum(r["rational_U_exceeds_RM_candidates"] for r in changes),
        max_integer_remaining_work_rounding_difference_ns=max_rounding,
        distributions={k: stats(r[k] for r in legal) for k in ("U_global", "U_rational", "U_recent")},
        note="Same logged candidate trajectory, not a new simulation. Infeasible scores are N/A; no future fault input read. "
             "1e-12/1e-9 are diagnostics only, not ranking/admission thresholds. R/M/feasibility/ties unchanged.")
    task140 = [r for r in candidates if r["task_id"] == "140" and r["candidate_node"] in (13, 15, 23)]
    return dict(summary=summary, candidates=candidates, changes=changes, task140=task140)


def persist_snapshot(source, output):
    require(not output.exists(), "refuse to overwrite snapshot evidence")
    result = snapshot(source)
    output.mkdir(parents=True)
    OLD["write_csv"](output / "candidate-snapshot.csv", result["candidates"])
    OLD["write_csv"](output / "selection-diff.csv", result["changes"])
    for name, value in (("summary", result["summary"]), ("task140-diagnostic", result["task140"])):
        (output / (name + ".json")).write_text(json.dumps(value, indent=2) + "\n")
    return result["summary"]


def audit_actual(directory, reconstructed=None):
    reconstructed = reconstructed or snapshot(directory)
    expected = {(r["decision_id"], str(r["candidate_node"])): r for r in reconstructed["candidates"]}
    seen = set()
    for row in rows(directory, "n5c-rational-u-history.csv"):
        key = row["decision_id"], row["candidate_node"]
        require(key in expected and key not in seen, "orphan/duplicate Rational-U history")
        seen.add(key)
        r = expected[key]
        require(row["task_id"] == r["task_id"] and int(row["time_ns"]) == r["time_ns"], "history identity")
        for column, field in (("horizon_ns", "T_remaining_ns"), ("continuous_idle_ns", "T_idle_ns"),
                              ("history_unavailable", "history_unavailable")):
            require(int(row[column]) == r[field], f"independent Rational-U {column} mismatch")
        for column, field in (("freshness", "freshness"), ("cumulative_utilization", "U_global"),
                              ("rational_pressure", "U_rational")):
            require(math.isclose(float(row[column]), r[field], rel_tol=1e-12, abs_tol=1e-15),
                    f"independent Rational-U {column} mismatch")
    require(seen == expected.keys(), "missing Rational-U history")
    winners = {r["decision_id"]: r["winner_rational"] for r in reconstructed["changes"]}
    for r in rows(directory, "n5c-placement-decisions.csv"):
        require(r["variant"] == "rational-U", "wrong actual variant")
        if r["selected"] == "1":
            require(int(r["candidate_node"]) == winners[r["decision_id"]], "independent rational winner mismatch")
    return dict(status="PASS", checked_candidate_rows=len(seen),
                checked_feasible_proposals=len(winners), future_fault_input_read=False)


def compare(root):
    run = runpy.run_path(str(HERE / "run-n5c-rational-u.py"))
    plan = json.loads((root / "execution-plan.json").read_text())
    require(plan["run"] == 11 and plan["variant"] == "rational-U" and
            plan["references"] == run["references"](), "evaluation plan/reference changed")
    directories = {g: Path(plan["references"][g]["directory"]) for g in ("full", "noU")}
    directories["rational-U"] = root / "run-11/rational-U"
    run["verify"](directories["rational-U"], plan["commit"])
    results, task_accounts, recoveries, snapshots, table = {}, {}, {}, {}, []
    for group, directory in directories.items():
        result = V4["analyze"](directory, include_tasks=True)
        require(result["summary"]["tasks"] == 800 and
                json.loads((directory / "run-summary.json").read_text())["total_compute_work_units"] == 352513119,
                "frozen task population/WU changed")
        task_accounts[group] = {r["task_id"]: r for r in result.pop("task_rows")}
        results[group] = result
        recoveries[group] = rows(directory, "recovery-summary.csv")
        row = OLD["summary_row"](result, 11, group, directory)
        row.update(relocated_state_logical_bytes=result["placement_recovery"]["relocated_state_logical_bytes"],
                   migration_total_sent_bytes=result["placement_recovery"]["migration_total_sent_bytes"])
        table.append(row)
        snapshots[group] = snapshot(directory)
        print("AUDITED", group, flush=True)
    independent = audit_actual(directories["rational-U"], snapshots["rational-U"])
    paired, deltas, placement_diffs = {}, [], []
    def commits(directory):
        selected = [r for r in rows(directory, "n5c-placement-decisions.csv") if r["selected"] == r["committed"] == "1"]
        require(len(selected) == len({r["task_id"] for r in selected}), "multiple commitments for one task")
        return {r["task_id"]: r for r in selected}
    actual = {g: commits(p) for g, p in directories.items()}
    def fault_signature(directory):
        return set((f["node_id"], f["fault_type"], f["start_time_ns"], f["duration_ns"],
                    f["f1_occurred"], f["f2_occurred"])
                   for f in json.loads((directory / "fault-trace.json").read_text())["faults"] if f["fault_occurred"])
    current_faults = fault_signature(directories["rational-U"])
    for old in ("full", "noU"):
        previous_faults = fault_signature(directories[old])
        paired[old] = dict(catch=OLD["paired_catch"](recoveries[old], recoveries["rational-U"], (old, "rational-U")),
            faults_only_in_reference=sorted(previous_faults-current_faults),
            faults_only_in_rational=sorted(current_faults-previous_faults))
        a = {(r["task_id"], r["fault_time_ns"], r["fault_type"]): r for r in recoveries[old]}
        b = {(r["task_id"], r["fault_time_ns"], r["fault_type"]): r for r in recoveries["rational-U"]}
        per_catch = []
        for key in sorted(a.keys() & b.keys()):
            if a[key]["actual_T_catch_ns"] and b[key]["actual_T_catch_ns"]:
                per_catch.append(dict(task_id=key[0], delta_seconds=
                    (int(b[key]["actual_T_catch_ns"])-int(a[key]["actual_T_catch_ns"])) / NS))
        paired[old]["largest_absolute_catch_changes"] = sorted(per_catch, key=lambda r: abs(r["delta_seconds"]), reverse=True)[:10]
        paired[old]["excluding_task140_catch"] = OLD["paired_catch"](
            [r for r in recoveries[old] if r["task_id"] != "140"],
            [r for r in recoveries["rational-U"] if r["task_id"] != "140"], (old, "rational-U"))
        require(task_accounts[old].keys() == task_accounts["rational-U"].keys(), "task set changed")
        local = []
        for task in sorted(task_accounts[old], key=int):
            previous, current = task_accounts[old][task], task_accounts["rational-U"][task]
            r = dict(reference=old, task_id=task, reference_completed=previous["completed"], rational_completed=current["completed"])
            for field in ("task_execution_waste_wu", "normal_protection_eq_wu", "reserved_idle_eq_wu", "w_waste_actual"):
                r["reference_"+field], r["rational_"+field] = previous[field], current[field]
                r["delta_"+field] = current[field]-previous[field]
            deltas.append(r)
            local.append(r)
            p, q = actual[old].get(task), actual["rational-U"].get(task)
            placement_diffs.append(dict(reference=old, task_id=task,
                reference_remote=p["candidate_node"] if p else None, rational_remote=q["candidate_node"] if q else None,
                both_committed=bool(p and q), same_time=p["time_ns"] == q["time_ns"] if p and q else None,
                same_config=all(p[k] == q[k] for k in ("delta_permille", "batch_n")) if p and q else None,
                remote_changed=p["candidate_node"] != q["candidate_node"] if p and q else None))
        paired[old]["largest_absolute_eq_waste_changes"] = sorted(local, key=lambda r: abs(r["delta_w_waste_actual"]), reverse=True)[:10]
    failures = {g: [r for r in rows(p, "task-summary.csv") if r["final_state"] != "COMPLETED"] for g, p in directories.items()}
    task140 = {g: dict(candidates=[r for r in snapshots[g]["candidates"] if r["task_id"] == "140"],
        task=next(r for r in rows(p, "task-summary.csv") if r["task_id"] == "140"),
        recovery=[r for r in recoveries[g] if r["task_id"] == "140"], account=task_accounts[g]["140"])
        for g, p in directories.items()}
    for name, value in (("audit-results", results), ("paired-comparison", paired), ("failure-diagnostics", failures),
                        ("task140-diagnostic", task140), ("rational-snapshot-summary", snapshots["rational-U"]["summary"])):
        (root / (name+".json")).write_text(json.dumps(value, indent=2)+"\n")
    for name, value in (("summary", table), ("per-task-comparison", deltas), ("actual-placement-diff", placement_diffs),
                        ("rational-decision-audit", snapshots["rational-U"]["changes"])):
        OLD["write_csv"](root / (name+".csv"), value)
    status = dict(status="RATIONAL_U_AUDIT_PASS", groups=3, new_formal_runs=1, reused_formal_references=2,
        independent_history=independent, no_default_promotion=True, next_action="STOP_FOR_USER_REVIEW")
    (root / "audit-status.json").write_text(json.dumps(status, indent=2)+"\n")
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument("--snapshot", type=Path)
    choice.add_argument("--compare", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    require(not args.snapshot or args.output, "snapshot requires --output")
    print(json.dumps(persist_snapshot(args.snapshot, args.output) if args.snapshot else compare(args.compare), indent=2))


if __name__ == "__main__":
    main()
