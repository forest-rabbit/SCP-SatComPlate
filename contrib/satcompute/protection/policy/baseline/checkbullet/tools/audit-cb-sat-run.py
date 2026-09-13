#!/usr/bin/env python3
"""Read-only causal checks of CB objects/flows/recovery plus the common actual-WU ledger."""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import runpy
import shlex
from cb_tools import REGRESSION, SCENE_HELPER, flags, require, rows, scene_identity, write_json

ACCOUNT = runpy.run_path(str(REGRESSION / "analyze-baseline-evaluation.py"))
NS = 10**9


def number(row, key):
    return int(row.get(key) or 0)


def yes(value):
    return value in (True, "true", "1", 1)


def state_bytes(task, work):
    return work//400*114688 if task["task_profile"] == "llm" else (
        int(task["variable_state_bytes"])*work//int(task["total_work_units"]))


def header(task):
    return 0 if task["task_profile"] == "llm" else (48 if task["task_profile"] == "sparse-inference" else 44)+len(task["task_id"])


def distribution(values):
    values = sorted(values)
    def q(p):
        if not values:
            return None
        x = (len(values)-1)*p; a,b = math.floor(x),math.ceil(x)
        return values[a]+(values[b]-values[a])*(x-a)
    return dict(count=len(values), mean=sum(values)/len(values) if values else None,
                p50=q(.5), p95=q(.95), max=max(values, default=None))


def completed_execution(run, identity, outcome):
    """PARTIAL is a task outcome, not an interrupted simulator execution."""
    require(outcome["returncode"] == 0 and outcome["status"] == "FINISHED", "execution did not finish")
    require(run["run_status"] in ("COMPLETE", "PARTIAL") and run["simulation_duration_ns"] ==
            round(identity["simulation_duration_s"]*NS), "simulation horizon incomplete")


