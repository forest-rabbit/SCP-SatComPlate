#!/usr/bin/env python3
"""One approved run11 Rational-U trial; reuse completed legacy gates, never expand."""
import argparse
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
RECENT = runpy.run_path(str(HERE.parents[1] / "support/protection/historical_recent_evidence.py"))
ROOT, OLD, MATRIX, FINAL = (RECENT[k] for k in ("ROOT", "OLD", "MATRIX", "FINAL"))
require = MATRIX["require"]
BASE_HEAD = "a19b722694948bda395cf95b96cd27609daf6c91"


def arguments(directory, random_run=11):
    require(type(random_run) is int and 11 <= random_run <= 15, "unapproved Rational-U run")
    return FINAL["arguments"](directory, protection_mode="compfrr", placement_mode="n5c",
        input_policy="deferred", remote_busy_recovery_policy="relocate",
        n5c_variant="rational-U", random_run=random_run)


def verify(directory, head, random_run=11):
    directory = directory.resolve()
    value = json.loads((directory / "execution.json").read_text())
    require(value["commit"] == head and not value["worktree_dirty"], "execution identity mismatch")
    require((value["seed"], value["run"], value["simulation_duration_s"]) == (1, random_run, 1300), "wrong run/horizon")
    require(value["fault_mode"] == "generate" and value["protection_mode"] == "compfrr" and
            value["placement_mode"] == "n5c" and value["n5c_variant"] == "rational-U" and
            value["input_staging_policy"] == "deferred" and value["remote_busy_recovery_policy"] == "relocate" and
            not value["audit"] and not value["shadow"], "execution controls mismatch")
    require(OLD["flags"](shlex.split(value["command"][-1])) == OLD["flags"](arguments(directory, random_run)),
            "frozen command mismatch")
    require(json.loads((directory / "execution-result.json").read_text())["returncode"] == 0, "incomplete run")
    return value


def references():
    proofs = json.loads((ROOT / "output/n5c-recent-u/legacy-equivalence.json").read_text())
    require(set(proofs) == {"full", "noU"}, "wrong reused gate set")
    out = {}
    for group, proof in proofs.items():
        require(proof["status"] == "PASS" and len(proof["compared"]) == 35 and
                all(v == "equal" for v in proof["compared"].values()), "legacy formal equivalence missing")
        reference = ROOT / f"output/n5c-v4/formal/{OLD['OLD_GROUPS'][group]}"
        gate = ROOT / f"output/n5c-recent-u/equivalence/{group}"
        require(Path(proof["reference"]) == reference and Path(proof["candidate"]) == gate, "wrong gate source")
        RECENT["verify"](gate, group, 11, BASE_HEAD)
        OLD["verify_execution"](reference, group, 11, OLD["OLD_EXECUTION"])
        out[group] = dict(directory=str(reference), commit=OLD["OLD_EXECUTION"], reused_gate=str(gate),
                          gate_commit=BASE_HEAD, compared_files=35)
    return out


def scope(head):
    allowed = {"para.h", "satcompute.cc", "metrics/core/n5c-placement-metrics.cc",
        "protection/runtime/compute-usage-history.h", "protection/runtime/compute-usage-history.cc",
        "protection/runtime/n5c-placement-tracker.cc",
        "protection/policy/compfrr/placement/n5c-placement-policy.h",
        "protection/policy/compfrr/placement/n5c-placement-policy.cc"}
    changed = OLD["git"]("diff", "--name-only", BASE_HEAD, head).splitlines()
    production = {p for p in changed if not p.endswith(".md") and not p.startswith("contrib/satcompute/tests/")}
    require(production <= {"contrib/satcompute/" + p for p in allowed}, "out-of-scope production/input changes")
    return dict(base=BASE_HEAD, changed_production=sorted(production),
        unchanged_inputs_faults_frequency_recovery_routing=True)


