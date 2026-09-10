#!/usr/bin/env python3
"""Audit the frozen N4 run offline. Never launch ns-3 or modify source evidence."""

import argparse
from collections import Counter
import csv
from decimal import Decimal
import json
from pathlib import Path
import shlex
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from audit_state import Replay, boolean, distribution, integer, require

ROOT = Path(__file__).resolve().parents[5]
SCENE = ROOT / "contrib/satcompute/input/experiments/leo-66"
BASE = "009788ca9c5042160e50a014c6d657e785f225f3"
RUN_COMMIT = "a78ee0b4d646f5d88195eb80e55bc61b273c0275"
SECOND = 10**9


def read_json(path):
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def csv_rows(path, columns):
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames is not None and len(reader.fieldnames) == len(set(reader.fieldnames)),
                f"missing/duplicate CSV header: {path}")
        require(set(columns.split()) <= set(reader.fieldnames), f"missing columns in {path}")
        for row in reader:
            require(None not in row and None not in row.values(), f"malformed row in {path}")
            yield row


def keyed(rows, field="task_id"):
    result = {}
    for row in rows:
        key = integer(row, field)
        require(key not in result, f"duplicate {field} {key}")
        result[key] = row
    return result


def seconds_ns(value):
    scaled = Decimal(value) * SECOND
    require(scaled.is_finite() and scaled >= 0, "invalid metric time")
    rounded = scaled.to_integral_value()
    require(abs(scaled-rounded) <= Decimal("0.00001"), "metric time loses nanosecond precision")
    return int(rounded)


def validate_task_history(replay, tasks, events, duration):
    """Cross-check task summary timestamps against the independent transition log."""
    fields = {"INPUT_TRANSFERRING": "arrival_time_ns", "QUEUED": "queue_enter_time_ns",
              "RUNNING": "compute_start_time_ns", "RESULT_TRANSFERRING": "compute_complete_time_ns",
              "COMPLETED": "result_transfer_complete_time_ns", "FAILED": "failure_time_ns"}
    for event in events:
        task_id, time = integer(event, "task_id"), integer(event, "simulation_time_ns")
        task = tasks[task_id]
        require(time <= duration and time == integer(task, fields[event["to_state"]]),
                f"task {task_id}: transition/summary timestamp mismatch")
        if event["to_state"] in {"QUEUED", "RUNNING", "RESULT_TRANSFERRING"}:
            require(integer(event, "node_id") == integer(task, "compute_node_id"), "compute event node mismatch")
    for task_id, task in tasks.items():
        require(replay.task_state[task_id].at(duration) == task["final_state"], "task events incomplete")


