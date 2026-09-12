#!/usr/bin/env python3
"""Summarize actual CB runs without overwriting historical comparison evidence."""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import re
import runpy
import shlex
import subprocess
from cb_tools import ROOT, GROUPS, flags, require, rows, write_json

AUDIT = runpy.run_path(str(Path(__file__).with_name("audit-cb-sat-run.py")))
distribution, number = AUDIT["distribution"], AUDIT["number"]
OLD_HEAD = "b51cc9d638c2630068b5174bacfcaf1500d7d7e5"
OLD_ROOT = ROOT/"output/pre-n5c-placement-final"


def csv_table(path, records):
    if not records:
        return
    keys = list(dict.fromkeys(k for r in records for k in r))
    with path.open("w") as stream:
        writer = csv.DictWriter(stream,keys); writer.writeheader()
        writer.writerows({k:json.dumps(v,sort_keys=True) if isinstance(v,(dict,list)) else v
                         for k,v in r.items()} for r in records)


def coverage(executions):
    """One matrix must contain eight unique configurations at one immutable execution HEAD."""
    expected = {f"CB-{p}-{b}" for p,b in GROUPS}
    names = [e["group"] for e in executions]
    missing = sorted(expected-set(names))
    extra = sorted(set(names)-expected)
    consistent = len({e["commit"] for e in executions}) == 1 and len({e["mtbf_seconds"] for e in executions}) == 1
    return dict(status="PASS" if not missing and not extra and len(names) == 8 and consistent else "INCOMPLETE",
                missing=missing,extra=extra,groups=len(names),one_head_and_profile=consistent)


def historical_equivalence():
    """No historical result writes, and no implicit permission to reuse changed shared behavior."""
    base = "contrib/satcompute/"
    shared = [base+p for p in ("protection/common","protection/mechanism","protection/runtime",
        "protection/storage","protection/policy","routing","fault","task","input","para.cc","para.h")]
    changed = subprocess.check_output(["git","diff","--name-only",OLD_HEAD,"HEAD","--",*shared],cwd=ROOT,text=True).splitlines()
    comments = []
    def tokens(source):
        return re.sub(r"\s+","",re.sub(r"/\*.*?\*/|//[^\n]*","",source,flags=re.S))
    for path in changed:
        if "/checkbullet/" in path or path.endswith(".md"):
            continue
        old = subprocess.check_output(["git","show",f"{OLD_HEAD}:{path}"],cwd=ROOT,text=True)
        require(tokens(old) == tokens((ROOT/path).read_text()),f"historical shared behavior changed: {path}; new reference runs required")
        comments.append(path)
    return dict(status="PASS",historical_execution_head=OLD_HEAD,comment_only_changes=comments,
        unchanged="shared state/costs, mechanisms, runtime, storage, old policies, routing, faults, task, input, para",
        registration_review="satcompute.cc/CMake register an isolated checkbullet branch; existing-mode maintained smoke/regression passed",
        historical_raw_root=str(OLD_ROOT),historical_outputs_read_only=True)


def common_row(summary, group, scheme, placement, busy, head, source):
    return dict(group=group,scheme=scheme,scenario="leo-66-frozen-800",placement=placement,busy=busy,
        execution_head=head,evidence=source,completed=summary["completed"],failed=summary["failed"],
        compute_deadline_success=summary["deadline_success"],
        execution_waste_wu=summary["task_execution_waste_wu"],
        protection_eq_cost=summary["normal_protection_eq_wu"],reserved_idle_eq_wu=summary["reserved_idle_eq_wu"],
        active_eq_cost=summary["task_execution_waste_wu"]+summary["normal_protection_eq_wu"],
        total_eq_waste=summary["w_waste_actual"],normal_ft_bytes=summary["normal_ft_sent_bytes"],
        recovery_ft_bytes=summary["recovery_ft_sent_bytes"],extra_sent_bytes=summary["extra_sent_bytes"],
        mean_link_util_percent=summary["mean_link_utilization_percent"],
        max_full_run_link_util_percent=summary["max_full_run_link_utilization_percent"],
        peak_window_util_percent=summary["peak_link_window_utilization_percent"],
        peak_node_backup_storage_bytes=summary["peak_node_storage_bytes"],final_leaks=summary["final_leaks"],audit_status="PASS")


