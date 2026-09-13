#!/usr/bin/env python3
"""Joint V7/CB report from actual runs; historical evidence is always read-only."""
import argparse
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
BASE = runpy.run_path(str(HERE / "analyze-baseline-evaluation.py"))
MATRIX = runpy.run_path(str(HERE / "run-v7-jit-matrix.py"))
ROOT = MATRIX["ROOT"]
CB_TOOLS = ROOT / "contrib/satcompute/protection/policy/baseline/checkbullet/tools"
sys.path.insert(0, str(CB_TOOLS))
CB = runpy.run_path(str(CB_TOOLS / "analyze-cb-sat-matrix.py"))
JIT = runpy.run_path(str(HERE / "audit-jit-input.py"))
ADJUST = runpy.run_path(str(CB_TOOLS / "audit-cb-sat-adjustment.py"))
rows, number, require = BASE["rows"], BASE["number"], BASE["require"]
NORMAL = ("INIT_BASE", "INIT_STATE", "L1", "REMOTE_BATCH")
FAULT = ("RECOVERY_INPUT", "RECOVERY_STATE", "RECOVERY_TAIL")


def network_partition(network, jit=None):
    by_kind = network["by_kind"]
    normal = sum(by_kind[k]["sent_bytes"] for k in NORMAL)
    fault = sum(by_kind[k]["sent_bytes"] for k in FAULT)
    if jit is not None:
        require(by_kind.get("PREFETCH_INPUT", {}).get("sent_bytes", 0) == jit["prefetch_total_sent_bytes"],
                "prefetch accounting differs from physical flow union")
        normal += jit["prefetch_before_fault_sent_bytes"]
        fault += jit["prefetch_after_fault_sent_bytes"]
    require(normal + fault == network["extra_sent_bytes"], "normal/fault network partition")
    return normal, fault


def controls(identity):
    ignore = {"outputDir", "faultTrace", "protectionMode", "placementMode", "remoteBusyRecoveryPolicy",
              "inputStagingPolicy", "jitStartBenefit", "fixedProtectionDelta", "fixedProtectionBatchN"}
    return {k: v for k, v in CB["flags"](shlex.split(identity["command"][-1])).items() if k not in ignore}


def compfrr_run(directory):
    result = BASE["analyze"](directory)
    e, s = result["execution"], result["summary"]
    require(not e["worktree_dirty"] and e["fault_mode"] == "generate" and
            result["execution_result"]["returncode"] == 0 and e["simulation_duration_s"] == 1300,
            "nonformal CompFRR identity")
    jit = JIT["audit"](directory) if e["input_staging_policy"] == "jit" else None
    normal, fault = network_partition(result["network"], jit)
    links = rows(directory, "link-summary.csv")
    summary = dict(s, deadline_success=s["on_time"], normal_ft_sent_bytes=normal,
        recovery_ft_sent_bytes=fault, extra_sent_bytes=result["network"]["extra_sent_bytes"],
        mean_link_utilization_percent=result["links"]["mean_utilization_percent"],
        max_full_run_link_utilization_percent=max(float(r["utilization_percent"]) for r in links),
        peak_link_window_utilization_percent=max(float(r["peak_window_utilization_percent"]) for r in links),
        peak_node_storage_bytes=result["storage"]["max_node_peak_bytes"], final_leaks=0)
    row = CB["common_row"](summary, directory.name, "compfrr", e["placement_mode"],
        e["remote_busy_recovery_policy"], e["commit"], str(directory))
    row.update(input_staging=e["input_staging_policy"], evidence_kind="new_formal_execution")
    if jit:
        row.update({key: value for key, value in jit.items() if key.startswith("prefetch_") or key.startswith("fault_")
                    or key in ("ready_reuse_count", "in_flight_reuse_count", "new_recovery_input_bytes")})
    recoveries = result["recovery_rows"]
    rec = dict(group=directory.name, attempted=s["recovery_attempted"], accepted=s["recovery_accepted"],
        success=s["recovery_success"], failed=s["recovery_failure"],
        resume_seconds=CB["distribution"]([(number(r, "recovery_compute_start_time_ns")-number(r, "fault_time_ns"))/1e9
            for r in recoveries if r["recovery_compute_start_time_ns"]]),
        catchup_seconds=result["T_catch_s"], paths=result["recovery_paths"],
        terminal_reasons=result["recovery_reasons"])
    return row, rec, result, jit