def validate_identity(source, tasks, shadow, faults, assumptions):
    execution, run = read_json(source / "execution.json"), read_json(source / "run-summary.json")
    require(execution["commit"] == RUN_COMMIT and execution["worktree_dirty"] is False,
            "source is not the clean frozen N4 release run")
    for key, value in {"seed": 1, "run": 11, "fault_mode": "generate", "audit": True,
                       "shadow": True, "simulation_duration_s": 1300, "fixed_delay_seconds": .001}.items():
        require(execution[key] == value, f"execution identity mismatch: {key}")
    arguments = shlex.split(execution["command"][-1])
    options = dict(arg[2:].split("=", 1) for arg in arguments[1:])
    for key, value in {"islBandwidthBps": "10000000000", "fixedDelay": "0.001",
                       "simulationDuration": "1300", "computeDeadlineFactor": "1.3",
                       "randomSeed": "1", "randomRun": "11", "orbitStartOffset": "0",
                       "networkUpdateInterval": "20", "maxIslDistance": "6171353",
                       "faultEnableF1": "1", "faultEnableF2": "1", "faultEnableF3": "1",
                       "faultF3Mode": "controlled", "faultF3Node": "62",
                       "faultF3Time": "1027.055770726", "compfrr-shadow": "1",
                       "faultProbabilityAudit": "1", "faultMode": "generate"}.items():
        require(options[key] == value, f"command identity mismatch: {key}")
    for key, value in {"task_count": 800, "compute_node_count": 66, "completed_task_count": 717,
                       "simulation_duration_ns": 1300*SECOND, "total_input_bytes": 193526895311,
                       "total_output_bytes": 99846517485, "total_compute_work_units": 351623833,
                       "route_computation_count": 2, "flow_monitor_lost_packets": 0,
                       "routing_mode": "global-capacity-aware-hrw"}.items():
        require(run[key] == value, f"run identity mismatch: {key}")
    # Preserve the published scene byte-for-byte, without content digests.
    for relative in ("topology/constellation.csv", "compute/compute-profile.json",
                     "workload/task-trace.json", "workload/workload-summary.json",
                     "placement/placement-manifest.json", "fault/f3-manifest.json"):
        path = SCENE / relative
        original = subprocess.check_output(["git", "show", f"{BASE}:{path.relative_to(ROOT)}"], cwd=ROOT)
        require(path.read_bytes() == original, f"frozen input modified: {path}")
    definitions = keyed(read_json(SCENE / "workload/task-trace.json")["tasks"])
    profiles = read_json(SCENE / "compute/compute-profile.json")["compute_nodes"]
    require({integer(p, "node_id") for p in profiles} == set(range(66)) and len(profiles) == 66,
            "compute-node identity mismatch")
    require(all(integer(p, "compute_rate_work_units_per_second") == 100000 for p in profiles),
            "compute rate mismatch")
    require(set(tasks) == set(shadow) == set(definitions) == set(range(1, 801)), "task identity mismatch")
    for task_id, task in tasks.items():
        for key in ("source_node_id", "compute_node_id", "result_node_id", "input_bytes",
                    "output_bytes", "compute_work_units", "arrival_time_ns"):
            require(integer(task, key) == definitions[task_id][key], f"task {task_id}: {key} mismatch")
        require(task["task_profile"] == definitions[task_id]["task_profile"], "profile mismatch")
        require(integer(task, "compute_rate_work_units_per_second") == 100000, "task rate mismatch")
        require(integer(shadow[task_id], "compute_node_id") == integer(task, "compute_node_id"),
                "shadow primary-node mismatch")
        for actual, expected in (("W", "compute_work_units"), ("input_bytes", "input_bytes")):
            require(integer(shadow[task_id], actual) == integer(task, expected), "shadow workload mismatch")
        require(shadow[task_id]["real_task_state"] == task["final_state"], "shadow terminal mismatch")
    require(Counter(t["final_state"] for t in tasks.values()) == {"COMPLETED": 717, "FAILED": 83},
            "terminal outcome mismatch")
    starts = [f for f in faults if f["event_type"] == "START"]
    require(Counter(f["fault_source"] for f in starts) == {"F1": 84, "F2": 2, "F3": 1},
            "fault-source identity mismatch")
    f3 = next(f for f in starts if f["fault_source"] == "F3")
    # WriteOptionalCsv leaves the duration empty for a permanent fault.
    require((integer(f3, "node_id"), integer(f3, "simulation_time_ns"), f3["duration_ns"])
            == (62, 1027055770726, ""), "controlled F3 mismatch")
    require(sum(boolean(f, "route_recomputed") for f in faults) == 1, "fault route count mismatch")
    recoveries = keyed((f for f in faults if f["event_type"] == "RECOVERY"), "fault_id")
    require(len(recoveries) == 86, "missing/duplicate recovery events")
    for fault in starts:
        time, node, fault_id = integer(fault, "simulation_time_ns"), integer(fault, "node_id"), integer(fault, "fault_id")
        if fault["fault_source"] != "F3":
            recovery = recoveries[fault_id]
            require(integer(fault, "duration_ns") > 0 and integer(recovery, "simulation_time_ns")
                    == time + integer(fault, "duration_ns") and integer(recovery, "node_id") == node,
                    "recovery lifecycle mismatch")
    require(all(0 <= integer(f, "simulation_time_ns") <= 1300*SECOND for f in faults), "fault outside run horizon")
    expected_assumptions = {
        "kind": "shadow / analytical decision evaluation", "bandwidth_bytes_per_s": 1250000000.0,
        "actual_packets_or_cpu_reservations": False, "f3_in_primary_risk_or_recovery": False,
        "node_available": True, "path_available": True, "storage_capacity_binding": False,
        "query_grid": "global fault checks in (now, now+horizon], excludes current draw",
        "same_time_order": "real pre-scheduled model/completion events precede later virtual events",
    }
    require(all(assumptions[k] == v for k, v in expected_assumptions.items()), "shadow assumptions mismatch")
    return {"base_main_commit": BASE, "source_run_commit": RUN_COMMIT, "source_directory": str(source),
            "scene": str(SCENE.relative_to(ROOT)), "tasks": 800, "satellites": 66,
            "seed": 1, "run": 11, "bandwidth_bps": 10**10, "fixed_delay_s": .001,
            "shadow_assumptions": assumptions}