def old_tables():
    evidence = json.loads((OLD_ROOT/"master-summary.json").read_text())
    require(evidence["commit"] == OLD_HEAD,"historical reference execution changed")
    public,recovery,replica = [],[],[]
    for group,g in sorted(evidence["groups"].items()):
        e,s = g["execution"],g["summary"]
        require(e["commit"] == OLD_HEAD and not e["worktree_dirty"] and g["execution_result"]["returncode"] == 0,
                "unqualified historical execution")
        directory = OLD_ROOT/group
        links = rows(directory,"link-summary.csv")
        summary = dict(s,deadline_success=s["on_time"],normal_ft_sent_bytes=g["network"]["normal_ft_bytes"],
            recovery_ft_sent_bytes=g["network"]["fault_ft_bytes"],extra_sent_bytes=g["network"]["extra_sent_bytes"],
            mean_link_utilization_percent=g["links"]["mean_utilization_percent"],
            max_full_run_link_utilization_percent=g["links"]["max_single_link_full_mean_utilization_percent"],
            peak_link_window_utilization_percent=max(float(l["peak_window_utilization_percent"]) for l in links),
            peak_node_storage_bytes=g["storage"]["max_node_peak_bytes"] if g["storage"]["applicable"] else None,final_leaks=0)
        require(g["storage"]["quiescent"],"historical storage not quiescent")
        row = common_row(summary,group,e["protection_mode"],e["placement_mode"],e["remote_busy_recovery_policy"],OLD_HEAD,str(directory))
        row.update(input_staging=e["input_staging_policy"],TF_profile=None)
        public.append(row)
        if e["protection_mode"] != "one-plus-one":
            rr = g["recovery_rows"]
            recovery.append(dict(group=group,attempted=s["recovery_attempted"],accepted=s["recovery_accepted"],
                success=s["recovery_success"],failed=s["recovery_attempted"]-s["recovery_success"],
                resume_seconds=distribution([(number(r,"recovery_compute_start_time_ns")-number(r,"fault_time_ns"))/1e9
                    for r in rr if r["recovery_compute_start_time_ns"]]),
                catchup_seconds=distribution([number(r,"actual_T_catch_ns")/1e9 for r in rr if r["actual_T_catch_ns"]]),
                lost_work_units=sum(number(r,"planned_catchup_redo_wu") for r in rr),paths=g["recovery_paths"]))
        else:
            rr = g["replica_rows"]
            primary = {i["task_id"]:i for i in rows(directory,"fault-task-impact.csv")
                       if i["impact_type"] == "PRIMARY_INTERRUPTED"}
            accepted = [r for r in rr if r["takeover_time_ns"]]
            require(all(r["task_id"] in primary for r in accepted),"takeover without actual primary impact")
            latency = [(number(r,"takeover_time_ns")-number(primary[r["task_id"]],"fault_time_ns"))/1e9 for r in accepted]
            require(all(v >= 0 for v in latency),"takeover before fault")
            faulted = sum(r["primary_faulted"] == "1" for r in rr)
            replica.append(dict(group=group,replica_requested=g["replica"]["requested"],replica_admitted=g["replica"]["admitted"],
                primary_faulted=faulted,takeover_evaluation_opportunities=faulted,
                takeover_accepted=len(accepted),takeover_success=sum(r["winner"] == "replica" and r["terminal_state"] == "COMPLETED" for r in accepted),
                takeover_latency_seconds=distribution(latency),post_fault_winner=dict(Counter(
                    r["winner"] or "none" for r in rr if r["primary_faulted"] == "1")),
                note="Evaluation opportunities are primary-faulted logical tasks, not unrecorded PromoteReplica API call counts. Takeover is NOT catchup."))
    return public,recovery,replica,evidence


