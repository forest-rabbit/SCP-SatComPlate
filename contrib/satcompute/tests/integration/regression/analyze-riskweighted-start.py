#!/usr/bin/env python3
"""Audit only new R4–R7: risk-weighted START, 400 WU/token with total WU conserved."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import runpy
import sys

HERE = Path(__file__).resolve().parent
API = runpy.run_path(str(HERE / "analyze-input-deferred.py"))
BASE = API["BASE"]
rows, require, number = API["rows"], API["require"], API["number"]
sys.path.insert(0, str(HERE.parents[2] / "tools/generation"))
from task_workload_model import image_budget


def near(actual, expected, message):
    require(math.isclose(actual, expected, rel_tol=1e-10, abs_tol=1e-10), message)


def variable_bytes(task):
    if task["task_profile"] == "llm":
        require(number(task, "compute_work_units") % 400 == 0, "LLM partial total token")
        return number(task, "compute_work_units") // 400 * 114688
    return image_budget(task["task_profile"], number(task, "input_bytes"), task["task_id"]).k_variable_bytes


def decision_check(row, task, deferred):
    """Independent score reconstruction; no outcome labels or second risk predictor."""
    if not row["selected_score"]:
        require(row["proposed_action"] != "START", "START without feasible candidate")
        return False
    cL, cR = ((.0001, .0005) if variable_bytes(task) <= 100000000 else
              (.0005, .002) if variable_bytes(task) <= 500000000 else (.002, .008))
    work = number(task, "compute_work_units")
    rate = float(row["recovery_rate"])
    delta, n = float(row["proposed_delta_permille"]) / 1000, int(row["proposed_n"])
    x = float(row["progress_ratio"])
    input_s = number(task, "input_bytes") / float(row["input_bandwidth_bytes_per_s"]) if row["replay_available"] == "1" else 0
    recovery = variable_bytes(task) * (n-1)*delta/(2*float(row["backup_bandwidth_bytes_per_s"])) + cR*(n-1)/n + work*delta/(2*rate)
    near(float(row["predicted_recovery_s"]), recovery, "Rbar changed")
    require(recovery + (input_s if deferred else 0) <= float(row["rmax_s"]) + 1e-10,
            "INPUT omitted from hard deadline")
    if row["phase_before"] == "ON":
        require(not row["p_fail_after_init_ready"] and not row["representative_progress_after_ready"],
                "START-window risk leaked into ON")
        normal = number(task, "compute_rate_work_units_per_second")/work*(cL/delta+cR/(n*delta))
        near(float(row["predicted_normal_s"]), normal, "ON one-second maintenance changed")
        near(float(row["selected_score"]), normal + float(row["q_current_sample"])*recovery,
             "ON current-q objective changed")
        return True
    require(row["phase_before"] == "OFF", "scored nondecision phase")
    p, ready = float(row["p_fail_before_finish"]), float(row["p_fail_after_init_ready"])
    require(0 <= ready <= p + 1e-12 <= 1 + 1e-12, "ready mass outside finish mass")
    ready_ns = int(row["init_ready_time_ns"])
    near(ready_ns/1e9, int(row["fault_epoch_time_ns"])/1e9 + float(row["t_init_s"]),
         "ready timestamp disagrees with initialization")
    if ready:
        representative = float(row["representative_progress_after_ready"])
        require(x <= representative <= 1, "representative progress is not causal")
    else:
        require(not row["representative_progress_after_ready"] and row["proposed_action"] != "START",
                "zero ready mass invented a representative progress or START")
        representative = 0
    if row["replay_available"] == "1" or ready == 0:
        score = ready * (representative*work/rate + (0 if deferred else input_s))
        near(float(row["j_off_start_window"]), score, "incorrect ready-window OFF loss")
        near(float(row["j_off"]), score, "public Joff differs from START-window loss")
    normal = (1-x)*(cL/delta+cR/(n*delta))
    near(float(row["predicted_normal_s"]), normal, "remaining maintenance was discounted")
    near(float(row["selected_score"]), normal+ready*recovery, "START uses wrong probability")
    near(float(row["j_start"]), cL+cR+normal+ready*recovery, "START double-counted shared INPUT")
    if row["proposed_action"] == "START":
        require(ready > 0 and (not row["j_off"] or float(row["j_start"]) < float(row["j_off"])),
                "START without strict positive expected benefit")
        require(float(row["t_init_s"]) < work*(1-x)/number(task, "compute_rate_work_units_per_second"),
                "initialization too late")
    return True


def start_metrics(root, result):
    tasks = {t["task_id"]: t for t in rows(root, "task-summary.csv")}
    decisions = rows(root, "frequency-decisions.csv")
    protected = {t["task_id"]: t for t in rows(root, "protection-task-summary.csv")}
    deferred = result["execution"]["input_staging_policy"] == "deferred"
    checked = sum(decision_check(d, tasks[d["task_id"]], deferred) for d in decisions)
    for key, value in protected.items():
        require(number(value, "variable_state_bytes") == variable_bytes(tasks[key]), "runtime state uses stale LLM mapping")
    off = [d for d in decisions if d["phase_before"] == "OFF"]
    starts = [d for d in off if d["proposed_action"] == "START"]
    commits = [d for d in starts if d["decision_committed"] == "1"]
    require({d["task_id"] for d in commits} == set(protected), "committed START/protection ledger mismatch")
    configs = [d for d in decisions if d["decision_committed"] == "1" and d["proposed_action"] in ("START", "UPDATE")]
    events = rows(root, "protection-events.csv")
    recoveries = result["recovery_rows"]
    for profile, summary in result["profiles"].items():
        subset = [t for t in tasks.values() if t["task_profile"] == profile]
        count = sum(tasks[k]["task_profile"] == profile for k in protected)
        summary.update(average_work_units=sum(number(t, "compute_work_units") for t in subset)/len(subset),
            fault_victims=sum(tasks[r["task_id"]]["task_profile"] == profile for r in recoveries),
            protected_count=count, start_rate=count/len(subset),
            **result["staging"]["per_profile"][profile])
    anchors = {}
    for key in ("120", "399", "596", "574"):
        fault = next((r for r in recoveries if r["task_id"] == key), None)
        initialization = [e for e in events if e["task_id"] == key and e["event"] == "INIT_COMPLETE"]
        anchors[key] = dict(task=tasks[key], decisions=[d for d in decisions if d["task_id"] == key],
            recovery=fault, initialization_commits=initialization,
            initialized_strictly_before_fault=bool(fault and any(int(e["time_ns"]) < int(fault["fault_time_ns"]) for e in initialization)))
    return dict(score_rows_checked=checked, off_decisions=len(off), start_proposed=len(starts),
        start_committed=len(commits), protected_tasks=len(protected),
        protected_llm=sum(tasks[k]["task_profile"] == "llm" for k in protected),
        initializing_at_fault=sum(r["phase_at_fault"] == "INITIALIZING" for r in recoveries),
        on_at_fault=sum(r["phase_at_fault"] == "ON" for r in recoveries),
        off_distributions={k: BASE["stats"](float(d[k]) for d in off if d[k]) for k in
            ("p_fail_before_finish", "p_fail_after_init_ready", "progress_ratio", "representative_progress_after_ready", "t_init_s")},
        committed_delta_permille=BASE["stats"](int(d["committed_delta_permille"]) for d in configs),
        committed_n=BASE["stats"](int(d["committed_n"]) for d in configs), anchors=anchors)


def fairness(runs, directories):
    API["fairness"](runs)
    require(len({r["execution"]["commit"] for r in runs.values()}) == 1, "new R4–R7 code differs")
    definitions = []
    for name, root in directories.items():
        tasks = rows(root, "task-summary.csv")
        definitions.append([{k:t[k] for k in ("task_id", "task_profile", "arrival_time_ns", "source_node_id",
            "compute_node_id", "result_node_id", "input_bytes", "output_bytes", "compute_work_units",
            "compute_rate_work_units_per_second")} for t in tasks])
        llm = [t for t in tasks if t["task_profile"] == "llm"]
        require(sum(number(t, "compute_work_units") for t in tasks) == 352513119 and
                sum(number(t, "compute_work_units") for t in llm) == 61333200 and
                sum(variable_bytes(t) for t in llm) == 17585455104, "total WU or 400 WU/token not conserved")
        require(sum(number(t, "input_bytes") for t in tasks) == 194119753287 and
                sum(number(t, "output_bytes") for t in tasks) == 100166291859, "wrong regenerated byte budgets")
    require(all(d == definitions[0] for d in definitions), "new groups have different workload")
    return dict(same_code=True, same_nonpolicy_controls=True, same_workload=True, total_work_units=352513119,
        llm_work_units=61333200, llm_tokens=153333, llm_work_units_per_token=400,
        nominal_total_compute_time_unchanged=True, individual_llm_rounding_limit_ns=2000000,
        old_eager_reused=False, old_R0_R3_strict_comparison=False, same_realized_fault_trace_required=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("r4", "r5", "r6", "r7"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    directories = {k.upper(): getattr(args, k).resolve() for k in ("r4", "r5", "r6", "r7")}
    require(not args.output.exists() and not any(args.output.resolve().is_relative_to(p) for p in directories.values()),
            "refusing to overwrite evidence or write into raw runs")
    runs = {}
    for name, root in directories.items():
        result = BASE["analyze"](root)
        result["staging"] = API["staging_metrics"](root, result)
        result["start"] = start_metrics(root, result)
        runs[name] = result
    result = dict(model="risk-weighted START / 400 WU per token / conserved total WU", runs=runs,
        fairness=fairness(runs, directories),
        comparisons={f"{a}_to_{b}":API["comparison"](runs[a], runs[b]) for a,b in
                     (("R4","R6"),("R5","R7"),("R4","R5"),("R6","R7"))},
        busy_comparisons={f"{a}_to_{b}":BASE["busy_comparison"](runs[a],runs[b]) for a,b in (("R4","R5"),("R6","R7"))},
        known_limitation="ON/PAUSE resource contention may not immediately re-place/resume; task 574 is recorded only. Deferred to N5C.",
        status="STOPPED AT PRE-N5C RISK-WEIGHTED START / LLM-4X / R4-R7 AUDIT")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({"fairness":result["fairness"], "comparisons":result["comparisons"]}, indent=2))


if __name__ == "__main__":
    main()