def available_ns(replay, a, b, start, end):
    cuts = sorted({start, end} | {t for n in (a, b) for t in replay.communication[n].times if start < t < end})
    return sum(right-left for left, right in zip(cuts, cuts[1:])
               if replay.communication[a].at(left) and replay.communication[b].at(left))


def validate_native_links(source, replay, duration):
    """Verify every native metric window, not just sampled route events or totals.

    This deliberately supports the frozen run's fixed natural graph plus recorded
    communication outages only. Any other dynamic link behavior is unsupported.
    Future windows validate the reconstruction, never filter a past candidate.
    """
    summaries = {}
    for row in csv_rows(source / "link-summary.csv", "source_node_id destination_node_id measurement_duration_s available_time_s mean_link_capacity_bps"):
        edge = (integer(row, "source_node_id"), integer(row, "destination_node_id"))
        require(edge not in summaries and edge[0] != edge[1] and set(edge) <= replay.nodes,
                "duplicate/invalid link summary")
        require(seconds_ns(row["measurement_duration_s"]) == duration, "link coverage mismatch")
        require(Decimal(row["mean_link_capacity_bps"]) == 10**10, "link bandwidth mismatch")
        summaries[edge] = row
    require(bool(summaries), "missing native links")
    next_start = {edge: 0 for edge in summaries}
    totals = Counter()
    initial_edges = set()
    windows = 0
    columns = "window_start_s window_end_s source_node_id destination_node_id available_time_s mean_link_capacity_bps"
    for row in csv_rows(source / "link-window-metrics.csv", columns):
        edge = (integer(row, "source_node_id"), integer(row, "destination_node_id"))
        start, end = seconds_ns(row["window_start_s"]), seconds_ns(row["window_end_s"])
        observed = seconds_ns(row["available_time_s"])
        require(edge in summaries and start == next_start[edge] and end == start + SECOND and end <= duration,
                "missing, duplicate, or non-1s native link window")
        require(Decimal(row["mean_link_capacity_bps"]) == 10**10, "window bandwidth mismatch")
        if start == 0:
            require(observed in (0, SECOND), "ambiguous initial native topology")
            if observed == SECOND:
                initial_edges.add(edge)
        expected = available_ns(replay, *edge, start, end) if edge in initial_edges else 0
        require(observed == expected, f"unsupported/unexplained native link transition: {edge}, {start}")
        totals[edge] += observed
        next_start[edge] = end
        windows += 1
    require(all(t == duration for t in next_start.values()), "incomplete native window coverage")
    require(all(totals[e] == seconds_ns(r["available_time_s"]) for e, r in summaries.items()),
            "native link summary/window mismatch")
    require(all((b, a) in initial_edges for a, b in initial_edges), "asymmetric native ISL evidence")
    replay.edges = initial_edges
    return {"directed_initial_links": len(initial_edges), "validated_windows": windows,
            "natural_graph": "fixed; all 1s availability windows verified against past communication events"}


def candidate_row(replay, primary, time, suffix, before=False):
    state = replay.candidates(primary, time, before=before, strict_start=not before)
    return {f"candidate_count_{suffix}": len(state["candidates"]),
            f"candidate_nodes_{suffix}": state.pop("candidates"), **state}


