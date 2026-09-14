#!/usr/bin/env python3
"""Frozen five-run, three-way audit; leave-one-task-out is statistics, not a rerun."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
RUN = runpy.run_path(str(HERE / "run-n5c-rational-multirun.py"))
RAT = runpy.run_path(str(HERE / "analyze-n5c-rational-u.py"))
OLD, V4, rows, require = (RAT[k] for k in ("OLD", "V4", "rows", "require"))
GROUPS = ("full", "noU", "rational-U")
PAIRS = (("full", "noU"), ("full", "rational-U"), ("noU", "rational-U"))
FIELDS = ("task_execution_waste_wu", "normal_protection_eq_wu", "reserved_idle_eq_wu",
          "w_waste_actual", "extra_application_bytes")
TASK_SIGNATURE = ("task_id", "source_node_id", "compute_node_id", "result_node_id", "input_bytes",
    "output_bytes", "compute_work_units", "compute_rate_work_units_per_second", "arrival_time_ns",
    "task_profile", "baseline_compute_time_ns", "compute_deadline_budget_ns")


def task_signature(tasks):
    require(len(tasks) == len({r["task_id"] for r in tasks}), "duplicate task")
    return sorted(tuple(r[k] for k in TASK_SIGNATURE) for r in tasks)


def task_network(transfers):
    """Actual protection flow bytes, including partial/cancelled; LocalDelivery has no flow."""
    unique, totals = {}, defaultdict(int)
    for r in transfers:
        value = (str(r["task_id"]), r["kind"], int(r["sent_bytes"]))
        require(value[2] >= 0, "negative actual flow bytes")
        key = r["transfer_id"]
        require(key not in unique or unique[key] == value, "conflicting duplicate protection flow")
        unique[key] = value
    for task, _, sent in unique.values():
        totals[task] += sent
    return totals


def cohort(accounts, recoveries):
    totals = {field: sum(t[field] for t in accounts.values()) for field in FIELDS}
    busy = [r for r in recoveries if r["remote_node"] and r["phase_at_fault"] in ("ON", "INITIALIZING")]
    paths = V4["recovery_composition"](recoveries, [])
    return dict(tasks=len(accounts), completed=sum(t["completed"] for t in accounts.values()),
        failed=sum(t["failed"] for t in accounts.values()), **totals,
        designated_at_fault=len(busy), busy_at_fault=sum(r["remote_busy_at_fault"] == "1" for r in busy),
        direct=paths["direct_count"], relocate=paths["relocate_count"], recompute=paths["recompute_count"],
        recovery_failed=paths["recovery_failed_count"])


def recovery_index(values):
    indexed = {(r["task_id"], r["fault_time_ns"], r["fault_type"]): r for r in values}
    require(len(indexed) == len(values), "duplicate fault identity")
    return indexed


def instance_key(identity):
    run, task = identity.split(":")
    return int(run), int(task)


def largest(contributions, selector):
    require(selector in ("largest_benefit", "largest_absolute"), "unknown tail selector")
    eligible = {k: v for k, v in contributions.items() if
                (v > 0 if selector == "largest_benefit" else abs(v) > 0)}
    if not eligible:
        return None
    score = (lambda v: v) if selector == "largest_benefit" else abs
    return min(eligible, key=lambda k: (-score(eligible[k]), instance_key(k)))


def contrast(accounts_a, accounts_b, recovery_a, recovery_b, labels, excluded=None):
    require(accounts_a.keys() == accounts_b.keys(), "unmatched task population")
    if excluded is not None:
        require(excluded in accounts_a, "excluded instance not present")
    a = {k: v for k, v in accounts_a.items() if k != excluded}
    b = {k: v for k, v in accounts_b.items() if k != excluded}
    ra = [r for r in recovery_a if r["task_id"] != excluded]
    rb = [r for r in recovery_b if r["task_id"] != excluded]
    ca, cb = cohort(a, ra), cohort(b, rb)
    return dict(reference=labels[0], candidate=labels[1], excluded_instance=excluded,
        reference_cohort=ca, candidate_cohort=cb,
        delta_candidate_minus_reference={k: cb[k]-ca[k] for k in ca},
        paired_catch=OLD["paired_catch"](ra, rb, labels))


def leave_one_out(accounts_a, accounts_b, recovery_a, recovery_b, labels):
    require(accounts_a.keys() == accounts_b.keys(), "unmatched task population")
    a, b = recovery_index(recovery_a), recovery_index(recovery_b)
    catch = defaultdict(float)
    for key in sorted(a.keys() & b.keys()):
        if a[key]["actual_T_catch_ns"] and b[key]["actual_T_catch_ns"]:
            catch[key[0]] += (int(a[key]["actual_T_catch_ns"])-int(b[key]["actual_T_catch_ns"]))/10**9
    waste = {k: accounts_a[k]["w_waste_actual"]-accounts_b[k]["w_waste_actual"] for k in accounts_a}
    out = []
    for metric, contributions in (("paired_catch_seconds", catch), ("total_eq_waste", waste)):
        for selector in ("largest_benefit", "largest_absolute"):
            selected = largest(contributions, selector)
            out.append(dict(metric=metric, selector=selector, selected_instance=selected,
                benefit_reference_minus_candidate=contributions.get(selected),
                status="EXCLUDED_ONE_INSTANCE" if selected else "NO_NONZERO_ELIGIBLE_CONTRIBUTION",
                result=contrast(accounts_a, accounts_b, recovery_a, recovery_b, labels, selected)))
    return out


def fault_signature(directory):
    return set((f["node_id"], f["fault_type"], f["start_time_ns"], f["duration_ns"],
                f["f1_occurred"], f["f2_occurred"])
        for f in json.loads((directory / "fault-trace.json").read_text())["faults"] if f["fault_occurred"])


def audit(root):
    root = root.resolve()
    plan = json.loads((root / "execution-plan.json").read_text())
    RUN["validate_plan"](plan)
    require(plan["references"] == RUN["references"](), "reference identity changed")
    RUN["source_scope"](plan["commit"])
    results, accounts, recoveries, faults, independent, snapshots, table = {}, {}, {}, {}, {}, {}, []
    signature = None
    for random_run in RUN["RUNS"]:
        run = str(random_run)
        results[run], accounts[run], recoveries[run], faults[run] = {}, {}, {}, {}
        for group in GROUPS:
            ref = plan["references"][run].get(group)
            directory = Path(ref["directory"]) if ref else root / f"run-{run}/rational-U"
            if group == "rational-U":
                RUN["RAT"]["verify"](directory, ref["commit"] if ref else plan["commit"], random_run)
            else:
                RUN["OLD"]["verify_execution"](directory, group, random_run, ref["commit"])
            tasks = rows(directory, "task-summary.csv")
            observed = task_signature(tasks)
            if signature is None:
                signature = observed
            require(observed == signature, "workload signature differs across groups/runs")
            result = V4["analyze"](directory, include_tasks=True)
            require(result["summary"]["tasks"] == 800 and
                json.loads((directory / "run-summary.json").read_text())["total_compute_work_units"] == 352513119,
                "frozen task count/WU changed")
            network = task_network(rows(directory, "protection-transfers.csv"))
            require(set(network) <= {str(t["task_id"]) for t in tasks}, "flow without task")
            require(sum(network.values()) == result["network"]["extra_sent_bytes"], "per-task network totals mismatch")
            accounts[run][group] = {f"{run}:{t['task_id']}": dict(t,
                extra_application_bytes=network.get(str(t["task_id"]), 0)) for t in result.pop("task_rows")}
            recoveries[run][group] = [dict(r, task_id=f"{run}:{r['task_id']}") for r in rows(directory, "recovery-summary.csv")]
            for field in FIELDS[:-1]:
                V4["near"](sum(t[field] for t in accounts[run][group].values()), result["summary"][field], field)
            faults[run][group] = fault_signature(directory)
            row = OLD["summary_row"](result, random_run, group, directory)
            row.update(total_application_bytes=result["network"]["total_physical_application_sent_bytes"],
                max_link_full_mean_percent=result["links"]["max_single_link_full_mean_utilization_percent"],
                relocated_state_logical_bytes=result["placement_recovery"]["relocated_state_logical_bytes"],
                migration_total_sent_bytes=result["placement_recovery"]["migration_total_sent_bytes"])
            table.append(row)
            results[run][group] = result
            if group == "rational-U":
                snapshot = RAT["snapshot"](directory)
                independent[run] = RAT["audit_actual"](directory, snapshot)
                snapshots[run] = snapshot["summary"]
            print("AUDITED", run, group, flush=True)
    pooled_a = {g: {k: v for run in accounts.values() for k, v in run[g].items()} for g in GROUPS}
    pooled_r = {g: [v for run in recoveries.values() for v in run[g]] for g in GROUPS}
    aggregate = {}
    for group in GROUPS:
        chosen = [r for r in table if r["group"] == group]
        c = cohort(pooled_a[group], pooled_r[group])
        c.update(busy_rate=c["busy_at_fault"]/c["designated_at_fault"] if c["designated_at_fault"] else None,
            total_application_bytes=sum(r["total_application_bytes"] for r in chosen),
            assignment_hhi_mean=sum(r["assignment_hhi"] for r in chosen)/len(chosen),
            storage_hhi_mean=sum(r["storage_hhi"] for r in chosen)/len(chosen),
            mean_link_utilization_percent=sum(r["mean_link_utilization_percent"] for r in chosen)/len(chosen),
            max_link_full_mean_percent=max(r["max_link_full_mean_percent"] for r in chosen),
            catch_seconds=V4["distribution"](int(r["actual_T_catch_ns"])/10**9 for r in pooled_r[group] if r["actual_T_catch_ns"]))
        aggregate[group] = c
    comparisons, exclusions, fault_diffs, task_deltas = {}, {}, {}, []
    for scope in [str(r) for r in RUN["RUNS"]] + ["pooled"]:
        a = pooled_a if scope == "pooled" else accounts[scope]
        r = pooled_r if scope == "pooled" else recoveries[scope]
        comparisons[scope], exclusions[scope] = {}, {}
        for labels in PAIRS:
            reference, candidate = labels
            name = reference + "__" + candidate
            comparisons[scope][name] = contrast(a[reference], a[candidate], r[reference], r[candidate], labels)
            exclusions[scope][name] = leave_one_out(a[reference], a[candidate], r[reference], r[candidate], labels)
            if scope != "pooled":
                fault_diffs.setdefault(scope, {})[name] = dict(
                    reference_only=sorted(faults[scope][reference]-faults[scope][candidate]),
                    candidate_only=sorted(faults[scope][candidate]-faults[scope][reference]))
                for key in sorted(a[reference], key=instance_key):
                    task_deltas.append(dict(run=int(scope), task_id=instance_key(key)[1], reference=reference,
                        candidate=candidate, reference_completed=a[reference][key]["completed"],
                        candidate_completed=a[candidate][key]["completed"], **{
                            f"delta_{field}": a[candidate][key][field]-a[reference][key][field] for field in FIELDS}))
    for name, value in (("audit-results", results), ("aggregate", aggregate), ("paired-comparison", comparisons),
        ("leave-one-out", exclusions), ("fault-differences", fault_diffs), ("rational-snapshot-summary", snapshots)):
        (root / (name + ".json")).write_text(json.dumps(value, indent=2) + "\n")
    OLD["write_csv"](root / "summary.csv", table)
    OLD["write_csv"](root / "per-task-comparison.csv", task_deltas)
    status = dict(status="RATIONAL_MULTIRUN_AUDIT_PASS", groups=15, new_executions=4, reused_executions=11,
        independent_history=independent, exact_workload_signature=True,
        notes=["Online fault configuration and streams fixed, not necessarily realized fault events.",
               "HHI is mean of five per-run HHIs, not concentration of pooled assignments.",
               "Leave-one-out removes one (run,task) from both sides without resimulation; HHI/link exposure unchanged.",
               "Missing catches excluded from paired latency, never imputed zero; whole completion remains reported."],
        next_action="STOP_FOR_USER_REVIEW", default_promotion=False, ci_or_merge=False)
    (root / "audit-status.json").write_text(json.dumps(status, indent=2) + "\n")
    return status


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=RUN["ROOT"] / "output/n5c-rational-multirun")
    print(json.dumps(audit(parser.parse_args().root), indent=2))