def fixture_equivalence(first, second):
    def files(root):
        return {p.relative_to(root) for p in root.rglob("*") if p.suffix in (".csv", ".json")
                and p.name not in ("execution.json", "execution-result.json")}
    old, new = files(first), files(second)
    extra = {p for p in new if p.parts[0] == "online-n5c-rational-U"}
    require(old and new - extra == old and extra, "legacy fixture file set changed / no new fixture")
    for name in sorted(old):
        a, b = first / name, second / name
        if name.suffix == ".csv":
            require(a.read_bytes() == b.read_bytes(), f"legacy CSV changed: {name}")
        else:
            require(MATRIX["normalize"](json.loads(a.read_text()), first) ==
                    MATRIX["normalize"](json.loads(b.read_text()), second), f"legacy JSON changed: {name}")
    return dict(status="PASS", reference=str(first), candidate=str(second),
        compared_files=len(old), new_rational_fixture_files=len(extra),
        includes_unchanged_recent_variant=True, allowed_changes=["output paths", "execution identity", "wall clock"])


def prepare(root):
    head = MATRIX["identity"]()
    production, refs = scope(head), references()
    require(not root.exists(), "refuse to overwrite trial evidence")
    b0 = ROOT / "output/n5c-u-freshness-snapshot/summary.json"
    snapshot = json.loads(b0.read_text())
    require(snapshot["source_execution"]["commit"] == OLD["OLD_EXECUTION"] and
            snapshot["source_execution"]["n5c_variant"] == "full", "B0 reference mismatch")
    fixture = root / "equivalence/fixture"
    fixture.mkdir(parents=True)
    with (fixture / "run.log").open("w") as log:
        subprocess.run([str(ROOT / "ns3"), "run", "--no-build",
            f"satcompute-frequency-runtime-test --outputDir={fixture}"], cwd=ROOT,
            stdout=log, stderr=subprocess.STDOUT, check=True)
    proof = fixture_equivalence(ROOT / "output/n5c-recent-u/equivalence/fixture", fixture)
    subprocess.run([sys.executable, str(HERE / "analyze-n5c-placement.py"), "--fixtures", str(fixture)], check=True)
    require(MATRIX["identity"]() == head, "source changed during fixture gate")
    plan = dict(commit=head, seed=1, run=11, variant="rational-U", source_scope=production,
        references=refs, reused_legacy_formal_gates=2, new_formal_executions=1,
        small_legacy_equivalence=proof, B0_snapshot=str(b0), B0_summary=snapshot,
        default_promotion=False, automatic_ci_or_merge=False)
    (root / "execution-plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    print("RATIONAL_U_PREPARED", head, proof["compared_files"], flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "output/n5c-rational-u")
    parser.add_argument("--phase", choices=("prepare", "run", "all"), required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    if args.phase in ("prepare", "all"):
        prepare(root)
    if args.phase in ("run", "all"):
        plan = json.loads((root / "execution-plan.json").read_text())
        require(MATRIX["identity"]() == plan["commit"] and plan["run"] == 11 and
                plan["variant"] == "rational-U" and plan["references"] == references(), "plan/source/reference changed")
        require(plan["small_legacy_equivalence"]["status"] == "PASS", "fixture gate missing")
        directory = root / "run-11/rational-U"
        require(not directory.exists(), "single new formal run only; no overwrite or automatic retry")
        subprocess.run([sys.executable, str(HERE / "run-final-scenario.py"), "--output-dir", str(directory),
            "--protection-mode", "compfrr", "--placement-mode", "n5c", "--input-policy", "deferred",
            "--remote-busy-recovery-policy", "relocate", "--n5c-variant", "rational-U", "--random-run", "11"],
            cwd=ROOT, check=True)
        require(MATRIX["identity"]() == plan["commit"], "source changed during run")
        verify(directory, plan["commit"])
        print("RATIONAL_U_EXECUTION_COMPLETE; audit then stop for user review", flush=True)


if __name__ == "__main__":
    main()