def audit_records(replay, tasks, shadow, shadow_events, decisions, shadow_faults, impacts, fault_events):
    starts = keyed(e for e in shadow_events if e["event"] == "START")
    on = keyed(e for e in shadow_events if e["event"] == "ON")
    triggered = keyed(d for d in decisions if boolean(d, "start_triggered"))
    require(set(starts) == set(triggered) == {i for i, s in shadow.items() if boolean(s, "ever_start")},
            "START event/decision/summary disagreement")
    require(set(on) == {i for i, s in shadow.items() if boolean(s, "init_complete")}, "ON identity mismatch")
    direct = keyed(i for i in impacts if i["impact_type"] in {"RUNNING_INTERRUPTED", "RUNNING_INTERRUPTED_PERMANENT"})
    victims = keyed(shadow_faults)
    require(set(victims) == set(direct), "shadow/direct victim ledger disagreement")
    actual_faults = keyed((f for f in fault_events if f["event_type"] == "START"), "fault_id")
    start_rows, fault_rows, persistent_rows, lead_rows, appendix = [], [], [], [], []
    for task_id in sorted(starts):
        task, summary, event, decision = tasks[task_id], shadow[task_id], starts[task_id], triggered[task_id]
        primary, time = integer(task, "compute_node_id"), integer(event, "time_ns")
        require(time == integer(summary, "start_time_ns") == integer(decision, "time_ns"), "START time mismatch")
        require(decision["mode_before"] == "OFF" and decision["mode_after"] == "INITIALIZING", "invalid START modes")
        require(replay.task_state[task_id].at(time) == "RUNNING" and replay.healthy[primary].at(time),
                "START task is not computing on a healthy primary")
        if task_id in on:
            complete = integer(summary, "init_complete_time_ns")
            require(time < complete == integer(on[task_id], "time_ns")
                    == integer(summary, "init_planned_complete_time_ns"), "ON completion time mismatch")
        start_rows.append({"task_id": task_id, "primary_node": primary, "start_time_ns": time,
                           "start_time_s": time/SECOND, "shadow_mode_before": decision["mode_before"],
                           "shadow_mode_after": decision["mode_after"], **candidate_row(replay, primary, time, "start")})
        failed = task["final_state"] == "FAILED"
        # Normal observation ends at task completion (after RESULT), conservatively
        # including that endpoint. Failed observation stops at the left limit.
        end = integer(task, "failure_time_ns" if failed else "result_transfer_complete_time_ns")
        persistent = replay.persistent(primary, time, end, before_end=failed)
        persistent_rows.append({"task_id": task_id, "primary_node": primary, "start_time_ns": time,
                                "end_time_ns": end, "end_boundary": "pre-fault" if failed else "post-task-completion",
                                "candidate_count_persistent": len(persistent), "candidate_nodes_persistent": persistent,
                                "retrospective": True})
    for task_id, fault in sorted(victims.items()):
        task, summary, impact = tasks[task_id], shadow[task_id], direct[task_id]
        primary, time, fault_id = integer(task, "compute_node_id"), integer(fault, "fault_time"), integer(fault, "fault_id")
        actual = actual_faults[fault_id]
        kind, mode = fault["fault_type"], fault["mode_at_fault"]
        require((integer(actual, "node_id"), integer(actual, "simulation_time_ns"), actual["fault_source"])
                == (primary, time, kind), "fault event/primary mismatch")
        require((integer(impact, "fault_id"), integer(impact, "fault_time_ns"), integer(impact, "fault_node_id"), impact["fault_type"])
                == (fault_id, time, primary, kind), "direct victim identity mismatch")
        require(impact["task_state_before_fault"] == impact["task_state_before_impact"] == "RUNNING"
                and replay.task_state[task_id].at(time, before=True) == "RUNNING", "victim is not pre-fault RUNNING")
        require(summary["shadow_mode_at_fault"] == mode and mode in {"ON", "INITIALIZING", "OFF"}, "shadow mode mismatch")
        require(integer(summary, "real_fault_time_ns") == time and summary["real_fault_type"] == kind,
                "shadow fault summary mismatch")
        require(boolean(fault, "primary_f1_f2") == (kind in {"F1", "F2"}), "F3 included in primary recovery")
        row = {"task_id": task_id, "fault_id": fault_id, "fault_type": kind, "primary_node": primary,
               "fault_time_ns": time, "fault_time_s": time/SECOND, "shadow_state_at_fault": mode,
               **candidate_row(replay, primary, time, "fault", before=True)}
        if kind == "F3":
            appendix.append(row)
            continue
        require(kind in {"F1", "F2"}, "unknown victim source")
        fault_rows.append(row)
        require(task_id in starts, "frozen primary victim has no START")
        require(mode in {"ON", "INITIALIZING"}, "START victim cannot return to OFF")
        start = integer(summary, "start_time_ns")
        planned = integer(summary, "init_planned_complete_time_ns")
        complete = integer(summary, "init_complete_time_ns") if boolean(summary, "init_complete") else None
        require(time >= start and planned > start, "invalid initialization timing")
        if mode == "ON":
            require(complete is not None and complete == integer(on[task_id], "time_ns") and complete <= time,
                    "ON victim lacks actual initialization completion")
        elif mode == "INITIALIZING":
            require(complete is None and planned >= time, "initialization miss disagrees with observed state")
        lead_rows.append({"task_id": task_id, "primary_node": primary, "fault_type": kind,
                          "shadow_state_at_fault": mode, "start_time_ns": start, "fault_time_ns": time,
                          "protect_lead_time_s": (time-start)/SECOND, "init_planned_complete_time_ns": planned,
                          "init_complete_time_ns": complete, "planned_init_duration_s": (planned-start)/SECOND,
                          "planned_init_margin_s": (time-planned)/SECOND,
                          "actual_init_margin_s": (time-complete)/SECOND if complete is not None else None,
                          "classification": "fault_after_init_complete" if mode == "ON" else "fault_during_initialization"})
    summary = {
        "start_tasks": len(starts), "on_tasks": len(on), "primary_victims": len(fault_rows),
        "victim_modes": dict(Counter(r["shadow_state_at_fault"] for r in fault_rows)),
        "start_candidates": distribution([r["candidate_count_start"] for r in start_rows]),
        "fault_candidates": distribution([r["candidate_count_fault"] for r in fault_rows]),
        "fault_candidates_by_mode": {mode: distribution([r["candidate_count_fault"] for r in fault_rows
                                                           if r["shadow_state_at_fault"] == mode])
                                     for mode in ("ON", "INITIALIZING", "OFF")},
        "persistent_candidates": distribution([r["candidate_count_persistent"] for r in persistent_rows]),
        "protect_lead_time_s": distribution([r["protect_lead_time_s"] for r in lead_rows]),
        "initialization_misses": [r for r in lead_rows if r["shadow_state_at_fault"] == "INITIALIZING"],
        "f3_appendix": appendix,
        "gate_a": "PASS" if start_rows and all(r["candidate_count_start"] for r in start_rows) else "FAIL",
        "gate_b": "PASS" if fault_rows and all(r["candidate_count_fault"] for r in fault_rows) else "FAIL",
        "persistent_is_gate": False, "new_simulation_executed": False, "n5_status": "NOT STARTED",
    }
    return summary, {"backup-candidates-at-start.csv": start_rows, "backup-candidates-at-fault.csv": fault_rows,
                     "backup-candidates-persistent.csv": persistent_rows, "victim-protection-lead-time.csv": lead_rows}