def audit(root):
    task_rows = rows(root, "task-summary.csv")
    tasks = {t["task_id"]:t for t in task_rows}
    normal = {t["task_id"]:t for t in rows(root, "cb-sat-tasks.csv")}
    recovery = {r["task_id"]:r for r in rows(root, "cb-sat-recovery.csv")}
    events = rows(root, "cb-sat-events.csv")
    records = rows(root, "cb-sat-checkpoints.csv")
    flows = rows(root, "cb-sat-transfers.csv")
    pools = rows(root, "cb-sat-storage.csv")
    decisions = rows(root, "cb-sat-decisions.csv")
    parameters = json.loads((root / "cb-sat-parameters.json").read_text())
    if (root/"execution.json").exists():
        execution_identity = json.loads((root/"execution.json").read_text())
        outcome = json.loads((root/"execution-result.json").read_text())
        run = json.loads((root/"run-summary.json").read_text())
        completed_execution(run, execution_identity, outcome)
        require(not execution_identity["worktree_dirty"], "dirty execution snapshot")
        require(not parameters.get("unit_fixture_only",False), "unit fixture leaked into platform execution")
        require(parameters["mtbf_seconds"] == execution_identity["mtbf_seconds"], "production MTBF differs from frozen execution")
        require(parameters["placement"] == execution_identity["placement_mode"] and
                parameters["remote_busy_policy"] == execution_identity["remote_busy_recovery_policy"],
                "actual policy differs from execution identity")
        require(math.isclose(parameters["eligible_exposure_seconds"],
                parameters["eligible_check_count"]*parameters["check_interval_ns"]/NS), "MTBF exposure clock mismatch")
        require(parameters["joint_failure_count"] == 0 if parameters["mtbf_seconds"] is None else
                math.isclose(parameters["mtbf_seconds"]*parameters["joint_failure_count"],
                             parameters["eligible_exposure_seconds"]), "MTBF pooled ratio mismatch")
        if execution_identity["stage"] == "formal":
            expected = SCENE_HELPER["arguments"](root,protection_mode="checkbullet",
                placement_mode=parameters["placement"],remote_busy_recovery_policy=parameters["remote_busy_policy"])
            require(flags(shlex.split(execution_identity["command"][-1])) == flags(expected), "formal command differs from frozen helper")
            require(execution_identity["scene"] == scene_identity(), "formal input identity changed")
            require((len(tasks),run["total_input_bytes"],run["total_output_bytes"],run["total_compute_work_units"],
                     run["compute_node_count"]) == (800,194119753287,100166291859,352513119,66), "formal workload changed")
    require(len(tasks) == len(task_rows), "duplicate logical tasks")
    require(all(t["final_state"] in ("COMPLETED", "FAILED") for t in task_rows), "nonterminal logical task")
    require(len(recovery) == len(rows(root,"cb-sat-recovery.csv")), "more than one CB recovery per task")
    require(yes(parameters["final_quiescent"]) and yes(parameters["final_loads_empty"]), "CB did not finalize")
    require(parameters["input_contract"] == "full_original_input" and not parameters["tail_enabled"], "wrong CB contract")
    by_task = defaultdict(list)
    for e in events:
        by_task[e["task_id"]].append(e)
    by_record = {(r["task_id"],number(r,"sequence")):r for r in records}
    task_records = defaultdict(list)
    for r in records:
        task_records[r["task_id"]].append(r)
    require(len(by_record) == len(records), "duplicate checkpoint identity")
    for tid,t in normal.items():
        t["task_profile"] = tasks[tid]["task_profile"]
        require(number(t,"input_bytes") == number(tasks[tid],"input_bytes"), "INPUT size was trimmed")
        tf = float(t["mtbf_seconds"]) if t["mtbf_seconds"] not in ("", "null") else math.inf
        g = math.sqrt(2*tf*float(t["reference_cost_seconds"]))
        require(math.isclose(float(t["interval_seconds"]),g) if math.isfinite(g) else t["interval_seconds"] in ("", "null"),
                "H interval differs from frozen Young equation")
        if math.isfinite(g):
            h = g/float(t["task_seconds"])
            require(math.isclose(float(t["raw_fraction"]),h), "H fraction mismatch")
            require(number(t,"delta_permille") == (0 if h >= 1 else max(1,math.floor(1000*h))), "H cadence mismatch")
        expected = number(t,"generated_count")*number(t,"local_cost_ns") + (
            number(t,"initial_commits")+number(t,"merge_count"))*number(t,"remote_cost_ns")
        require(expected == number(t,"normal_protection_cost_ns"), "normal cL/cR charged incorrectly")
        require(math.isclose(float(t["normal_protection_eq_wu"]),expected*number(t,"primary_rate_wu_per_s")/NS),
                "normal equivalent cost mismatch")
        require(sum(e["event"] == "GENERATED" for e in by_task[tid]) == number(t,"generated_count"),
                "generated count lacks completed event")
        require(sum(e["event"] == "REMOTE_COST_COMMITTED" for e in by_task[tid]) ==
                number(t,"initial_commits")+number(t,"merge_count"), "merge cost lacks completed event")
        previous = 0
        for r in sorted(task_records[tid], key=lambda r:number(r,"sequence")):
            seq,w = number(r,"sequence"),number(r,"to_work_units")
            require(number(r,"from_work_units") == previous and 0 < w < number(t,"total_work_units"), "checkpoint chain discontinuity")
            require(number(r,"record_bytes") == state_bytes(t,w)-state_bytes(t,previous)+header(t), "CB record bytes/H mismatch")
            require(number(r,"base_version") == seq-1 and r["attempt_generation"] == "0", "checkpoint lineage mismatch")
            if r["generated_time_ns"]:
                require(number(r,"generated_time_ns") == number(r,"captured_time_ns")+number(t,"local_cost_ns"), "cL not asynchronous actual generation")
            if r["received_time_ns"]:
                require(r["generated_time_ns"] and number(r,"received_time_ns") >= number(r,"generated_time_ns"), "receipt before generation")
            previous = w
    for d in decisions:
        limits = [number(d,k) for k in ("storage_limit","natural_limit","implementation_limit")]
        if d["recovery_limit"]:
            limits.append(number(d,"recovery_limit"))
        require(number(d,"threshold") == min(limits) and yes(d["feasible"]) == (min(limits) > 0),
                "X not min of actual limits or zero clamped to one")
    physical_ids = set()
    for f in flows:
        tid,kind = f["task_id"],f["kind"]
        require("TAIL" not in kind and kind in ("CB_INIT_INPUT","CB_INIT_FULL","CB_DELTA","CB_RELOCATE_INPUT",
                "CB_RELOCATE_FULL","CB_RELOCATE_LOG","CB_FALLBACK_INPUT","RESULT"), "unknown or tail CB flow")
        require(f["transfer_id"] not in physical_ids, "duplicate physical CB flow")
        physical_ids.add(f["transfer_id"])
        require(f["source_node"] != f["destination_node"] and number(f,"bytes") > 0, "fake local UDP")
        require(f["state"] in ("COMPLETED","FAILED","CANCELLED"), "live CB flow after finalize")
        require(number(f,"registered_time_ns") == number(f,"requested_time_ns")+1, "flow skipped canonical next-ns registration")
        require(0 <= number(f,"received_bytes") <= number(f,"sent_bytes") <= number(f,"bytes"), "physical flow byte conservation")
        if f["state"] == "COMPLETED":
            require(number(f,"received_bytes") == number(f,"bytes") and
                    number(f,"received_time_ns") >= number(f,"sender_finished_time_ns"), "completion not receiver-based")
        if "INPUT" in kind:
            require(number(f,"bytes") == number(tasks[tid],"input_bytes"), "CB did not move full original INPUT")
        if kind.startswith("CB_INIT") or kind == "CB_DELTA":
            require(f["source_node"] == normal[tid]["primary_node"] and f["destination_node"] == normal[tid]["backup_node"], "normal flow changed assigned backup")
        if kind in ("CB_INIT_FULL","CB_DELTA"):
            require(number(f,"bytes") == number(by_record[(tid,number(f,"sequence"))],"record_bytes"), "retry changed captured object bytes")
        if kind.startswith("CB_RELOCATE"):
            r = recovery[tid]
            require(r["chosen_path"] == "RELOCATE" and f["source_node"] == r["backup_node"] and
                    f["destination_node"] == r["recovery_node"], "migration accessed unapproved state source")
        if kind != "RESULT":
            reserve = [e for e in by_task[tid] if e["event"] == "STORAGE_RESERVED" and
                       e["node_id"] == f["destination_node"] and e["object_id"] == f["destination_object_id"]]
            require(len(reserve) == 1 and number(reserve[0],"bytes") == number(f,"bytes") and
                    number(reserve[0],"time_ns") <= number(f,"requested_time_ns"), "flow without real prior destination reservation")
    capacities = {r["node_id"]:number(r,"capacity_bytes") for r in pools}
    require(pools and all(number(p,"final_used_bytes") == number(p,"final_reserved_bytes") == 0 and
                         number(p,"peak_total_bytes") <= number(p,"capacity_bytes") for p in pools), "storage leaked/overcommitted")
    require(all(number(e,"node_used_bytes")+number(e,"node_reserved_bytes") <= capacities[e["node_id"]]
                for e in events if e["node_id"] in capacities), "event storage exceeds physical capacity")
    for r in recovery.values():
        tid,cutoff = r["task_id"],number(r,"cutoff_time_ns")
        wr,wq,wf = (number(r,k) for k in ("root_work_units","recoverable_work_units","actual_work_units"))
        require(0 <= wr <= wq <= wf <= number(tasks[tid],"compute_work_units"), "invalid r/q/Wf")
        require(number(r,"tail_request_count") == number(r,"tail_bytes") == 0, "CB secretly used tail")
        if r["recovery_accept_time_ns"]:
            require(number(r,"recovery_accept_time_ns") >= cutoff+1, "recovery before complete fault batch")
        if r["chosen_path"] == "RECOMPUTE":
            require(number(r,"resume_work_units") == 0, "recompute used checkpoint")
        elif r["chosen_path"] in ("DIRECT","RELOCATE"):
            require(yes(r["input_ready"]) and yes(r["root_ready"]) and number(r,"input_object_id") > 0,
                    "checkpoint recovery requires complete INPUT and root before fault")
            receipts = [e for e in by_task[tid] if e["event"] == "RECEIVED" and
                        number(e,"sequence") == 0 and e["object_id"] == r["input_object_id"] and
                        e["node_id"] == r["backup_node"] and number(e,"time_ns") < cutoff]
            inputs = [e for e in by_task[tid] if e["event"] == "STORAGE_USED" and e["role"] == "INPUT" and
                      e["object_id"] == r["input_object_id"] and e["node_id"] == r["backup_node"] and
                      number(e,"time_ns") < cutoff]
            reservations = [e for e in by_task[tid] if e["event"] == "STORAGE_RESERVED" and
                            e["role"] == "INPUT" and e["object_id"] == r["input_object_id"] and
                            e["node_id"] == r["backup_node"] and number(e,"time_ns") < cutoff and
                            number(e,"bytes") == number(tasks[tid],"input_bytes")]
            require(len(receipts) == len(inputs) == len(reservations) == 1,
                    "saved complete INPUT lacks strict-before physical receipt/object")
            require(number(r,"resume_work_units") == wq, "CB upgraded beyond approved q")
            seq = number(r,"root_sequence")
            roots = [e for e in by_task[tid] if e["event"] in ("ROOT_COMMIT","MERGE_COMMIT") and
                     number(e,"sequence") == seq and number(e,"time_ns") < cutoff and
                     e["object_id"] == r["root_object_id"] and e["node_id"] == r["backup_node"]]
            require(len(roots) == 1, "saved root missing strict-before commit")
            for token in filter(None,r["log_objects"].split(';')):
                sequence,object_id = token.split(':'); record = by_record[(tid,int(sequence))]
                require(record["destination_object_id"] == object_id and record["log_commit_time_ns"] and
                        record["received_time_ns"] and number(record,"log_commit_time_ns") < cutoff and
                        number(record,"received_time_ns") < cutoff, "recovery read future or foreign log")
        if r["recovery_compute_start_time_ns"]:
            start = number(r,"recovery_compute_start_time_ns")
            require(start >= max(number(r,"input_received_time_ns"),number(r,"state_ready_time_ns")), "compute before ready")
            require(number(r,"reserved_idle_ns") == start-number(r,"recovery_accept_time_ns"), "idle not actual wait")
        require(number(r,"restore_processing_ns_included_in_idle") <= number(r,"reserved_idle_ns"), "restore diagnostic outside actual idle")
        if r["state_ready_time_ns"] and r["chosen_path"] in ("DIRECT", "RELOCATE"):
            saved = dict(token.split(':') for token in filter(None,r["log_objects"].split(';')))
            applied = [e for e in by_task[tid] if e["event"] == "RECOVERY_LOG_APPLIED"]
            require(len(applied) == len(saved) and {e["sequence"] for e in applied} == set(saved),
                    "actual restored chain differs from approved snapshot")
            for e in applied:
                require(e["node_id"] == r["recovery_node"] and number(e,"time_ns") <= number(r,"state_ready_time_ns"),
                        "log applied on wrong holder or after ready")
                if r["chosen_path"] == "DIRECT":
                    require(e["object_id"] == saved[e["sequence"]], "direct used another object's same-sized data")
            if r["chosen_path"] == "RELOCATE":
                wanted = [("CB_RELOCATE_FULL",r["root_sequence"],
                           state_bytes(normal[tid],wr)+header(normal[tid]),None)]
                wanted += [("CB_RELOCATE_LOG",e["sequence"],number(by_record[(tid,number(e,"sequence"))],"record_bytes"),
                            e["object_id"]) for e in applied]
                for kind,sequence,bytes_,object_id in wanted:
                    delivered = [f for f in flows if f["task_id"] == tid and f["kind"] == kind and f["sequence"] == sequence
                        and f["source_node"] == r["backup_node"] and f["destination_node"] == r["recovery_node"]
                        and f["attempt_generation"] == "1" and f["state"] == "COMPLETED" and number(f,"bytes") == bytes_
                        and number(f,"registered_time_ns") > cutoff and number(f,"terminal_time_ns") <= number(r,"state_ready_time_ns")
                        and (object_id is None or f["destination_object_id"] == object_id)]
                    require(len(delivered) == 1, "new holder's objects lack approved actual B-to-C delivery")
    placement = rows(root,"placement-selections.csv")
    require(all(not p["local_node"] and not p["remote_node"] for p in placement), "fabricated double-tier pair")
    require(all(p["placement_mode"] == parameters["placement"] for p in placement), "placement label differs from actual class")
    require(all(number(p,"active_backup_assignments") == number(p,"active_recoveries") == 0
                for p in rows(root,"placement-node-summary.csv")), "placement load leaked")
    impacts = rows(root,"fault-task-impact.csv") if (root/"fault-task-impact.csv").exists() else []
    execution = []
    for tid,t in tasks.items():
        r,n = recovery.get(tid,{}),normal.get(tid,{})
        normal_eq = float(n.get("normal_protection_eq_wu",0))
        idle_eq = float(r.get("recovery_reserved_idle_eq_wu",0))
        primary = [i for i in impacts if i["task_id"] == tid and i["task_state_before_fault"] == "RUNNING" and i["progress_valid"] == "1"]
        e = ACCOUNT["task_execution"](t,r,{},[],primary,normal_eq,idle_eq)
        e["execution_source"] = e["execution_source"].replace("recovery-summary.csv","cb-sat-recovery.csv")
        execution.append(dict(task_id=tid,task_profile=t["task_profile"],normal_protection_eq_wu=normal_eq,
                              reserved_idle_eq_wu=idle_eq,**e))
    normal_flows = [f for f in flows if f["attempt_generation"] == "0"]
    fault_flows = [f for f in flows if f["attempt_generation"] == "1" and f["kind"] != "RESULT"]
    completed = sum(t["task_success"] == "1" for t in task_rows)
    summary = dict(completed=completed,failed=len(tasks)-completed,
        deadline_success=sum(t["compute_deadline_met"] == "1" for t in task_rows),
        started=len(normal),backup_assigned=sum(bool(t["backup_node"]) for t in normal.values()),
        initialized=sum(bool(t["initialized_time_ns"]) for t in normal.values()),
        no_checkpoint_interval=sum(t["target_count"] == "0" for t in normal.values()),
        admission_rejections=sum(e["event"] == "BOUNDARY_ADMISSION_REJECTED" for e in events),
        recovery_attempted=len(recovery),
        recovery_accepted=sum(bool(r["recovery_accept_time_ns"]) for r in recovery.values()),
        recovery_success=sum(r["terminal_state"] == "COMPLETED" for r in recovery.values()),
        recovery_failed=sum(r["terminal_state"] != "COMPLETED" for r in recovery.values()),
        recovery_paths=dict(Counter(r["chosen_path"] or "REJECTED" for r in recovery.values())),
        normal_ft_sent_bytes=sum(number(f,"sent_bytes") for f in normal_flows),
        recovery_ft_sent_bytes=sum(number(f,"sent_bytes") for f in fault_flows),
        peak_node_storage_bytes=max(number(p,"peak_total_bytes") for p in pools),
        simultaneous_global_storage_peak_bytes=parameters["simultaneous_global_storage_peak_bytes"],
        resume_seconds=distribution([(number(r,"recovery_compute_start_time_ns")-number(r,"cutoff_time_ns"))/NS
            for r in recovery.values() if r["recovery_compute_start_time_ns"]]),
        catchup_seconds=distribution([number(r,"actual_T_catch_ns")/NS for r in recovery.values() if r["actual_T_catch_ns"]]),
        final_leaks=0)
    for key in ("task_execution_waste_wu","normal_protection_eq_wu","reserved_idle_eq_wu","w_waste_actual",
                "primary_actual_wu","recovery_actual_wu","total_executed_wu","useful_work_wu"):
        summary[key] = sum(e[key] for e in execution)
    summary["active_eq_cost"] = summary["task_execution_waste_wu"]+summary["normal_protection_eq_wu"]
    summary["extra_sent_bytes"] = summary["normal_ft_sent_bytes"]+summary["recovery_ft_sent_bytes"]
    summary["lost_work_units"] = sum(number(r,"actual_work_units")-number(r,"resume_work_units") for r in recovery.values())
    summary["failed_task_ids"] = [t["task_id"] for t in task_rows if t["task_success"] != "1"]
    if (root/"link-summary.csv").exists():
        links = rows(root,"link-summary.csv")
        summary.update(mean_link_utilization_percent=sum(float(l["utilization_percent"]) for l in links)/len(links),
            max_full_run_link_utilization_percent=max(float(l["utilization_percent"]) for l in links),
            peak_link_window_utilization_percent=max(float(l["peak_window_utilization_percent"]) for l in links))
    checks = dict(full_input=True,checkpoint_lineage_and_size=True,strict_before_fault_state=True,
        no_tail=True,single_backup_placement=True,physical_network_and_reservations=True,
        storage_and_load_finalization=True,actual_execution_conservation=True,normal_idle_no_double_count=True,
        interval_and_threshold_equations=True,available_state_source_authorized=True)
    fault_events = rows(root,"fault-events.csv") if (root/"fault-events.csv").exists() else []
    starts = [f for f in fault_events if f["event_type"] == "START"]
    def group_recovery(selected):
        return dict(attempted=len(selected),accepted=sum(bool(r["recovery_accept_time_ns"]) for r in selected),
            completed=sum(r["terminal_state"] == "COMPLETED" for r in selected),
            failed=sum(r["terminal_state"] != "COMPLETED" for r in selected),
            lost_work_units=sum(number(r,"actual_work_units")-number(r,"resume_work_units") for r in selected),
            resume_seconds=distribution([(number(r,"recovery_compute_start_time_ns")-number(r,"cutoff_time_ns"))/NS
                for r in selected if r["recovery_compute_start_time_ns"]]),
            catchup_seconds=distribution([number(r,"actual_T_catch_ns")/NS for r in selected if r["actual_T_catch_ns"]]),
            terminal_reasons=dict(Counter(r["terminal_reason"] for r in selected)))
    fault_by_id = {f["fault_id"]:f for f in starts}
    profiles = {}
    for profile in sorted({t["task_profile"] for t in task_rows}):
        selected = [t for t in task_rows if t["task_profile"] == profile]
        costs = [e for e in execution if e["task_profile"] == profile]
        profiles[profile] = dict(tasks=len(selected),completed=sum(t["task_success"] == "1" for t in selected),
            failed=sum(t["task_success"] != "1" for t in selected),
            recovery=group_recovery([r for r in recovery.values() if tasks[r["task_id"]]["task_profile"] == profile]),
            **{k:sum(e[k] for e in costs) for k in ("task_execution_waste_wu","normal_protection_eq_wu","reserved_idle_eq_wu","w_waste_actual")})
    breakdown = dict(profiles=profiles,flow_kinds={kind:dict(flows=sum(f["kind"] == kind for f in flows),
        sent_bytes=sum(number(f,"sent_bytes") for f in flows if f["kind"] == kind)) for kind in sorted({f["kind"] for f in flows})},
        fault_starts=dict(Counter(f["fault_source"] for f in starts)),
        primary_running_victims=dict(Counter(i["fault_type"] for i in impacts
            if i["task_state_before_fault"] == "RUNNING" and i["progress_valid"] == "1")),
        fault_recovery={source:group_recovery([r for r in recovery.values()
            if fault_by_id.get(r["fault_id"],{}).get("fault_source") == source]) for source in sorted({f["fault_source"] for f in starts})},
        recovery_paths={path:group_recovery([r for r in recovery.values() if r["chosen_path"] == path])
            for path in sorted({r["chosen_path"] for r in recovery.values()})},
        failure_reasons=dict(Counter(t["failure_reason"] for t in task_rows if t["task_success"] != "1")))
    diagnostics = dict(H=distribution([float(t["raw_fraction"]) for t in normal.values() if t["raw_fraction"] not in ("", "null")]),
        X=distribution([number(d,"threshold") for d in decisions]),
        X_reasons=dict(Counter(d["threshold_reason"] for d in decisions)),
        unbounded_X_R_decisions=sum(not d["recovery_limit"] for d in decisions),
        restore_processing_ns=distribution([number(r,"restore_processing_ns_included_in_idle") for r in recovery.values()]),
        r_equals_q_at_fault=sum(r["root_work_units"] == r["recoverable_work_units"] for r in recovery.values()),
        r_below_q_at_fault=sum(number(r,"root_work_units") < number(r,"recoverable_work_units") for r in recovery.values()),
        events=dict(Counter(e["event"] for e in events)),
        failed_recoveries=[r for r in recovery.values() if r["terminal_state"] != "COMPLETED"])
    result = dict(status="PASS",checks=checks,samples=dict(tasks=len(tasks),records=len(records),flows=len(flows),
        recoveries=len(recovery),placement_decisions=len(placement)),summary=summary,
        recovery_sampling_note="Resume/catchup include only observed corresponding milestones; failures reported separately.",
        resource_units="physical progress in WU; active/total/normal/idle costs in eq-WU",task_execution=execution,
        breakdown=breakdown,diagnostics=diagnostics)
    write_json(root/"cb-sat-audit.json",result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root",type=Path,required=True)
    args = parser.parse_args()
    try:
        result = audit(args.root.resolve())
        print(json.dumps(result["summary"],indent=2))
    except Exception as error:
        write_json(args.root/"cb-sat-audit.json",dict(status="FAIL",error=str(error)))
        raise


if __name__ == "__main__":
    main()
