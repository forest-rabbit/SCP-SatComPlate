#!/usr/bin/env python3
"""Independent recent-U window reconstruction and three-way paired evaluation; no tuning."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
RUN = runpy.run_path(str(HERE / "run-n5c-recent-u.py"))
OLD = runpy.run_path(str(HERE / "analyze-n5c-u-audit.py"))
V4 = OLD["V4"]
rows, require, near = V4["rows"], V4["require"], V4["near"]


def window(history, node, time, horizon):
    """Difference of independent actual-service prefixes; no upcoming workload in U."""
    require(horizon >= 0, "negative recent horizon")
    begin = max(0, time-horizon)
    first, last = history.query(node, begin), history.query(node, time)
    result = {key:last[key]-first[key] for key in ("normal_busy_ns", "recovery_busy_ns", "exposure_ns")}
    busy, exposure = result["normal_busy_ns"]+result["recovery_busy_ns"], result["exposure_ns"]
    require(0 <= busy <= exposure, "recent reconstruction exceeds live exposure")
    result.update(window_begin_ns=begin, window_end_ns=time, horizon_ns=horizon,
        recent_utilization=busy/exposure if exposure else 0, history_unavailable=int(not exposure),
        cumulative_utilization=last["cumulative_U"] or 0)
    return result


def recent_audit(directory, run):
    history = OLD["History"].from_output(directory)
    tasks = {r["task_id"]:r for r in rows(directory, "task-summary.csv")}
    companion = rows(directory, "n5c-recent-u-history.csv")
    keyed = {(r["decision_id"], r["candidate_node"]):r for r in companion}
    require(len(keyed) == len(companion), "duplicate recent window row")
    groups = defaultdict(list)
    for r in rows(directory, "n5c-placement-decisions.csv"):
        require(r["variant"] == "recent-U", "wrong spatial variant")
        key = r["decision_id"], r["candidate_node"]
        observed = keyed.pop(key)
        time, node = int(r["time_ns"]), int(r["candidate_node"])
        require(observed["task_id"] == r["task_id"] and int(observed["time_ns"]) == time,
                "window identity mismatch")
        horizon = OLD["remaining_ns"](tasks[r["task_id"]], time)
        expected = window(history, node, time, horizon)
        for field, value in expected.items():
            if field.endswith("utilization"):
                near(float(observed[field]), value, f"independent {field}")
            else:
                require(int(observed[field]) == value, f"independent recent {field}")
        current = history.query(node, time)
        require(all(int(r[k]) == current[k] for k in ("normal_busy_ns", "recovery_busy_ns", "exposure_ns")),
                "cumulative raw history changed")
        r = dict(r, recent_U=expected["recent_utilization"], window=expected)
        groups[r["decision_id"]].append(r)
    require(not keyed and groups, "orphan or vacuous recent evidence")
    details, counts = [], Counter()
    for decision, candidates in groups.items():
        legal = [r for r in candidates if r["feasible"] == "1"]
        counts["all_candidate_rows"] += len(candidates)
        counts["feasible_candidate_rows"] += len(legal)
        counts["zero_U_feasible_rows"] += sum(r["recent_U"] == 0 for r in legal)
        counts["unavailable_feasible_rows"] += sum(r["window"]["history_unavailable"] for r in legal)
        if not legal:
            counts["no_feasible_proposals"] += 1
            continue
        selected = next(r for r in legal if r["selected"] == "1")
        alternate = OLD["snapshot_no_u"](legal)
        all_zero = all(r["recent_U"] == 0 for r in legal)
        changed = selected["candidate_node"] != alternate["candidate_node"]
        committed = selected["committed"] == "1"
        details.append(dict(run=run, decision_id=decision, task_id=selected["task_id"],
            time_ns=selected["time_ns"], committed=committed, horizon_ns=selected["window"]["horizon_ns"],
            feasible_count=len(legal), zero_U_count=sum(r["recent_U"] == 0 for r in legal),
            all_candidates_zero_U=all_zero, selected_recent_U=selected["recent_U"],
            selected_cumulative_U=selected["window"]["cumulative_utilization"],
            selected_remote=int(selected["candidate_node"]),
            same_snapshot_noU_remote=int(alternate["candidate_node"]),
            same_snapshot_noU_changes=changed, dominant=selected["dominant_dimension"]))
        for scope in ("proposals", "committed") if committed else ("proposals",):
            counts[scope] += 1
            counts[scope+"_all_zero_U"] += all_zero
            counts[scope+"_selected_zero_U"] += selected["recent_U"] == 0
            counts[scope+"_different_from_snapshot_noU"] += changed
    return dict(counts=dict(counts), decisions=details, groups=groups)


def matched_selection(first, second):
    def commits(directory):
        result = {}
        for row in rows(directory, "n5c-placement-decisions.csv"):
            if row["selected"] == row["committed"] == "1":
                require(row["task_id"] not in result, "multiple START commitments")
                result[row["task_id"]] = row
        return result
    a, b = commits(first), commits(second)
    common = a.keys() & b.keys()
    same_time = [k for k in common if a[k]["time_ns"] == b[k]["time_ns"]]
    return dict(first_only=len(a.keys()-b.keys()), recent_only=len(b.keys()-a.keys()), both=len(common),
        same_time=len(same_time), different_time=len(common)-len(same_time),
        changed_remote=sum(a[k]["candidate_node"] != b[k]["candidate_node"] for k in common),
        same_time_changed_remote=sum(a[k]["candidate_node"] != b[k]["candidate_node"] for k in same_time),
        note="Actual trajectories; not a same-snapshot counterfactual.")


def fault_signature(directory):
    trace = json.loads((directory / "fault-trace.json").read_text())
    return set((x["node_id"], x["fault_type"], x["start_time_ns"], x["duration_ns"],
                x["f1_occurred"], x["f2_occurred"]) for x in trace["faults"] if x["fault_occurred"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=RUN["ROOT"] / "output/n5c-recent-u")
    args = parser.parse_args()
    root = args.root.resolve()
    plan = json.loads((root / "execution-plan.json").read_text())
    require(plan["runs"] == list(RUN["RUNS"]) and plan["variant"] == "recent-U" and
            plan["references"] == RUN["references"](), "evaluation plan/reference changed")
    proofs = json.loads((root / "legacy-equivalence.json").read_text())
    require(set(proofs) == {"full", "noU"} and all(p["status"] == "PASS" for p in proofs.values()),
            "legacy equivalence gates not passed")
    results, table, details, paired, diagnostics = {}, [], [], {}, {}
    cohorts = defaultdict(list)
    for run in RUN["RUNS"]:
        directories = {g:Path(plan["references"][str(run)][g]["directory"]) for g in ("full", "noU")}
        directories["recent-U"] = root / f"run-{run}" / "recent-U"
        for group, directory in directories.items():
            head = plan["commit"] if group == "recent-U" else plan["references"][str(run)][group]["commit"]
            RUN["verify"](directory, group, run, head)
            result = V4["analyze"](directory)
            require(json.loads((directory / "run-summary.json").read_text())["total_compute_work_units"] == 352513119,
                    "frozen workload WU changed")
            results[f"{run}/{group}"] = result
            table.append(OLD["summary_row"](result, run, group, directory))
            cohorts[group].extend(dict(r, task_id=f"{run}:{r['task_id']}")
                                  for r in rows(directory, "recovery-summary.csv"))
            print("AUDITED", run, group, flush=True)
        window_audit = recent_audit(directories["recent-U"], run)
        details.extend(window_audit["decisions"])
        current_faults = fault_signature(directories["recent-U"])
        paired[run] = dict(recent_windows=window_audit["counts"])
        for group in ("full", "noU"):
            old_faults = fault_signature(directories[group])
            paired[run][f"versus_{group}"] = dict(
                catch=OLD["paired_catch"](rows(directories[group], "recovery-summary.csv"),
                    rows(directories["recent-U"], "recovery-summary.csv"), (group,"recent-U")),
                selection=matched_selection(directories[group], directories["recent-U"]),
                fault_events_only_in_reference=sorted(old_faults-current_faults),
                fault_events_only_in_recent=sorted(current_faults-old_faults))
        diagnostics[run] = {}
        for group, directory in directories.items():
            history = OLD["History"].from_output(directory)
            task = next(r for r in rows(directory,"task-summary.csv") if r["task_id"] == "140")
            selected = next((r for r in rows(directory,"n5c-placement-decisions.csv")
                if r["task_id"] == "140" and r["selected"] == r["committed"] == "1"), None)
            if not selected:
                diagnostics[run][group] = dict(status="NO_COMMITMENT")
                continue
            time = int(selected["time_ns"])
            horizon = OLD["remaining_ns"](task,time)
            diagnostics[run][group] = dict(status="OBSERVED", selected_remote=int(selected["candidate_node"]),
                time_ns=time, horizon_ns=horizon,
                nodes={n:window(history,n,time,horizon) for n in (13,15)},
                note="Each variant's own trajectory and START time, not forced identical histories.")
    pooled = {g:OLD["paired_catch"](cohorts[g], cohorts["recent-U"], (g,"recent-U")) for g in ("full","noU")}
    OLD["write_csv"](root / "summary.csv", table)
    OLD["write_csv"](root / "recent-u-decision-audit.csv", details)
    for name, value in (("audit-results",results), ("paired-comparison",paired),
                        ("pooled-paired-catch",pooled), ("task140-diagnostic",diagnostics)):
        (root / f"{name}.json").write_text(json.dumps(value, indent=2)+"\n")
    status = dict(status="RECENT_U_AUDIT_PASS", groups=len(table), reused_references=10,
        new_recent_runs=5, legacy_gate_runs=2, recent_decisions=len(details),
        same_model_defaults=True, automatic_recommendation=False,
        note="Descriptive paired evidence, including missing catch and online fault differences; no tuned thresholds.")
    (root / "audit-status.json").write_text(json.dumps(status,indent=2)+"\n")
    print(json.dumps(status), flush=True)


if __name__ == "__main__":
    main()
