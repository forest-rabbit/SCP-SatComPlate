#!/usr/bin/env python3
"""Four new Rational-U runs only; immutable run11 runtime and eleven reused results."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import runpy
import subprocess
import sys

HERE = Path(__file__).resolve().parent
RAT = runpy.run_path(str(HERE / "run-n5c-rational-u.py"))
ROOT, OLD, MATRIX, require = (RAT[k] for k in ("ROOT", "OLD", "MATRIX", "require"))
RUNS, NEW_RUNS = (11, 12, 13, 14, 15), (12, 13, 14, 15)
RATIONAL_HEAD = "99be7b760d04e8ba557ecde83e0c3fb156982950"
GATE_A_HEAD = "625fff908104f4ba14443ed47ee856ec49d40adf"


def source_scope(head):
    changed = OLD["git"]("diff", "--name-only", RATIONAL_HEAD, head).splitlines()
    outside = [p for p in changed if not p.endswith(".md") and not p.startswith("contrib/satcompute/tests/")]
    require(not outside, "frozen Rational-U runtime/inputs changed: " + str(outside))
    for baseline in (OLD["OLD_EXECUTION"], GATE_A_HEAD):
        require(not OLD["git"]("diff", "--name-only", baseline, head, "--", "contrib/satcompute/input",
            "contrib/satcompute/para.cc", "contrib/satcompute/fault", "contrib/satcompute/routing",
            "contrib/satcompute/protection/runtime/recovery-controller.cc",
            "contrib/satcompute/protection/runtime/recovery-controller.h",
            "contrib/satcompute/protection/runtime/recompute-controller.cc",
            "contrib/satcompute/protection/runtime/recompute-controller.h",
            "contrib/satcompute/protection/baseline/recompute/recompute-runtime.cc",
            "contrib/satcompute/protection/baseline/recompute/recompute-runtime.h",
            "contrib/satcompute/protection/runtime/transfer-only-recovery-ledger.cc",
            "contrib/satcompute/protection/runtime/checkpoint-recovery-port.cc",
            "contrib/satcompute/protection/policy/recovery-policy.h",
            "contrib/satcompute/protection/policy/compfrr/frequency"),
            "baseline scene/fault/routing/frequency changed")
    return dict(base=RATIONAL_HEAD, production_and_inputs_identical=True, changed_only_docs_tests=changed)


def references():
    RAT["references"]()  # Reuse both already completed legacy formal equivalence gates.
    original = ROOT / "output/n5c-rational-u"
    proof = json.loads((original / "execution-plan.json").read_text())
    require(proof["commit"] == RATIONAL_HEAD and proof["small_legacy_equivalence"]["status"] == "PASS" and
            proof["small_legacy_equivalence"]["compared_files"] == 425, "run11 fixture evidence missing")
    require(json.loads((original / "audit-status.json").read_text())["status"] == "RATIONAL_U_AUDIT_PASS",
            "run11 independent audit missing")
    out = {}
    for run in RUNS:
        out[str(run)] = {}
        for group in ("full", "noU"):
            directory = ROOT / (f"output/n5c-v4/formal/{OLD['OLD_GROUPS'][group]}" if run == 11 else
                                f"output/n5c-u-audit/run-{run}/{group}")
            commit = OLD["OLD_EXECUTION"] if run == 11 else GATE_A_HEAD
            OLD["verify_execution"](directory, group, run, commit)
            out[str(run)][group] = dict(directory=str(directory), commit=commit)
    directory = original / "run-11/rational-U"
    RAT["verify"](directory, RATIONAL_HEAD)
    out["11"]["rational-U"] = dict(directory=str(directory), commit=RATIONAL_HEAD)
    return out


def prepare(root):
    head = MATRIX["identity"]()
    scope, refs = source_scope(head), references()
    require(not root.exists(), "refuse to overwrite multi-run evidence")
    root.mkdir(parents=True)
    plan = dict(commit=head, seed=1, runs=list(RUNS), new_runs=list(NEW_RUNS), variant="rational-U",
        jobs_limit=4, source_scope=scope, references=refs, new_executions=4, reused_executions=11,
        leave_one_out=dict(unit="one (run,task) instance from both sides; no rerun",
            metrics=["paired_catch_seconds", "total_eq_waste"],
            selectors=["largest_benefit", "largest_absolute"], scopes=["per_run", "pooled"],
            tie_break="ascending run, numeric task ID",
            global_link_storage_hhi="original full trajectories; do not pretend statistical exclusion is a resimulation"),
        automatic_ci_merge_promotion=False)
    (root / "execution-plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    print("RATIONAL_MULTIRUN_PREPARED", head, "four new / eleven reused", flush=True)


def validate_plan(plan):
    require(plan["runs"] == list(RUNS) and plan["new_runs"] == list(NEW_RUNS) and
            plan["variant"] == "rational-U" and plan["new_executions"] == 4 and
            plan["reused_executions"] == 11, "unapproved execution matrix")


def execute(root, jobs, resume=False):
    require(type(jobs) is int and 1 <= jobs <= 4, "one to four parallel simulations only")
    plan = json.loads((root / "execution-plan.json").read_text())
    validate_plan(plan)
    require(MATRIX["identity"]() == plan["commit"] and plan["references"] == references(), "plan/source/reference changed")
    source_scope(plan["commit"])
    def one(run):
        directory = root / f"run-{run}/rational-U"
        require(MATRIX["identity"]() == plan["commit"], "source changed before run")
        if directory.exists():
            require(resume, "existing result, no overwrite")
            RAT["verify"](directory, plan["commit"], run)
            print("REUSE_COMPLETE", run, flush=True)
            return
        print("START", run, flush=True)
        subprocess.run([sys.executable, str(HERE / "run-final-scenario.py"), "--output-dir", str(directory),
            "--protection-mode", "compfrr", "--placement-mode", "n5c", "--input-policy", "deferred",
            "--remote-busy-recovery-policy", "relocate", "--n5c-variant", "rational-U", "--random-run", str(run)],
            cwd=ROOT, check=True)
        require(MATRIX["identity"]() == plan["commit"], "source changed during run")
        RAT["verify"](directory, plan["commit"], run)
        print("FINISHED", run, flush=True)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(one, NEW_RUNS))
    print("FOUR_RUNS_COMPLETE; audit all fifteen then STOP_FOR_USER_REVIEW", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "output/n5c-rational-multirun")
    parser.add_argument("--phase", choices=("prepare", "run", "all"), required=True)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--resume", action="store_true", help="Reuse complete identical results only, never partial runs")
    args = parser.parse_args()
    root = args.root.resolve()
    if args.phase in ("prepare", "all"):
        prepare(root)
    if args.phase in ("run", "all"):
        execute(root, args.jobs, args.resume)


if __name__ == "__main__":
    main()