def analyze(source):
    tables = {
        "task-summary.csv": "task_id compute_node_id final_state failure_time_ns result_transfer_complete_time_ns",
        "task-events.csv": "simulation_time_ns task_id from_state to_state node_id cause",
        "fault-events.csv": "simulation_time_ns fault_id node_id fault_type event_type satellite_available_after communication_available_after compute_available_after fault_source route_recomputed duration_ns",
        "fault-task-impact.csv": "task_id fault_id fault_type fault_time_ns fault_node_id task_state_before_fault task_state_before_impact impact_type",
        "shadow/shadow-task-summary.csv": "task_id compute_node_id ever_start init_complete init_complete_time_ns init_planned_complete_time_ns start_time_ns real_fault_time_ns real_fault_type shadow_mode_at_fault real_task_state",
        "shadow/shadow-events.csv": "task_id time_ns event",
        "shadow/shadow-decisions.csv": "task_id time_ns start_triggered mode_before mode_after",
        "shadow/shadow-faults.csv": "task_id fault_id fault_type fault_time mode_at_fault primary_f1_f2",
    }
    data = {name: list(csv_rows(source / name, fields)) for name, fields in tables.items()}
    tasks, shadow = keyed(data["task-summary.csv"]), keyed(data["shadow/shadow-task-summary.csv"])
    identity = validate_identity(source, tasks, shadow, data["fault-events.csv"], read_json(source / "shadow/shadow-assumptions.json"))
    replay = Replay(range(66), tasks, data["task-events.csv"], data["fault-events.csv"], [])
    validate_task_history(replay, tasks, data["task-events.csv"], 1300*SECOND)
    network = validate_native_links(source, replay, 1300*SECOND)
    require(network["directed_initial_links"] == 242, "frozen initial link count mismatch")
    # The complete first window is initial-edge evidence, available before every query.
    require(all(integer(s, "start_time_ns") >= SECOND for s in shadow.values() if boolean(s, "ever_start")),
            "START predates available initial-link evidence")
    require(all(integer(f, "fault_time") >= SECOND for f in data["shadow/shadow-faults.csv"]),
            "fault query predates available initial-link evidence")
    summary, rows = audit_records(replay, tasks, shadow, data["shadow/shadow-events.csv"],
                                  data["shadow/shadow-decisions.csv"], data["shadow/shadow-faults.csv"],
                                  data["fault-task-impact.csv"], data["fault-events.csv"])
    require((summary["start_tasks"], summary["on_tasks"], summary["primary_victims"], summary["victim_modes"])
            == (387, 382, 82, {"ON": 77, "INITIALIZING": 5}), "frozen START/victim identity mismatch")
    require([(r["task_id"], r["primary_node"]) for r in summary["f3_appendix"]] == [(120, 62)], "F3 appendix mismatch")
    summary.update(identity=identity, native_network=network,
                   audit_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                   audit_worktree_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
                   limits=["independent per-task existence, not concurrent allocation or reservation",
                           "START/fault candidates can be different nodes; no state-placement guarantee",
                           "path existence only, not immediate capacity-aware admission or bandwidth",
                           "persistent is retrospective; healthy, idle including queue, and reachable throughout",
                           "no storage/risk scoring, real checkpoint traffic, or recovery execution"],
                   status="PASS" if summary["gate_a"] == summary["gate_b"] == "PASS" else "FAIL")
    return summary, rows


