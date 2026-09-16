#!/usr/bin/env python3
"""Audit G4 zero side effects before summarizing analytical protection costs (no simulation)."""
import argparse
from collections import Counter, defaultdict, deque
import csv
import json
import math
from pathlib import Path

BUSINESS_CSV = (
    "task-summary.csv", "task-events.csv", "transfer-summary.csv", "compute-node-summary.csv",
    "fault-events.csv", "fault-task-impact.csv", "ecmp-route-events.csv",
    "size-aware-reservation-events.csv", "network-flow-metrics.csv", "network-flow-details.csv",
    "link-summary.csv", "link-window-metrics.csv", "network-link-window-metrics.csv",
    "fault-predictions.csv", "fault-model-probabilities.csv", "fault-model-state.csv",
)
BUSINESS_JSON = (
    "run-summary.json", "fault-trace.json", "fault-summary.json", "capacity-aware-summary.json",
    "size-aware-summary.json", "fault-prediction-summary.json",
)
REQUIRED = {"task-summary.csv", "task-events.csv", "transfer-summary.csv", "fault-events.csv",
            "run-summary.json", "fault-trace.json", "fault-summary.json"}


def rows(directory, filename):
    with (directory / filename).open(newline="") as stream:
        return list(csv.DictReader(stream))


def compare_business(reference, candidate):
    """Compare structured values, not a digest; ignore only run wall-clock fields."""
    details = {}
    for filename in (*BUSINESS_CSV, *BUSINESS_JSON):
        left, right = reference / filename, candidate / filename
        if not left.exists() and not right.exists() and filename not in REQUIRED:
            continue
        if not left.is_file() or not right.is_file():
            details[filename] = {"match": False, "reason": "missing output"}
            continue
        if filename.endswith(".csv"):
            # Fast byte comparison also retains empty/header-only files faithfully.
            same = left.read_bytes() == right.read_bytes()
            details[filename] = {"match": same, "comparison": "exact bytes"}
        else:
            a, b = json.loads(left.read_text()), json.loads(right.read_text())
            ignored = ("wall_clock_ns", "wall_clock_s") if filename == "run-summary.json" else ()
            for key in ignored:
                a.pop(key, None); b.pop(key, None)
            details[filename] = {"match": a == b, "ignored_keys": ignored}
            if a != b and isinstance(a, dict) and isinstance(b, dict):
                details[filename]["different_keys"] = [key for key in a.keys() | b.keys() if a.get(key) != b.get(key)]
    return {"pass": all(value["match"] for value in details.values()),
            "reference": str(reference), "candidate": str(candidate), "files": details}


def number(row, field):
    return float(row[field])


def truth(row, field):
    return row.get(field) == "true"


def distribution(values):
    data = sorted(float(v) for v in values)
    if not data:
        return {"count": 0}
    def quantile(p):
        index = (len(data) - 1) * p
        lo, hi = math.floor(index), math.ceil(index)
        return data[lo] + (data[hi] - data[lo]) * (index - lo)
    return {"count": len(data), "min": data[0], "P10": quantile(.1), "P50": quantile(.5),
            "P90": quantile(.9), "max": data[-1], "mean": sum(data) / len(data)}


def near(a, b, message):
    if not math.isclose(a, b, rel_tol=1e-11, abs_tol=1e-8):
        raise ValueError(f"{message}: {a} != {b}")