def analyze(cb_root, jit_root, output):
    require(not output.exists(), "refusing to overwrite an existing comparison")
    old_public, old_recovery, replicas, old = CB["old_tables"]()
    public, recovery, raw, audits = [], [], {}, {}
    for name in MATRIX["GROUPS"]:
        row, rec, result, audit = compfrr_run(jit_root / name)
        public.append(row); recovery.append(rec); raw[name] = result
        if audit is not None:
            audits[name] = audit
    head = {r["execution_head"] for r in public}
    require(len(head) == 1, "mixed new execution heads")
    equivalence = json.loads((jit_root / "old-mode-equivalence.json").read_text())
    require(set(equivalence) == set(MATRIX["REFERENCES"]), "missing fresh old-mode anchors")
    for name, old_name in MATRIX["REFERENCES"].items():
        proof = equivalence[name]
        require(proof["new_execution"] in head and proof["historical_execution"] == CB["OLD_HEAD"] and
                all(proof["business"]["files"].values()) and all(proof["protection"].values()), "old-mode anchor differs")
        # Recheck the actual source files, not only a claimed PASS in the matrix JSON.
        BASE["ACCOUNTING"]["compare"](CB["OLD_ROOT"] / old_name, jit_root / name)
        for file in proof["protection"]:
            require((CB["OLD_ROOT"] / old_name / file).read_bytes() == (jit_root / name / file).read_bytes(),
                    "old-mode protection evidence differs")
    shared_controls = [controls(r["execution"]) for r in raw.values()]
    for placement, busy in CB["GROUPS"]:
        directory = cb_root / f"CB-{placement}-{busy}"
        e = json.loads((directory / "execution.json").read_text())
        outcome = json.loads((directory / "execution-result.json").read_text())
        require(e["commit"] in head and e["stage"] == "formal" and not e["worktree_dirty"] and
                outcome["returncode"] == 0, "CB execution identity/incomplete matrix")
        # Audit only new output, never call historical_equivalence() on changed V7 shared code.
        audit = CB["AUDIT"]["audit"](directory)
        s = audit["summary"]
        require(audit["samples"]["tasks"] == 800, "CB task count changed")
        row = CB["common_row"](s, directory.name, "checkbullet", placement, busy, e["commit"], str(directory))
        row.update(input_staging="full-input-at-first-H", evidence_kind="new_formal_execution",
                   TF_profile=e["mtbf_seconds"])
        public.append(row)
        recovery.append(dict(group=directory.name, attempted=s["recovery_attempted"], accepted=s["recovery_accepted"],
            success=s["recovery_success"], failed=s["recovery_failed"], resume_seconds=s["resume_seconds"],
            catchup_seconds=s["catchup_seconds"], paths=s["recovery_paths"]))
        shared_controls.append(controls(e))
    require(all(c == shared_controls[0] for c in shared_controls), "new runs have different common scenario controls")
    impact, zero = ADJUST["inspect"](cb_root)
    require(impact["affected_count"] == 0, "new CB still recovers checkpoint without complete INPUT")
    # Historical data is labeled explicitly; only three FA-LRL modes were rerun for equivalence.
    for row in old_public:
        row["evidence_kind"] = "historical_read_only_not_rerun"
    selected = [r for r in public if r["placement"] == "fa-lrl"] + [
        r for r in old_public if r["group"] in ("R0-fa-lrl", "R1-fa-lrl")]
    deltas = []
    for candidate in selected:
        if candidate["group"] not in ("CompFRR-JIT-V7", "CompFRR-JIT-V6START"):
            continue
        for reference in selected:
            if candidate is reference:
                continue
            d = dict(candidate=candidate["group"], reference=reference["group"],
                completed_difference=candidate["completed"]-reference["completed"])
            for key in ("active_eq_cost", "total_eq_waste", "normal_ft_bytes", "recovery_ft_bytes",
                        "extra_sent_bytes", "mean_link_util_percent"):
                d[key+"_change_percent"] = 100*(candidate[key]/reference[key]-1) if reference[key] else None
            deltas.append(d)
    notes = ["Single-seed online-generate system comparisons, not multi-seed statistical evidence.",
        "Fault sets can differ because runtime differs. Aggregate latency is descriptive, not paired-event speedup.",
        "Execution WU is separate from protection/idle eq-WU; idle cost is not CPU work.",
        "Network bytes are actual application payload, including cancelled flows, not byte-hop.",
        "JIT normal/fault partition includes both parts of each original prefetch lifecycle.",
        "CB recompute-on-busy is the main adaptation; relocate is an extension, not an upper bound.",
        "Only R4/R5/R7 FA-LRL have fresh exact-equivalence anchors; other old groups remain historical.",
        "1+1 takeover latency is not checkpoint catchup latency. Zero admitted prefetch uses ratio null."]
    report = dict(execution_head=head.pop(), audit_head=subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        public=public, selected_fa_lrl=selected, recovery=recovery, historical_public=old_public,
        historical_recovery=old_recovery, historical_replica_takeover=replicas,
        deltas=deltas, jit_audits=audits, old_mode_equivalence=equivalence,
        cb_input_readiness=impact, cb_x_zero=zero, notes=notes)
    output.mkdir(parents=True)
    CB["write_json"](output / "comparison.json", report)
    for file, records in (("comparison-public.csv", public), ("comparison-fa-lrl.csv", selected),
                          ("comparison-recovery.csv", recovery), ("comparison-deltas.csv", deltas)):
        CB["csv_table"](output / file, records)
    print(json.dumps(dict(status="PASS", new_runs=len(public), output=str(output))))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cb-root", required=True, type=Path)
    parser.add_argument("--jit-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    analyze(args.cb_root.resolve(), args.jit_root.resolve(), args.output.resolve())
