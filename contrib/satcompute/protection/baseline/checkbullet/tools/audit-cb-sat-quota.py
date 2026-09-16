#!/usr/bin/env python3
"""Read-only quota impact audit; preserve source execution and same-ns uncertainty."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import subprocess

from cb_tools import GROUPS, ROOT, require, rows, write_json


def assignment_intervals(events):
    """Do not invent ordering between ownership and storage CSVs at the same ns."""
    active, intervals = {}, defaultdict(list)
    for row in events:
        if row["event"] not in ("ASSIGNMENT_ESTABLISHED", "ASSIGNMENT_RELEASED"):
            continue
        task, node, time = (int(row[k]) for k in ("task_id", "node_id", "time_ns"))
        key = task, node
        if row["event"] == "ASSIGNMENT_ESTABLISHED":
            require(key not in active, "duplicate assignment")
            active[key] = time
        else:
            require(key in active, "release without assignment")
            start = active.pop(key)
            require(start <= time, "reversed assignment interval")
            intervals[node].append((task, start, time))
    require(not active, "unreleased formal assignment")
    return intervals


def free_share(free, owners, task):
    ordered = sorted(owners | {task})
    each, remainder = divmod(free, len(ordered))
    return each + (ordered.index(task) < remainder)


def inspect_rows(decisions, events, pools, loads):
    """Reconstruct per-owner bytes from ordered pool deltas, including in-place merges."""
    capacities = {int(r["node_id"]): int(r["capacity_bytes"]) for r in pools}
    require(len(capacities) == len(pools), "duplicate physical pool")
    intervals = assignment_intervals(loads)
    affected, ambiguous = [], []
    max_excess = 0
    for row in decisions:
        quota, occupied = int(row["quota_bytes"]), int(row["occupied_bytes"])
        require(quota >= 0 and occupied >= 0, "negative quota evidence")
        excess = max(0, occupied - quota)
        max_excess = max(max_excess, excess)
        if excess:
            affected.append(dict(kind="OCCUPIED_EXCEEDS_QUOTA", task_id=int(row["task_id"]),
                time_ns=int(row["time_ns"]), quota_bytes=quota, occupied_bytes=occupied))

    occupancy = {node: {} for node in capacities}
    totals = dict.fromkeys(capacities, 0)
    reservations = same_ns = 0
    previous = -1
    for index, row in enumerate(events):
        task, node, time = (int(row[k]) for k in ("task_id", "node_id", "time_ns"))
        require(time >= previous, "storage event order reversed")
        previous = time
        require(node in capacities, "unknown physical pool")
        used, reserved = int(row["node_used_bytes"]), int(row["node_reserved_bytes"])
        total = used + reserved
        require(min(used, reserved) >= 0 and total <= capacities[node], "physical capacity exceeded")
        before = totals[node]
        owners = occupancy[node]
        delta = total - before
        if row["event"] == "STORAGE_RESERVED":
            request = int(row["bytes"])
            require(request > 0 and delta == request, "reservation/pool delta mismatch")
            require(int(row["object_id"]) > 0, "accepted reservation lacks object")
            reservations += 1
            certain, possible = set(owners) | {task}, set(owners) | {task}
            for owner, start, end in intervals[node]:
                if start < time < end:
                    certain.add(owner)
                if start <= time <= end:
                    possible.add(owner)
            low = free_share(capacities[node] - before, possible, task)
            high = free_share(capacities[node] - before, certain, task)
            require(low <= high, "invalid same-ns share bounds")
            same_ns += low != high
            if request > low:
                detail = dict(kind="ACCEPTED_ABOVE_AVAILABLE_QUOTA" if request > high else
                              "SAME_NS_ORDER_REQUIRES_REVIEW", task_id=task, node_id=node,
                    time_ns=time, event_row=index + 2, request_bytes=request,
                    occupied_bytes=owners.get(task, 0), available_lower_bytes=low,
                    available_upper_bytes=high)
                (affected if request > high else ambiguous).append(detail)
        else:
            require(delta <= 0, "unlogged positive storage allocation")
        new = owners.get(task, 0) + delta
        require(new >= 0, "storage delta attributed to wrong owner or missing event")
        if new:
            owners[task] = new
        else:
            owners.pop(task, None)
        totals[node] = total
        require(sum(owners.values()) == total, "per-owner/physical bytes disagree")
    require(not any(totals.values()), "nonzero final event occupancy")
    require(all(int(r["final_used_bytes"]) == int(r["final_reserved_bytes"]) == 0 for r in pools),
            "nonzero final pool occupancy")
    return dict(decision_count=len(decisions), event_count=len(events),
        accepted_reservation_count=reservations, same_ns_bounded_reservations=same_ns,
        affected_count=len(affected), affected_records=affected,
        ambiguous_count=len(ambiguous), ambiguous_records=ambiguous,
        max_occupied_minus_quota_bytes=max_excess,
        physical_capacity_and_owner_ledger_consistent=True)


def inspect(root):
    groups, commits = {}, set()
    for placement, busy in GROUPS:
        name = f"CB-{placement}-{busy}"
        directory = root / name
        identity = json.loads((directory / "execution.json").read_text())
        outcome = json.loads((directory / "execution-result.json").read_text())
        require(identity["stage"] == "formal" and not identity["worktree_dirty"], "nonformal/dirty source")
        require(outcome["status"] == "FINISHED" and outcome["returncode"] == 0, "unfinished source")
        require(identity["placement_mode"] == placement and identity["remote_busy_recovery_policy"] == busy,
                "matrix identity mismatch")
        commits.add(identity["commit"])
        groups[name] = inspect_rows(rows(directory, "cb-sat-decisions.csv"),
            rows(directory, "cb-sat-events.csv"), rows(directory, "cb-sat-storage.csv"),
            rows(directory, "placement-load-events.csv"))
    require(len(commits) == 1, "mixed source execution commits")
    affected = sum(g["affected_count"] for g in groups.values())
    ambiguous = sum(g["ambiguous_count"] for g in groups.values())
    return dict(source_root=str(root), source_execution_commit=commits.pop(), source_read_only=True,
        audit_kind="offline_quota_defensive_subtraction_impact", groups=groups,
        affected_count=affected, affected_tasks=sorted({r["task_id"] for g in groups.values()
            for r in g["affected_records"]}), affected_groups=[n for n, g in groups.items() if g["affected_count"]],
        max_occupied_minus_quota_bytes=max(g["max_occupied_minus_quota_bytes"] for g in groups.values()),
        ambiguous_count=ambiguous, audit_complete=ambiguous == 0,
        formal_rerun_required=affected > 0,
        evidence_scope="All recorded quota snapshots and accepted object reservations; unrecorded rejected previews are not reconstructed.",
        same_ns_scope="Use all possible zero-occupancy owners for a lower free-share bound; never assume cross-CSV tie ordering.",
        share_contract="occupied floor plus equal shares of remaining free capacity; unchanged")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    require(output != root and root not in output.parents, "source evidence must remain read-only")
    require(not output.exists(), "refusing to overwrite quota audit")
    result = inspect(root)
    result.update(audit_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        audit_worktree_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)))
    write_json(output, result)
    print(json.dumps({k: result[k] for k in ("affected_count", "affected_groups", "ambiguous_count",
        "audit_complete", "formal_rerun_required")}))
    require(result["audit_complete"], "ambiguous admission requires same-ns review before closeout")


if __name__ == "__main__":
    main()
