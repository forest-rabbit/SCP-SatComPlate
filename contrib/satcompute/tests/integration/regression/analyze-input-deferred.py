#!/usr/bin/env python3
"""InputDeferred v5 audit: read existing R4/R5 and new R6/R7, never rerun or overwrite them."""
import argparse
from collections import Counter
import json
from pathlib import Path
import runpy
import shlex

BASE = runpy.run_path(str(Path(__file__).with_name("analyze-baseline-evaluation.py")))
rows, require, number = BASE["rows"], BASE["require"], BASE["number"]
NS = 10**9
NORMAL = ("INIT_BASE", "INIT_STATE", "L1", "REMOTE_BATCH")
FAULT = ("RECOVERY_INPUT", "RECOVERY_STATE", "RECOVERY_TAIL")


def distribution(values):
    values = list(values)
    return {**BASE["stats"](values), "max": max(values, default=None)}


def dependency_check(task, protected, r, flows, events):
    """Fail closed on duplicated INPUT, hidden raw bytes, premature compute or extra merge."""
    require(r.get("input_staging_policy") == "deferred", "missing deferred recovery identity")
    if not r["recovery_accept_time_ns"]:
        require(not r["input_start_time_ns"] and not r["recovery_compute_start_time_ns"],
                "unaccepted recovery started INPUT/compute")
        return
    start = [e for e in events if e["event"] == "RECOVERY_INPUT_STARTED"]
    size = number(task, "input_bytes")
    require(len(start) == 1 and number(start[0], "bytes") == size and
            start[0]["time_ns"] == r["input_start_time_ns"] == r["recovery_accept_time_ns"],
            "deferred needs exactly one full original INPUT at recovery acceptance")
    input_flows = [f for f in flows if f["kind"] == "RECOVERY_INPUT"]
    local = task["source_node_id"] == r["recovery_node"]
    require(r["input_delivery_mode"] == ("LOCAL" if local else "NETWORK"), "INPUT local/network mismatch")
    require(len(input_flows) == int(not local), "missing/duplicate INPUT flow or local pseudo-UDP")
    if input_flows:
        f = input_flows[0]
        require(number(f, "bytes") == size and f["source_node"] == task["source_node_id"] and
                f["destination_node"] == r["recovery_node"] and f["storage_object_id"] == "0",
                "INPUT must target actual recovery, not backup storage")
    path = r["chosen_path"]
    has_tail = path in ("TAIL", "MIGRATE_TAIL")
    for key in ("tail_start_time_ns", "state_start_time_ns"):
        if r[key]:
            require(r[key] == r["input_start_time_ns"], "independent recovery flows artificially serialized")
    merges = [e for e in events if e["event"] == "RECOVERY_TAIL_COMMIT"]
    require(len(merges) <= 1 and (has_tail or not merges), "fault cR duplicated or charged for INPUT/state copy")
    if r["tail_commit_time_ns"]:
        require(has_tail and number(r, "tail_bytes") > 0 and len(merges) == 1, "merge without nonempty tail")
        receive = number(r, "tail_received_time_ns")
        if path == "MIGRATE_TAIL":
            require(r["state_received_time_ns"], "merge without committed state receiver")
            receive = max(receive, number(r, "state_received_time_ns"))
        require(number(r, "tail_commit_time_ns") == receive + number(protected, "cR_ns"),
                "fault tail did not pay exactly one cR after required receivers")
    if r["phase_at_fault"] == "ON":
        work, remote = number(task, "compute_work_units"), number(r, "remote_work_units")
        state = (remote // 100 * 114688 if task["task_profile"] == "llm" else
                 number(protected, "variable_state_bytes") * remote // work)
        require(number(r, "committed_remote_bytes") == number(r, "checkpoint_state_bytes") == state and
                bool(r["committed_remote_object_id"]), "deferred committed state contains INPUT or lost zero identity")
        if path.startswith("MIGRATE_"):
            require(number(r, "checkpoint_relocation_bytes") == state, "migration hides original INPUT")
    if r["recovery_compute_start_time_ns"]:
        require(r["input_received_time_ns"] and r["state_ready_time_ns"], "compute before INPUT/state ready")
        state_time = (r["tail_commit_time_ns"] if has_tail else
                      r["state_received_time_ns"] if path == "MIGRATE_REDO" else r["recovery_accept_time_ns"])
        require(state_time and r["state_ready_time_ns"] == state_time, "incorrect state dependency join")
        require(number(r, "recovery_compute_start_time_ns") ==
                max(number(r, "input_received_time_ns"), int(state_time)), "compute violated dependency barrier")


def staging_metrics(root, result):
    tasks = {t["task_id"]: t for t in rows(root, "task-summary.csv")}
    protected = {p["task_id"]: p for p in rows(root, "protection-task-summary.csv")}
    flows = rows(root, "protection-transfers.csv")
    events = rows(root, "recovery-events.csv")
    recoveries = result["recovery_rows"]
    deferred = result["execution"].get("input_staging_policy", "eager") == "deferred"
    if deferred:
        require(not any(f["kind"] == "INIT_BASE" for f in flows), "deferred sent normal original INPUT")
        for r in recoveries:
            key = r["task_id"]
            dependency_check(tasks[key], protected.get(key, {}), r,
                             [f for f in flows if f["task_id"] == key],
                             [e for e in events if e["task_id"] == key])
    sent = {k: sum(number(f, "sent_bytes") for f in flows if f["kind"] == k) for k in NORMAL + FAULT}
    normal, fault = sum(sent[k] for k in NORMAL), sum(sent[k] for k in FAULT)
    require(normal + fault == result["network"]["extra_sent_bytes"], "normal/fault network partition")
    accepted = [r for r in recoveries if r["recovery_accept_time_ns"]]
    received = [r for r in accepted if r["input_received_time_ns"]]
    joined = [r for r in accepted if r.get("state_ready_time_ns") and r["input_received_time_ns"]]
    pools = rows(root, "protection-node-storage-summary.csv")
    global_peak = None
    if deferred:
        meta = json.loads((root / "input-staging-summary.json").read_text())
        require(meta["input_staging_policy"] == "deferred", "staging output identity")
        global_peak = meta["global_simultaneous_backup_peak_bytes"]
        require(result["storage"]["max_node_peak_bytes"] <= global_peak <=
                sum(number(p, "peak_total_bytes") for p in pools), "global peak bounds")
    result["storage"].update(global_simultaneous_backup_peak_bytes=global_peak, node_peaks=pools,
        global_peak_note="Direct observed simultaneous total for deferred; old eager ledger did not collect this metric.")
    link_rows = rows(root, "link-summary.csv")
    result["links"]["max_single_link_full_mean_utilization_percent"] = max(
        100 * float(r["tx_busy_time_s"]) / float(r["measurement_duration_s"]) for r in link_rows)
    return dict(input_staging_policy="deferred" if deferred else "eager",
        protected_task_count=len(protected),
        faulted_protected_task_count=len(set(protected) & {r["task_id"] for r in recoveries}),
        on_at_fault=sum(r["phase_at_fault"] == "ON" for r in recoveries),
        eager_input_bytes_normal=sent["INIT_BASE"] if not deferred else 0,
        deferred_input_bytes_normal=sent["INIT_BASE"] if deferred else 0,
        fault_recovery_input_logical_bytes=sum(number(e, "bytes") for e in events if e["event"] == "RECOVERY_INPUT_STARTED"),
        fault_recovery_input_network_bytes=sent["RECOVERY_INPUT"],
        state_protection_bytes=sum(sent[k] for k in ("INIT_STATE", "L1", "REMOTE_BATCH")),
        recovery_state_bytes=sent["RECOVERY_STATE"], recovery_tail_bytes=sent["RECOVERY_TAIL"],
        normal_ft_network_bytes=normal, fault_ft_network_bytes=fault, total_ft_extra_network_bytes=normal+fault,
        input_delivery_modes=dict(Counter(r["input_delivery_mode"] for r in accepted)),
        input_receive_wait_s=distribution((number(r, "input_received_time_ns")-number(r, "input_start_time_ns"))/NS for r in received),
        pending_input_failed_count=sum(bool(r["input_start_time_ns"]) and not r["input_received_time_ns"] for r in accepted),
        state_ready_first=sum(number(r, "state_ready_time_ns") < number(r, "input_received_time_ns") for r in joined),
        input_ready_first=sum(number(r, "state_ready_time_ns") > number(r, "input_received_time_ns") for r in joined),
        simultaneous_ready=sum(r["state_ready_time_ns"] == r["input_received_time_ns"] for r in joined),
        input_critical_path_count=sum(number(r, "input_received_time_ns") > number(r, "state_ready_time_ns") for r in joined),
        incremental_input_critical_wait_s=distribution(max(0, number(r, "input_received_time_ns")-number(r, "state_ready_time_ns"))/NS for r in joined),
        incomplete_dependency_join=len(accepted)-len(joined) if deferred else None,
        per_profile={profile: dict(
            protected=sum(tasks[k]["task_profile"] == profile for k in protected),
            normal_ft_network_bytes=sum(number(f, "sent_bytes") for f in flows if f["kind"] in NORMAL and tasks[f["task_id"]]["task_profile"] == profile),
            fault_ft_network_bytes=sum(number(f, "sent_bytes") for f in flows if f["kind"] in FAULT and tasks[f["task_id"]]["task_profile"] == profile))
            for profile in BASE["PROFILES"]},
        note="Actual source application payload; logical INPUT counted once even for LocalDelivery. Wait distribution uses completed receivers; incomplete waits are not zero samples.")


def comparison(before, after):
    def delta(a, b):
        return dict(before=a, after=b, reduction=a-b, reduction_percent=100*(a-b)/a if a else None)
    return dict(waste=delta(before["summary"]["w_waste_actual"], after["summary"]["w_waste_actual"]),
        completed=dict(before=before["summary"]["completed"], after=after["summary"]["completed"]),
        network={k: delta(before["staging"][k], after["staging"][k]) for k in
                 ("normal_ft_network_bytes", "fault_ft_network_bytes", "total_ft_extra_network_bytes")},
        max_node_storage=delta(before["storage"]["max_node_peak_bytes"], after["storage"]["max_node_peak_bytes"]),
        T_catch_s=dict(before=before["T_catch_s"], after=after["T_catch_s"]))


def fairness(runs):
    controls = []
    for name, run in runs.items():
        e = run["execution"]
        deferred = name in ("R6", "R7")
        require((e["protection_mode"], e["placement_mode"], e.get("input_staging_policy", "eager"),
                 e["remote_busy_recovery_policy"]) ==
                ("compfrr", "ffp", "deferred" if deferred else "eager", "recompute" if name in ("R4", "R6") else "relocate"),
                "comparison policy identity mismatch")
        require(e["fault_mode"] == "generate" and not e["audit"] and not e["shadow"] and not e["worktree_dirty"], "nonformal identity")
        require((e["seed"], e["run"], e["simulation_duration_s"], run["execution_result"]["returncode"]) == (1, 11, 1300, 0), "incomplete formal run")
        ignored = {"--outputDir", "--faultTrace", "--inputStagingPolicy", "--remoteBusyRecoveryPolicy"}
        controls.append({k: v for k, v in (arg.split("=", 1) for arg in shlex.split(e["command"][-1])[1:]) if k not in ignored})
    require(all(c == controls[0] for c in controls), "nonpolicy frozen controls differ")
    require(runs["R6"]["execution"]["commit"] == runs["R7"]["execution"]["commit"], "R6/R7 execution code differs")
    return dict(same_nonpolicy_controls=True, new_groups_same_code=True, old_eager_reused=True,
                same_realized_fault_trace_required=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in ("r4", "r5", "r6", "r7"):
        parser.add_argument(f"--{label}", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    directories = {k.upper(): getattr(args, k).resolve() for k in ("r4", "r5", "r6", "r7")}
    require(not args.output.exists() and not any(args.output.resolve().is_relative_to(p) for p in directories.values()),
            "refusing to overwrite existing evidence or write inside raw runs")
    runs = {}
    for name, directory in directories.items():
        r = BASE["analyze"](directory)
        r["staging"] = staging_metrics(directory, r)
        runs[name] = r
    result = dict(model="CompFRR InputDeferred v5", fairness=fairness(runs), runs=runs,
        comparisons={f"{a}_to_{b}": comparison(runs[a], runs[b]) for a, b in (("R4", "R6"), ("R5", "R7"), ("R6", "R7"))},
        R6_minus_R7_busy=BASE["busy_comparison"](runs["R6"], runs["R7"]),
        status="STOPPED AT PRE-N5C COMPFrr INPUT-DEFERRED AUDIT")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({"fairness": result["fairness"], "comparisons": result["comparisons"]}, indent=2))


if __name__ == "__main__":
    main()