def audit_score(row, task):
    """Independent enumeration of every scored decision, using recorded causal inputs."""
    if row["mode_before"] not in ("OFF", "ON"):
        return
    work, k = number(task, "W"), number(task, "K_variable")
    rate = number(task, "compute_rate_wu_per_s")
    cl, cr = number(row, "cL_s"), number(row, "cR_s")
    x, q, p = number(row, "progress"), number(row, "q_comp_1s"), number(row, "p_finish")
    on = row["mode_before"] == "ON"
    best, count = None, 0
    for d in range(10, 101):
        delta = d / 1000
        for n in range(1, min(100, 1000 // d) + 1):
            catch = k * (n - 1) * delta / (2 * 1.25e9) + cr * (n - 1) / n + work * delta / (2 * rate)
            if catch > number(row, "r_max_s"):
                continue
            count += 1
            maintenance = cl / delta + cr / (n * delta)
            j = rate / work * maintenance + q * catch if on else (1 - x) * maintenance + p * catch
            item = (j, d, n)
            if best is None or item < best: best = item
    if count != int(row["feasible_candidate_count"]): raise ValueError("feasible candidate count differs")
    if best is None:
        if row["best_delta"]: raise ValueError("empty feasible set has a selected candidate")
        return
    near(number(row, "best_delta"), best[1] / 1000, "globally optimal delta")
    if int(row["best_n"]) != best[2]: raise ValueError("globally optimal n differs")
    near(number(row, "j_on_s" if on else "j_start_s"), best[0] + (0 if on else cl + cr), "objective")
    j_off = p * (number(task, "input_bytes") / 1.25e9 + x * work / rate)
    near(number(row, "j_off_s"), j_off, "OFF objective")
    if not on:
        start = (cl + cr) + best[0] < j_off and number(row, "t_init_s") < number(row, "remaining_compute_s")
        if truth(row, "start_triggered") != start: raise ValueError("strict START condition differs")


def audit_virtual(shadow):
    """Reconstruct completed state/queues from the event ledger, never adjust data."""
    tasks = {r["task_id"]: r for r in rows(shadow, "shadow-task-summary.csv")}
    state = defaultdict(lambda: {"l": 0, "r": 0, "nl": 0, "nr": 0, "pending": deque(),
                                  "remote_tail": 0, "batches": {}, "stopped": False, "triggered": set()})
    event_count = 0
    for event in rows(shadow, "shadow-events.csv"):
        event_count += 1
        s, task = state[event["task_id"]], tasks[event["task_id"]]
        time, kind = int(event["time_ns"]), event["event"]
        if s["stopped"]:
            raise ValueError("shadow operation after real compute terminal")
        if kind == "ON":
            s["l"], s["r"] = int(event["local_work"]), int(event["remote_work"])
            if s["l"] != s["r"] or s["l"] != int(task["initial_legal_work"]):
                raise ValueError("initial protection state differs from captured START")
        elif kind == "L1_TRIGGER":
            work = int(event["target_work"])
            if work in s["triggered"]: raise ValueError("duplicate L1 checkpoint")
            s["triggered"].add(work)
        elif kind == "L1_DONE":
            work = int(event["local_work"])
            if work <= s["l"] or int(event["previous_local_work"]) != s["l"]:
                raise ValueError("nonmonotonic local state")
            if time < int(event["trigger_time_ns"]) + number(task, "cL_s") * 1e9 - 1e-5:
                raise ValueError("L1 effective before cL")
            k, w = int(task["K_variable"]), int(task["W"])
            if task["task_profile"] == "llm":
                if k <= 0 or k % 114688 or w % (k // 114688):
                    raise ValueError("invalid whole-token KV/WU ledger")
                per_token = w // (k // 114688)
                state_bytes = lambda x: x // per_token * 114688
            else:
                state_bytes = lambda x: k * x // w
            delta_bytes = state_bytes(work) - state_bytes(s["l"])
            if delta_bytes != int(event["delta_state_bytes_actual"]):
                raise ValueError("L1 incremental bytes differ from G1 state")
            near(number(event, "delta_progress_actual"), (work - s["l"]) / w, "L1 actual progress")
            s["pending"].append((work, delta_bytes)); s["l"] = work; s["nl"] += 1
        elif kind == "REMOTE_BATCH":
            n = int(event["n_at_creation"])
            if len(s["pending"]) < n: raise ValueError("batch formed before n L1 completions")
            local = [s["pending"].popleft() for _ in range(n)]
            if sum(b for _, b in local) != int(event["batch_bytes"]) or local[-1][0] != int(event["batch_end_work"]):
                raise ValueError("batch lost pending L1 bytes or state")
            start, done = int(event["start_time_ns"]), int(event["done_time_ns"])
            if start != max(time, s["remote_tail"]): raise ValueError("virtual remote queue is not serialized")
            near((done - start) / 1e9, int(event["batch_bytes"]) / 1.25e9 + number(task, "cR_s"), "remote duration")
            s["remote_tail"] = done
            s["batches"][event["batch_id"]] = (done, local[-1][0])
        elif kind == "REMOTE_DONE":
            done, work = s["batches"][event["batch_id"]]
            if time != done or int(event["remote_work"]) != work or not s["r"] < work <= s["l"]:
                raise ValueError("remote state advanced before completion or beyond local state")
            s["r"] = work; s["nr"] += 1
        elif kind in ("REAL_COMPUTE_FAILED", "REAL_COMPUTE_COMPLETE"):
            s["stopped"] = True
            s["stop_ns"] = time
    for id, task in tasks.items():
        s = state[id]
        if s["nl"] != int(task["N_L"]) or s["nr"] != int(task["N_R"]):
            raise ValueError("completed checkpoint counts differ from event ledger")
        expected = (number(task, "cL_s") + number(task, "cR_s")) * truth(task, "init_complete")
        expected += s["nl"] * number(task, "cL_s") + s["nr"] * number(task, "cR_s")
        near(number(task, "normal_cost_s"), expected, "v4 normal cost, no initialization double counting")
        near(number(task, "normal_waste_wu"), expected * number(task, "compute_rate_wu_per_s"), "normal WU")
    decisions = rows(shadow, "shadow-decisions.csv")
    by_task = defaultdict(list)
    for row in decisions:
        audit_score(row, tasks[row["task_id"]])
        by_task[row["task_id"]].append(int(row["time_ns"]))
        if not 0 <= number(row, "progress") < 1 or number(row, "remaining_compute_s") <= 0:
            raise ValueError("non-running decision")
        if not truth(row, "query_available"): raise ValueError("running decision has no causal risk query")
        if not 0 <= number(row, "q_comp_1s") <= 1 or not 0 <= number(row, "p_finish") <= 1:
            raise ValueError("invalid decision probability")
        if row["task_id"] in state and int(row["time_ns"]) >= state[row["task_id"]].get("stop_ns", math.inf):
            raise ValueError("decision at/after real terminal event")
        if row["best_delta"]:
            delta, n = number(row, "best_delta"), int(row["best_n"])
            if not .01 <= delta <= .1 or not 1 <= n <= 100 or n * delta > 1 + 1e-12:
                raise ValueError("illegal frequency")
            near(number(row, "best_remote_interval"), delta * n, "remote interval")
    for times in by_task.values():
        if any(b - a != 10**9 for a, b in zip(times, times[1:])):
            raise ValueError("decision grid is not compute-start-relative 1 second")
    return {"pass": True, "task_count": len(tasks), "decision_count": len(decisions),
            "event_count": event_count, "checked": "legal byte deltas, initialization, completed costs, retained pending batches, serial remote queue, no after-terminal decisions"}


def start_workload_and_tiers(tasks, decisions):
    """Offline START workload and full-K cost-tier accounting; no new model evaluation."""
    started = {r["task_id"]: r for r in tasks if truth(r, "ever_start")}
    starts = [r for r in decisions if truth(r, "start_triggered")]
    if len(starts) != len(started) or {r["task_id"] for r in starts} != set(started):
        raise ValueError("START task/decision records are missing or duplicated")
    def describe(group):
        return {"input_bytes": distribution(number(started[r["task_id"]], "input_bytes") for r in group),
                "remaining_compute_s": distribution(number(r, "remaining_compute_s") for r in group)}
    tiers = []
    for label, low, high in (("K<=100MB", 0, 100_000_000),
                             ("100MB<K<=500MB", 100_000_000, 500_000_000),
                             ("K>500MB", 500_000_000, math.inf)):
        group = [r for r in tasks if low < int(r["K_variable"]) <= high or
                 (low == 0 and int(r["K_variable"]) == 0)]
        tiers.append({"cost_tier": label, "task_count": len(group),
                      "start_count": sum(truth(r, "ever_start") for r in group),
                      "on_count": sum(truth(r, "init_complete") for r in group),
                      "normal_waste_wu": sum(number(r, "normal_waste_wu") for r in group)})
    return {"start_workload": describe(starts),
            "start_workload_by_profile": {p: describe([r for r in starts if started[r["task_id"]]["task_profile"] == p])
                for p in sorted({r["task_profile"] for r in tasks})},
            "cost_tiers": tiers}


def summarize(shadow):
    audit = audit_virtual(shadow)
    tasks = rows(shadow, "shadow-task-summary.csv")
    faults = rows(shadow, "shadow-faults.csv")
    primary = [r for r in faults if truth(r, "primary_f1_f2")]
    f3 = [r for r in faults if not truth(r, "primary_f1_f2")]
    started = [r for r in tasks if truth(r, "ever_start")]
    entered = [r for r in tasks if truth(r, "init_complete")]
    decisions = [r for r in rows(shadow, "shadow-decisions.csv") if r["mode_before"] == "ON" and r["best_delta"]]
    fields = ("start_p_finish", "start_q_1s", "start_progress")
    describe_start = lambda data: {f: distribution(number(r, f) for r in data) for f in fields}
    frequencies = lambda data: {f: distribution(number(r, f) for r in data)
                               for f in ("best_delta", "best_n", "best_remote_interval")}
    normal = sum(number(r, "normal_waste_wu") for r in tasks)
    off_waste = sum(number(r, "recompute_total_wu") for r in primary)
    recovery = sum(number(r, "compfrr_recovery_waste_wu") for r in primary)
    for row in faults:
        near(number(row, "recompute_total_wu"), number(row, "recompute_idle_wu") + number(row, "recompute_executed_wu"), "recompute split")
        near(number(row, "compfrr_total_waste_wu"), number(row, "normal_waste_wu") + number(row, "compfrr_recovery_waste_wu"), "lifecycle split")
    bins = (("<5%", 0, .05), ("5-10%", .05, .1), ("10-20%", .1, .2),
            ("20-30%", .2, .3), ("30-50%", .3, .5), (">=50%", .5, 1.000001))
    by_id = {r["task_id"]: r for r in tasks}
    def off_feasible(row):
        remaining = (1 - number(row, "x_f")) * number(row, "work_units") / number(by_id[row["task_id"]], "compute_rate_wu_per_s")
        return number(row, "T_catch_all_off") + remaining <= (int(row["deadline_time_ns"]) - int(row["fault_time"])) / 1e9
    return {"scope": "G4 analytical only; no actual task rescued", "virtual_audit": audit,
        **start_workload_and_tiers(tasks, rows(shadow, "shadow-decisions.csv")),
        "task_count": len(tasks), "never_started": len(tasks) - len(started),
        "start_count": len(started), "initialization_success_count": len(entered),
        "initialization_attempted_s": distribution(number(r, "initialization_duration_attempted_s") for r in started),
        "initialization_fault_miss_count": sum(truth(r, "initialization_fault_miss") for r in tasks),
        "initialization_completion_abort_count": sum(truth(r, "initialization_completion_abort") for r in tasks),
        "start_rate": len(started) / len(tasks), "on_rate": len(entered) / len(tasks),
        "start_state": describe_start(started),
        "start_by_profile": {p: describe_start([r for r in started if r["task_profile"] == p])
                             for p in sorted({r["task_profile"] for r in tasks})},
        "primary_direct_fault_count": len(primary),
        "primary_modes": dict(Counter(r["mode_at_fault"] for r in primary)),
        "primary_by_source": {p: dict(Counter(r["mode_at_fault"] for r in primary if r["fault_type"] == p))
                              for p in ("F1", "F2", "F1+F2")},
        "start_to_fault_s": distribution(number(r, "start_to_fault_s") for r in primary if r["start_to_fault_s"]),
        "on_to_fault_s": distribution(number(r, "on_to_fault_s") for r in primary if r["on_to_fault_s"]),
        "on_frequencies": frequencies(decisions),
        "on_frequencies_by_q": {label: frequencies([r for r in decisions if low <= number(r, "q_comp_1s") < high])
                                for label, low, high in bins},
        "N_L": sum(int(r["N_L"]) for r in tasks), "N_R": sum(int(r["N_R"]) for r in tasks),
        "config_changes_per_task": distribution(number(r, "config_change_count") for r in tasks),
        "config_changes_per_on_task": distribution(number(r, "config_change_count") for r in entered),
        "virtual_storage_peak_per_task": {field: distribution(number(r, field) for r in tasks) for field in (
            "peak_local_materialized_state_bytes", "peak_remote_materialized_state_bytes",
            "peak_local_tail_bytes", "peak_replicated_input_bytes", "peak_remote_inflight_batches")},
        "delta_changes": sum(int(r["delta_change_count"]) for r in tasks),
        "n_changes": sum(int(r["n_change_count"]) for r in tasks),
        "normal_cost_s_all_tasks": sum(number(r, "normal_cost_s") for r in tasks),
        "normal_waste_wu_all_tasks": normal,
        "resource_comparison": {
            "all_off": {"normal_extra_wu": 0, "idle_wu": sum(number(r, "recompute_idle_wu") for r in primary),
                "executed_recovery_wu": sum(number(r, "recompute_executed_wu") for r in primary),
                "recovery_waste_wu": off_waste, "lifecycle_waste_wu": off_waste,
                "catch_up_s": distribution(number(r, "T_catch_all_off") for r in primary)},
            "compfrr_shadow": {"normal_extra_wu": normal,
                "idle_wu": sum(number(r, "W_idle_reserved") for r in primary),
                "executed_recovery_wu": sum(number(r, "compfrr_recovery_waste_wu") - number(r, "W_idle_reserved") for r in primary),
                "recovery_waste_wu": recovery, "lifecycle_waste_wu": normal + recovery,
                "catch_up_s": distribution(number(r, "T_catch_shadow") for r in primary)},
            "recovery_saving_ratio": 1 - recovery / off_waste if off_waste else None,
            "net_lifecycle_saving_ratio": 1 - (normal + recovery) / off_waste if off_waste else None},
        "deadline_feasible_shadow": sum(truth(r, "deadline_feasible_shadow") for r in primary),
        "deadline_feasible_all_off": sum(off_feasible(r) for r in primary),
        "deadline_newly_feasible_shadow": sum(truth(r, "deadline_feasible_shadow") and not off_feasible(r) for r in primary),
        "f3_appendix": f3, "f3_excluded_from_primary_recovery": True,
        "normal_cost_denominator": "all tasks, including F3 victim normal maintenance",
        "limitations": ["Nonbinding candidate nodes, paths, and storage", "10 Gbps analytical transfer, no real congestion",
            "No real reservations or recovery; original failed tasks remain failed", "Not an algorithm performance claim for N5"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", required=True, type=Path)
    parser.add_argument("--reference-dir", required=True, type=Path)
    args = parser.parse_args()
    comparison = compare_business(args.reference_dir, args.run_dir)
    output = args.run_dir / "g4-equivalence.json"
    output.write_text(json.dumps(comparison, indent=2) + "\n")
    if not comparison["pass"]:
        print(json.dumps(comparison, indent=2))
        raise SystemExit("STOP: shadow changed business evidence; no G4 benefit summary generated")
    report = summarize(args.run_dir / "shadow")
    (args.run_dir / "g4-summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"equivalence": True, "summary": str(args.run_dir / "g4-summary.json"),
                      "starts": report["start_count"], "on": report["initialization_success_count"],
                      "primary_modes": report["primary_modes"],
                      "net_saving_ratio": report["resource_comparison"]["net_lifecycle_saving_ratio"]}))


if __name__ == "__main__":
    main()
