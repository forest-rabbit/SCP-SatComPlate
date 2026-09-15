#!/usr/bin/env python3
"""Historical recent-U trial, archived in place; do not launch on production N5R.

Rational-U tooling and maintained tests still import identity/equivalence helpers.
Keep its frozen source guard and old execution metadata; production rejects recent-U.
See docs/n5/reviews/N5R-implementation.md for the dependency/retention audit.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
OLD = runpy.run_path(str(HERE / "run-n5c-u-audit.py"))
MATRIX, FINAL = OLD["MATRIX"], OLD["FINAL"]
require = MATRIX["require"]
RUNS = OLD["RUNS"]
GATE_A_HEAD = "625fff908104f4ba14443ed47ee856ec49d40adf"
RECENT_BASE = "455bde05f45d42c9b7244911ff5272793d0c23d5"


def source_scope(head):
    allowed = {"CMakeLists.txt", "para.h", "satcompute.cc",
        "protection/runtime/compute-usage-history.cc", "protection/runtime/compute-usage-history.h",
        "protection/runtime/n5c-placement-tracker.cc", "protection/runtime/n5c-placement-tracker.h",
        "protection/runtime/frequency-n5c-adapter.cc", "metrics/core/n5c-placement-metrics.cc",
        "protection/policy/compfrr/placement/n5c-placement-policy.cc",
        "protection/policy/compfrr/placement/n5c-placement-policy.h"}
    changed = OLD["git"]("diff", "--name-only", RECENT_BASE, head).splitlines()
    production = {p for p in changed if not p.endswith(".md") and not p.startswith("contrib/satcompute/tests/")}
    require(production <= {"contrib/satcompute/"+p for p in allowed}, "out-of-scope production/input changes")
    return dict(base=RECENT_BASE, unchanged_compute_fault_recovery_routing_inputs=True,
                production_files=sorted(production))


def arguments(directory, variant, run):
    require(variant in ("full", "noU", "recent-U") and run in RUNS, "unapproved variant/run")
    return FINAL["arguments"](directory, protection_mode="compfrr", placement_mode="n5c",
        input_policy="deferred", remote_busy_recovery_policy="relocate",
        n5c_variant=variant, random_run=run)


def verify(directory, variant, run, head):
    value = json.loads((directory / "execution.json").read_text())
    require(value["commit"] == head and not value["worktree_dirty"], "execution identity mismatch")
    require((value["seed"], value["run"], value["simulation_duration_s"]) == (1, run, 1300), "wrong run/horizon")
    require(value["fault_mode"] == "generate" and value["protection_mode"] == "compfrr" and
            value["placement_mode"] == "n5c" and value["n5c_variant"] == variant and
            value["input_staging_policy"] == "deferred" and value["remote_busy_recovery_policy"] == "relocate" and
            not value["audit"] and not value["shadow"], "execution controls mismatch")
    require(OLD["flags"](shlex.split(value["command"][-1])) ==
            OLD["flags"](arguments(directory, variant, run)), "frozen command mismatch")
    require(json.loads((directory / "execution-result.json").read_text())["returncode"] == 0, "incomplete run")
    return value


def references():
    result = {}
    for run in RUNS:
        result[str(run)] = {}
        for group in ("full", "noU"):
            directory = ROOT / (f"output/n5c-v4/formal/{OLD['OLD_GROUPS'][group]}" if run == 11 else
                                f"output/n5c-u-audit/run-{run}/{group}")
            head = OLD["OLD_EXECUTION"] if run == 11 else GATE_A_HEAD
            OLD["verify_execution"](directory, group, run, head)
            result[str(run)][group] = dict(directory=str(directory), commit=head)
    return result


def equivalent(first, second, fixture=False):
    def files(root):
        return {p.relative_to(root) for p in root.rglob("*") if p.suffix in (".csv", ".json")
                and p.name not in ("execution.json", "execution-result.json")}
    old, new = files(first), files(second)
    extra = {p for p in new if fixture and p.parts[0] == "online-n5c-recent-U"}
    require(old and new - extra == old, "legacy output file set changed")
    checked = {}
    for name in sorted(old):
        a, b = first / name, second / name
        if name.suffix == ".csv":
            require(a.read_bytes() == b.read_bytes(), f"legacy CSV changed: {name}")
        else:
            require(MATRIX["normalize"](json.loads(a.read_text()), first) ==
                    MATRIX["normalize"](json.loads(b.read_text()), second), f"legacy JSON changed: {name}")
        checked[str(name)] = "equal"
    return dict(status="PASS", reference=str(first), candidate=str(second), compared=checked,
                extra_recent_fixture_files=len(extra),
                allowed_changes=["output paths", "execution identity", "wall clock"])


def prepare(root):
    head = MATRIX["identity"]()
    scope = source_scope(head)
    require(not (root / "execution-plan.json").exists(), "refuse to overwrite frozen plan")
    refs = references()
    fixture = root / "equivalence/fixture"
    require(not fixture.exists(), "refuse to overwrite fixture")
    fixture.mkdir(parents=True)
    with (fixture / "run.log").open("w") as log:
        subprocess.run([str(ROOT / "ns3"), "run", "--no-build",
            f"satcompute-frequency-runtime-test --outputDir={fixture}"], cwd=ROOT,
            stdout=log, stderr=subprocess.STDOUT, check=True)
    proof = equivalent(ROOT / "output/n5c-u-audit/equivalence/first", fixture, fixture=True)
    require(MATRIX["identity"]() == head, "source changed during fixture gate")
    plan = dict(commit=head, seed=1, runs=list(RUNS), variant="recent-U", jobs_limit=8,
        references=refs, source_scope=scope, small_legacy_equivalence=proof, legacy_formal_gates=["full", "noU"],
        window="exact current primary remaining pure compute ns; past-only live exposure",
        default_promotion=False, automatic_ci_or_merge=False)
    (root / "execution-plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    print("RECENT_U_PREPARED", head, flush=True)


def execute(root, items, head, jobs, resume):
    def run(item):
        directory, variant, run_number = item
        require(MATRIX["identity"]() == head, "source changed before run")
        if directory.exists():
            require(resume, "existing output; only completed identical runs can resume")
            verify(directory, variant, run_number, head)
            print("REUSE", run_number, variant, flush=True)
            return
        print("START", run_number, variant, flush=True)
        subprocess.run([sys.executable, str(HERE / "run-final-scenario.py"),
            "--output-dir", str(directory), "--protection-mode", "compfrr", "--placement-mode", "n5c",
            "--input-policy", "deferred", "--remote-busy-recovery-policy", "relocate",
            "--n5c-variant", variant, "--random-run", str(run_number)], cwd=ROOT, check=True)
        require(MATRIX["identity"]() == head, "source changed during run")
        verify(directory, variant, run_number, head)
        print("FINISHED", run_number, variant, flush=True)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(run, items))


def gates(root, plan, jobs, resume):
    execute(root, [(root / "equivalence" / g, g, 11) for g in ("full", "noU")],
            plan["commit"], min(2, jobs), resume)
    proofs = {g:equivalent(Path(plan["references"]["11"][g]["directory"]), root / "equivalence" / g)
              for g in ("full", "noU")}
    (root / "legacy-equivalence.json").write_text(json.dumps(proofs, indent=2) + "\n")
    print("LEGACY_FORMAL_EQUIVALENCE_PASS", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "output/n5c-recent-u")
    parser.add_argument("--phase", choices=("prepare", "gates", "run", "all"), required=True)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    require(1 <= args.jobs <= 8, "at most eight simultaneous simulations")
    root = args.root.resolve()
    if args.phase in ("prepare", "all"):
        prepare(root)
    plan = json.loads((root / "execution-plan.json").read_text())
    require(MATRIX["identity"]() == plan["commit"] and plan["runs"] == list(RUNS) and
            plan["references"] == references(), "source/plan/reference changed")
    if args.phase in ("gates", "all"):
        gates(root, plan, args.jobs, args.resume)
    if args.phase in ("run", "all"):
        proofs = json.loads((root / "legacy-equivalence.json").read_text())
        require(set(proofs) == {"full", "noU"} and all(p["status"] == "PASS" for p in proofs.values()),
                "legacy equivalence gate missing")
        for g in proofs:
            verify(root / "equivalence" / g, g, 11, plan["commit"])
        execute(root, [(root / f"run-{r}" / "recent-U", "recent-U", r) for r in RUNS],
                plan["commit"], args.jobs, args.resume)
        print("RECENT_U_EXECUTIONS_COMPLETE; audit before user review, no default change", flush=True)


if __name__ == "__main__":
    main()
