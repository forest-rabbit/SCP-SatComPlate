#!/usr/bin/env python3
"""N5C V4 causal-placement audit; reuse corrected actual-WU and network accounting."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
BASE = runpy.run_path(str(HERE / "analyze-baseline-evaluation.py"))
FREQ = runpy.run_path(str(HERE / "analyze-frequency-evaluation.py"))
RISK = runpy.run_path(str(HERE / "analyze-riskweighted-start.py"))
rows, require = BASE["rows"], BASE["require"]
NS = 10**9


def distribution(values):
    values = sorted(values)
    total = sum(values)
    result = FREQ["stats"](values)
    def q(p):
        if not values:
            return None
        x = (len(values)-1)*p
        return values[math.floor(x)] + (values[math.ceil(x)]-values[math.floor(x)])*(x-math.floor(x))
    result.update(p95=q(.95), top1_share=max(values, default=0)/total if total else None,
                  top5_share=sum(values[-5:])/total if total else None,
                  hhi=sum((v/total)**2 for v in values) if total else None,
                  gini=sum((2*i-len(values)-1)*v for i,v in enumerate(values,1))/(len(values)*total) if total else None,
                  max_to_mean=max(values)*len(values)/total if total else None)
    return result


def near(a, b, label):
    require(math.isclose(a, b, rel_tol=1e-10, abs_tol=1e-10), label)


def resources(root):
    data = rows(root, "placement-resource-summary.csv")
    pools = {r["node_id"]:r for r in rows(root, "protection-node-storage-summary.csv")}
    end = json.loads((root/"run-summary.json").read_text())["simulation_duration_ns"]
    counts, active, peaks, last, integral = (defaultdict(int) for _ in range(5))
    owners = {}
    for e in rows(root, "placement-load-events.csv"):
        if not e["event"].startswith("ASSIGNMENT"):
            continue
        node, task, time = e["node_id"], e["task_id"], int(e["time_ns"])
        integral[node] += (time-last[node])*active[node]
        last[node] = time
        if e["event"] == "ASSIGNMENT_ESTABLISHED":
            require(task not in owners, "duplicate assignment")
            owners[task] = node
            counts[node] += 1
            active[node] += 1
            peaks[node] = max(peaks[node], active[node])
        else:
            require(owners.pop(task, None) == node, "unowned release")
            active[node] -= 1
    require(not owners and not any(active.values()), "final live assignment")
    require({r["node_id"] for r in data} == set(pools), "resource observations omitted zero nodes")
    for r in data:
        node = r["node_id"]
        require(int(r["backup_assignment_count"]) == counts[node] and
                int(r["peak_active_backups"]) == peaks[node] and
                int(r["backup_assignment_time_integral_ns"]) == integral[node], "assignment integral mismatch")
        require(int(r["active_backups"]) == int(r["storage_bytes"]) == 0, "resource leak")
        require(int(r["peak_backup_storage_bytes"]) == int(pools[node]["peak_total_bytes"]), "physical peak mismatch")
        storage_integral = int(r["backup_storage_time_integral_byte_ns"])
        require(0 <= storage_integral <= int(r["peak_backup_storage_bytes"])*end, "physical integral bound")
        near(float(r["mean_backup_storage_bytes"]), storage_integral/end, "physical mean mismatch")
        near(float(r["mean_active_backups"]), integral[node]/end, "active mean mismatch")
        busy = int(r["normal_busy_ns"])+int(r["recovery_busy_ns"])
        exposure = int(r["survival_exposure_ns"])
        require(0 <= busy <= exposure <= end, "history inconsistent survival exposure")
        near(float(r["historical_utilization"]), busy/exposure if exposure else 0, "history division")
    return {key:distribution([int(r[key]) for r in data]) for key in (
        "backup_assignment_count", "backup_assignment_time_integral_ns", "peak_active_backups",
        "peak_backup_storage_bytes", "backup_storage_time_integral_byte_ns")}


def spatial(root):
    decisions = rows(root, "n5c-placement-decisions.csv", True)
    recent_rows = rows(root, "n5c-recent-u-history.csv", True)
    recent = {(r["decision_id"], r["candidate_node"]):r for r in recent_rows}
    require(len(recent) == len(recent_rows), "duplicate recent-U history key")
    expected = {(r["decision_id"], r["candidate_node"]) for r in decisions if r["variant"] == "recent-U"}
    require(set(recent) == expected, "missing or unexpected recent-U companion rows")
    rational_rows = rows(root, "n5c-rational-u-history.csv", True)
    rational = {(r["decision_id"], r["candidate_node"]):r for r in rational_rows}
    expected_rat = {(r["decision_id"], r["candidate_node"]) for r in decisions if r["variant"] == "rational-U"}
    require(len(rational) == len(rational_rows) and set(rational) == expected_rat,
            "missing/duplicate/unexpected Rational-U companion rows")
    groups = defaultdict(list)
    for r in decisions:
        groups[r["decision_id"]].append(r)
    common = rows(root, "frequency-decisions.csv")
    keyed = {(r["task_id"],r["fault_epoch_time_ns"],r["decision_trigger"]):r for r in common}
    selected, candidates, feasible_sizes = [], [], []
    for items in groups.values():
        first = items[0]
        row = keyed[(first["task_id"],first["time_ns"],first["trigger"])]
        require(row["phase_before"] == "OFF" and row["pair_hard_checked"] == "1", "N5C reran ON/Frequency")
        require(first["reference_local_node"] == row["local_node"] and
                first["delta_permille"] == row["proposed_delta_permille"] and
                first["batch_n"] == row["proposed_n"], "N5C changed local or temporal config")
        legal = [r for r in items if r["feasible"] == "1"]
        feasible_sizes.append(len(legal))
        require(all(int(r["feasible_candidate_count"]) == len(legal) for r in items), "candidate count mismatch")
        for r in legal:
            R,U,M = (float(r[k]) for k in ("recovery_conflict","historical_utilization","storage_pressure"))
            require(all(0 <= v <= 1 for v in (R,U,M)), "pressure outside unit interval")
            busy = int(r["normal_busy_ns"])+int(r["recovery_busy_ns"])
            exposure = int(r["exposure_ns"])
            require(0 <= busy <= exposure, "candidate exposure mismatch")
            near(U,busy/exposure if exposure else 0,"candidate history")
            if r["variant"] == "recent-U":
                window = recent[(r["decision_id"], r["candidate_node"])]
                recent_busy = int(window["normal_busy_ns"]) + int(window["recovery_busy_ns"])
                recent_exposure = int(window["exposure_ns"])
                require(0 <= recent_busy <= recent_exposure, "recent history exposure mismatch")
                U = float(window["recent_utilization"])
                near(U, recent_busy/recent_exposure if recent_exposure else 0, "recent candidate history")
                require(r["history_unavailable"] == window["history_unavailable"] == str(int(not recent_exposure)),
                        "recent unavailable diagnostic mismatch")
            if r["variant"] == "rational-U":
                raw = rational[(r["decision_id"], r["candidate_node"])]
                H, I = int(raw["horizon_ns"]), int(raw["continuous_idle_ns"])
                require(raw["task_id"] == r["task_id"] and raw["time_ns"] == r["time_ns"] and
                        H > 0 and 0 <= I <= int(r["time_ns"]), "Rational-U identity/time domain")
                near(float(raw["cumulative_utilization"]), U, "Rational-U overwrote global history")
                near(float(raw["freshness"]), H/(H+I), "Rational-U freshness")
                near(float(raw["rational_pressure"]), U*H/(H+I), "Rational-U pressure")
                require(raw["history_unavailable"] == r["history_unavailable"] == str(int(not exposure)),
                        "Rational-U missing history diagnostic")
                U = float(raw["rational_pressure"])
            near(M,(int(r["actual_plus_quota_bytes"])+int(r["additional_quota_bytes"]))/int(r["capacity_bytes"]),"candidate storage")
            demand, numerator = float(r["first_failure_demand_probability"]), float(r["weighted_conflict"])
            require(0 <= numerator <= demand <= 1, "unconditional first-failure mass invalid")
            near(R,numerator/demand if demand else 0,"conflict normalization")
            near(float(r["bottleneck"]),max(v for k,v in (("noR",R),("noU",U),("noM",M)) if r["variant"] != k),"min-max variant")
            candidates.append(r)
        chosen = [r for r in items if r["selected"] == "1"]
        require(len(chosen) == int(bool(legal)), "selected infeasible/multiple candidate")
        if legal:
            winner = min(legal,key=lambda r:(float(r["bottleneck"]),int(r["propagation_ns"]),int(r["candidate_node"])))
            require(chosen[0] == winner and winner["candidate_node"] == row["remote_node"], "wrong deterministic winner")
            selected.append(winner)
    for row in common:
        if row["phase_before"] == "OFF" and int(row.get("pair_path_feasible") or 0):
            require(row["pair_hard_checked"] == "1", "reference solve count changed")
    return dict(proposals=len(groups), feasible_candidate_count=distribution(feasible_sizes),
                committed=sum(r["committed"] == "1" for r in selected),
                changed_from_reference=sum(r["candidate_node"] != r["reference_remote_node"] for r in selected),
                selected_dominant=dict(Counter(r["dominant_dimension"] for r in selected)),
                ties=dict(Counter(r["tie_break"] for r in selected)),
                raw_feasible={k:distribution(float(r[k]) for r in candidates) for k in (
                    "recovery_conflict","historical_utilization","storage_pressure")})


def recovery_composition(recoveries, transfers):
    """Attempts and outcomes overlap; logical relocated state is not physical traffic."""
    total = len(recoveries)
    counts = dict(direct_count=0, relocate_count=0, recompute_count=0, unselected_count=0)
    migrated = set()
    for r in recoveries:
        path = r["chosen_path"]
        if path in ("TAIL", "REMOTE_REDO"):
            counts["direct_count"] += 1
        elif path in ("MIGRATE_TAIL", "MIGRATE_REDO"):
            counts["relocate_count"] += 1
            migrated.add((r["task_id"], r["attempt_generation"]))
        elif path == "RECOMPUTE":
            counts["recompute_count"] += 1
        else:
            counts["unselected_count"] += 1
    counts["recovery_failed_count"] = sum(r["terminal_state"] == "FAILED" for r in recoveries)
    sent = dict.fromkeys(("RECOVERY_INPUT", "RECOVERY_STATE", "RECOVERY_TAIL"), 0)
    flows = {}
    for r in transfers:
        if (r["task_id"], r["attempt_generation"]) not in migrated or r["kind"] not in sent:
            continue
        value = (r["kind"], int(r["sent_bytes"]))
        require(value[1] >= 0, "negative migration traffic")
        require(r["transfer_id"] not in flows or flows[r["transfer_id"]] == value,
                "duplicate migration flow evidence disagrees")
        flows[r["transfer_id"]] = value
    for kind, size in flows.values():
        sent[kind] += size
    return dict(**counts,
        action_ratios={k.removesuffix("_count"):v/total if total else None for k,v in counts.items()},
        relocated_state_logical_bytes=sum(int(r["checkpoint_relocation_bytes"]) for r in recoveries),
        migration_sent_bytes_by_kind=sent, migration_total_sent_bytes=sum(sent.values()),
        migration_traffic_note="Actual sent payload for MIGRATE_* attempts, including partial/cancelled flows; "
            "INPUT belongs to this operation but is not all uniquely incremental versus direct recovery. "
            "LocalDelivery has no physical flow. Failed outcomes can overlap any action category.")


def analyze(root, include_tasks=False):
    value = BASE["analyze"](root)  # Corrected successful useful-WU subtraction and failed-task waste.
    value["resource_concentration"] = resources(root)
    recoveries = value["recovery_rows"]
    # Only a physically designated backup at this fault belongs to the busy denominator.
    assigned = [r for r in recoveries if r["remote_node"] and r["phase_at_fault"] in ("ON","INITIALIZING")]
    busy = sum(r["remote_busy_at_fault"] == "1" for r in assigned)
    value["placement_recovery"] = dict(
        recovery_attempted=len(recoveries), paths=dict(Counter(r["chosen_path"] for r in recoveries)),
        path_ratios={k:v/len(recoveries) for k,v in Counter(r["chosen_path"] for r in recoveries).items()},
        designated_at_fault=len(assigned), backup_busy_at_fault=busy,
        backup_busy_ratio=busy/len(assigned) if assigned else None,
        unavailable_or_fallback=dict(Counter(r["checkpoint_fallback_reason"] for r in recoveries)),
        catch_seconds=distribution(int(r["actual_T_catch_ns"])/NS for r in recoveries if r["actual_T_catch_ns"]),
        resume_seconds=distribution((int(r["recovery_compute_start_time_ns"])-int(r["fault_time_ns"]))/NS
                                    for r in recoveries if r["recovery_compute_start_time_ns"]),
        without_catch=sum(not r["actual_T_catch_ns"] for r in recoveries),
        **recovery_composition(recoveries, rows(root,"protection-transfers.csv",True)))
    value["links"]["max_single_link_full_mean_utilization_percent"] = max(
        100*float(r["tx_busy_time_s"])/float(r["measurement_duration_s"]) for r in rows(root,"link-summary.csv"))
    tasks = {r["task_id"]:r for r in rows(root,"task-summary.csv")}
    common = rows(root,"frequency-decisions.csv")
    FREQ["verify_pair_retries"](common, rows(root,"frequency-capacity-waits.csv"))
    value["frequency_rows_independently_checked"] = sum(RISK["decision_check"](
        r,tasks[r["task_id"]],value["execution"]["input_staging_policy"] == "deferred") for r in common)
    value["n5c"] = spatial(root) if value["execution"]["placement_mode"] == "n5c" else None
    for key in (("recovery_rows",) if include_tasks else ("task_rows", "recovery_rows")):
        value.pop(key, None)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument("--root",type=Path)
    choice.add_argument("--fixtures",type=Path,help="Only audit maintained small runtime fixtures")
    args = parser.parse_args()
    if args.fixtures:
        groups = ["online-n5c","online-n5c-deferred","online-n5c-recent-U","online-n5c-rational-U"] + [f"n5c-boundary-{mode}-{case}"
                  for mode in ("eager","deferred") for case in ("normal","hit","race")]
        results = {name:spatial(args.fixtures/name) for name in groups}
        require(all(r["proposals"] > 0 for r in results.values()),"vacuous spatial fixture")
        print(json.dumps({"status":"N5C_FIXTURE_AUDIT_PASS","groups":len(results),
                          "proposals":sum(r["proposals"] for r in results.values())},sort_keys=True))
        return
    results = {p.name:analyze(p) for p in sorted(args.root.iterdir()) if p.is_dir() and (p/"execution-result.json").exists()}
    require(results,"no completed runs")
    (args.root/"comparison.json").write_text(json.dumps(results,indent=2)+"\n")
    table = []
    for group,r in results.items():
        s,p = r["summary"],r["placement_recovery"]
        table.append(dict(group=group,completed=s["completed"],deadline_miss=s["deadline_miss"],
            recovery_attempted=p["recovery_attempted"],backup_busy=p["backup_busy_at_fault"],
            designated_at_fault=p["designated_at_fault"],catch_mean_s=p["catch_seconds"]["mean"],
            catch_p50_s=p["catch_seconds"]["p50"],catch_p95_s=p["catch_seconds"]["p95"],
            resume_mean_s=p["resume_seconds"]["mean"],
            total_capacity_equivalent_waste_eq_wu=s["w_waste_actual"],
            actual_execution_waste_wu=s["task_execution_waste_wu"],
            normal_protection_eq_wu=s["normal_protection_eq_wu"],reserved_idle_eq_wu=s["reserved_idle_eq_wu"],
            extra_application_sent_bytes=r["network"]["extra_sent_bytes"],
            direct_count=p["direct_count"],relocate_count=p["relocate_count"],
            recompute_count=p["recompute_count"],recovery_failed_count=p["recovery_failed_count"],
            relocated_state_logical_bytes=p["relocated_state_logical_bytes"],
            migration_total_sent_bytes=p["migration_total_sent_bytes"],
            mean_link_utilization_percent=r["links"]["mean_utilization_percent"]))
    with (args.root/"comparison.csv").open("w") as stream:
        writer=csv.DictWriter(stream,fieldnames=table[0])
        writer.writeheader(); writer.writerows(table)
    print(json.dumps({"status":"AUDIT_PASS","groups":list(results)},sort_keys=True))


if __name__ == "__main__":
    main()