def write_results(output, summary, tables):
    output.mkdir(parents=True, exist_ok=False)
    for name, rows in tables.items():
        fields = sorted({key for row in rows for key in row}) or ["task_id"]
        with (output / name).open("x", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
            writer.writeheader()
            for row in rows:
                writer.writerow({key: json.dumps(value, separators=(",", ":")) if isinstance(value, list) else value
                                 for key, value in row.items()})
    with (output / "summary.json").open("x", encoding="utf-8") as stream:
        json.dump(summary, stream, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False)
        stream.write("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "output/n4-release-validation")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "output/pre-n5-backup-feasibility")
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output_dir.resolve()
    try:
        require(source.is_dir(), "BLOCKED: frozen N4 release output is unavailable; do not rerun ns-3")
        require(not output.exists(), "output directory already exists; refusing overwrite")
        require(source != output and source not in output.parents and output not in source.parents,
                "output must not overlap source evidence")
        require(ROOT not in output.parents or ROOT / "output" in output.parents,
                "repository outputs must stay under ignored output/")
        summary, tables = analyze(source)
        write_results(output, summary, tables)
    except (ValueError, KeyError, OSError, csv.Error, subprocess.CalledProcessError) as error:
        parser.exit(2, f"FAIL / STOP: {error}\n")
    print(json.dumps({key: summary[key] for key in ("status", "gate_a", "gate_b", "start_candidates", "fault_candidates")}, indent=2))
    return 0 if summary["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