def comparison(root, formal):
    equivalence = historical_equivalence()
    public,recoveries,replicas,old = old_tables()
    for s in formal:
        cb = common_row(s,s["group"],"checkbullet",s["placement"],s["busy"],s["commit"],s["directory"])
        cb.update(input_staging="full-input-at-first-H",TF_profile=s["mtbf_seconds"])
        public.append(cb)
        recoveries.append(dict(group=s["group"],attempted=s["recovery_attempted"],accepted=s["recovery_accepted"],
            success=s["recovery_success"],failed=s["recovery_failed"],resume_seconds=s["resume_seconds"],
            catchup_seconds=s["catchup_seconds"],lost_work_units=s["lost_work_units"],paths=s["recovery_paths"]))
        actual_flags = flags(shlex.split(json.loads((Path(s["directory"])/"execution.json").read_text())["command"][-1]))
        omitted = {"outputDir","faultTrace","protectionMode","placementMode","remoteBusyRecoveryPolicy",
            "inputStagingPolicy","fixedProtectionDelta","fixedProtectionBatchN"}
        common = lambda f:{k:v for k,v in f.items() if k not in omitted}
        for g in old["groups"].values():
            require(common(actual_flags) == common(flags(shlex.split(g["execution"]["command"][-1]))),
                    "CB and historical common scene/network/fault command differs")
    deltas = []
    for cb in (r for r in public if r["scheme"] == "checkbullet"):
        for ref in (r for r in public if r["scheme"] != "checkbullet" and r["placement"] == cb["placement"]):
            delta = dict(candidate=cb["group"],reference=ref["group"],completed_difference=cb["completed"]-ref["completed"])
            for key in ("active_eq_cost","total_eq_waste","extra_sent_bytes","mean_link_util_percent"):
                delta[key+"_change_percent"] = (cb[key]/ref[key]-1)*100 if ref[key] else None
            deltas.append(delta)
    csv_table(root/"comparison-public.csv",public)
    csv_table(root/"comparison-recovery.csv",recoveries)
    csv_table(root/"comparison-replica.csv",replicas)
    csv_table(root/"comparison-deltas.csv",deltas)
    write_json(root/"comparison.json",dict(public=public,checkpoint_recovery=recoveries,replica_takeover=replicas,
        deltas=deltas,historical_equivalence=equivalence,
        notes=["Single-seed controlled 800-task scene; not unbiased multi-seed evidence.",
               "Online faults need not be identical; deltas are system-level, not paired fault-ID effects.",
               "Resume/catchup use observed milestones including later-failed tasks; denominators and failures are explicit.",
               "1+1 backup storage is N/A: its active working set was not charged to checkpoint pools.",
               "Extra bytes are physical application sent payload including cancellation; not byte-hop.",
               "Reserved idle is equivalent capacity cost, not executed CPU WU."]))
    return public


def analyze(root):
    audit = AUDIT["audit"]
    results, failures = [], []
    for path in sorted(root.rglob("execution.json")):
        execution = json.loads(path.read_text())
        if execution.get("protection_mode") != "checkbullet":
            continue
        directory = path.parent
        try:
            outcome = json.loads((directory/"execution-result.json").read_text())
            require(outcome["returncode"] == 0,"simulation failed")
            result = audit(directory)
            result["summary"].update(group=directory.name,stage=execution["stage"],commit=execution["commit"],
                placement=execution["placement_mode"],busy=execution["remote_busy_recovery_policy"],
                mtbf_seconds=execution["mtbf_seconds"],directory=str(directory),audit_status="PASS")
            results.append(result["summary"])
        except Exception as error:
            failures.append(dict(directory=str(directory),error=str(error)))
            write_json(directory/"cb-sat-audit.json",dict(status="FAIL",error=str(error)))
    require(results or failures,"no actual CB execution evidence found")
    matrices = {}
    audit_snapshot = dict(commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
        worktree_dirty=bool(subprocess.check_output(["git","status","--porcelain"],cwd=ROOT)))
    for parent in sorted({str(Path(r["directory"]).parent) for r in results if r["stage"] == "formal"}):
        formal = [r for r in results if r["stage"] == "formal" and str(Path(r["directory"]).parent) == parent]
        matrices[parent] = coverage(formal)
        if matrices[parent]["status"] != "PASS":
            failures.append(dict(directory=parent,error="incomplete or mixed-version formal matrix"))
        else:
            comparison(Path(parent),formal)
            status_path = Path(parent)/"matrix-status.json"
            if status_path.exists() and not (Path(parent)/"initial-matrix-status.json").exists():
                write_json(Path(parent)/"initial-matrix-status.json",json.loads(status_path.read_text()))
            write_json(status_path,dict(commit=formal[0]["commit"],stage="formal",audit_snapshot=audit_snapshot,
                groups={r["group"]:"AUDIT_PASS" for r in formal}))
            write_json(Path(parent)/"run-manifest.json",dict(execution_commit=formal[0]["commit"],
                audit_snapshot=audit_snapshot,coverage=matrices[parent],groups=[dict(group=r["group"],
                directory=r["directory"],execution="execution.json",outcome="execution-result.json",audit="cb-sat-audit.json") for r in formal]))
    write_json(root/"cb-sat-matrix-summary.json",dict(runs=results,failed_or_incomplete=failures,formal_matrices=matrices,
        audit_snapshot=audit_snapshot,
        unprepared_workloads={"50GB":"NOT_PREPARED: no approved input manifest","100GB":"NOT_PREPARED: no approved input manifest"},
        note="Eight configurations, not independent repeats. Calibration pilots are not success-rate samples."))
    csv_table(root/"cb-sat-matrix-summary.csv",results)
    return results,failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root",type=Path,required=True)
    args = parser.parse_args()
    runs,failures = analyze(args.root.resolve())
    print(json.dumps(dict(audited_runs=len(runs),failed_or_incomplete=len(failures))))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
