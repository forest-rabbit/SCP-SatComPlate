#!/usr/bin/env python3
"""Inspect old CB evidence without changing it; write adjustment impact to a new directory."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import subprocess
from cb_tools import ROOT, GROUPS, require, rows, write_json


def input_impact(recoveries):
    affected = []
    for row in recoveries:
        require(row["input_ready"] in ("true", "false"), "invalid INPUT readiness evidence")
        if row["chosen_path"] in ("DIRECT", "RELOCATE") and row["input_ready"] == "false":
            affected.append({key: row[key] for key in (
                "task_id", "fault_time_ns", "chosen_path", "input_ready", "root_ready",
                "input_object_id", "resume_work_units", "terminal_state")})
    return dict(recovery_count=len(recoveries), affected_count=len(affected), affected_tasks=affected)


def zero_thresholds(decisions, events):
    zeros = [row for row in decisions if int(row["threshold"]) == 0]
    keys = {(row["task_id"], row["time_ns"]) for row in zeros}
    grouped = defaultdict(list)
    for event in events:
        key = event["task_id"], event["time_ns"]
        if key in keys:
            grouped[key].append(event)
    details = []
    for row in zeros:
        same = grouped[row["task_id"], row["time_ns"]]
        merges = [event for event in same if event["event"] == "MERGE_START"]
        logs = any(int(event["recoverable_work_units"]) > int(event["root_work_units"])
                   for event in merges)
        assignment = any(event["event"] == "BACKUP_ASSIGNED" for event in same)
        classification = ("EXISTING_LOG_COMPACTION" if merges and logs else
                          "INITIAL_ASSIGNMENT_NO_REMAINING_LOGS" if assignment and
                          row["threshold_reason"] == "NO_REMAINING_LOGS" and not merges else
                          "REQUIRES_REVIEW")
        details.append(dict(task_id=row["task_id"], time_ns=int(row["time_ns"]),
            threshold_reason=row["threshold_reason"], decision_reason=row["reason"],
            natural_limit=int(row["natural_limit"]), existing_logs_at_merge=logs,
            immediate_merge_count=len(merges), classification=classification,
            same_time_events=same))
    return dict(decision_count=len(decisions), zero_count=len(zeros),
        classifications=dict(Counter(row["classification"] for row in details)), observations=details)


def inspect(root):
    impact, thresholds, commits = {}, {}, set()
    for placement, busy in GROUPS:
        name = f"CB-{placement}-{busy}"
        directory = root / name
        identity = json.loads((directory / "execution.json").read_text())
        require(identity["stage"] == "formal", "adjustment impact requires formal evidence")
        commits.add(identity["commit"])
        impact[name] = input_impact(rows(directory, "cb-sat-recovery.csv"))
        thresholds[name] = zero_thresholds(rows(directory, "cb-sat-decisions.csv"),
                                          rows(directory, "cb-sat-events.csv"))
    require(len(commits) == 1, "mixed old execution commits")
    total = sum(group["affected_count"] for group in impact.values())
    common = dict(source_root=str(root.resolve()), source_execution_commit=commits.pop(),
                  source_read_only=True, audit_kind="offline_adjustment_impact")
    return (
        dict(**common, affected_count=total,
             affected_tasks=sorted({r["task_id"] for g in impact.values() for r in g["affected_tasks"]}, key=int),
             groups=impact, formal_rerun_required=total > 0),
        dict(**common, groups=thresholds,
             total_zero_count=sum(g["zero_count"] for g in thresholds.values()),
             immediate_compaction_count=sum(r["immediate_merge_count"] for g in thresholds.values()
                                            for r in g["observations"])))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    require(output != root and root not in output.parents, "old evidence must remain read-only")
    files = [output / "cb-input-readiness-impact-audit.json", output / "cb-x0-runtime-audit.json"]
    require(not any(path.exists() for path in files), "refusing to overwrite an existing impact audit")
    impact, thresholds = inspect(root)
    audit_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT))
    for path, result in zip(files, (impact, thresholds)):
        result.update(audit_commit=audit_commit, audit_worktree_dirty=dirty)
        write_json(path, result)
    print(json.dumps(dict(affected_records=impact["affected_count"], affected_tasks=impact["affected_tasks"],
                          formal_rerun_required=impact["formal_rerun_required"],
                          x_zero_count=thresholds["total_zero_count"], output=str(output))))


if __name__ == "__main__":
    main()
