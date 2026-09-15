#!/usr/bin/env python3
"""Gate A only: immutable V4, three Deferred placements, five predefined random runs."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
FINAL = runpy.run_path(str(HERE / "run-final-scenario.py"))
MATRIX = runpy.run_path(str(HERE / "run-pre-n5c-placement-matrix.py"))
require = MATRIX["require"]
CHECKPOINT = "f5a479ae36c15e356cddd063235b33318856a86e"
OLD_EXECUTION = "c7f1a91e12f57064646af4f52705724dc27cb990"
RUNS = (11, 12, 13, 14, 15)
MAX_JOBS = 8
GROUPS = {"fa-ffp": ("fa-ffp", "full"), "full": ("n5c", "full"), "noU": ("n5c", "noU")}
OLD_GROUPS = {"fa-ffp": "R7-fa-ffp", "full": "R7-n5c", "noU": "R7-n5c-noU"}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def arguments(output, group, run):
    require(group in GROUPS and run in RUNS, "unapproved group/run")
    placement, variant = GROUPS[group]
    return FINAL["arguments"](output, protection_mode="compfrr", placement_mode=placement,
        input_policy="deferred", remote_busy_recovery_policy="relocate",
        n5c_variant=variant, random_run=run)


def command(output, group, run):
    placement, variant = GROUPS[group]
    return [sys.executable, str(HERE / "run-final-scenario.py"), "--output-dir", str(output),
        "--protection-mode", "compfrr", "--placement-mode", placement,
        "--input-policy", "deferred", "--remote-busy-recovery-policy", "relocate",
        "--n5c-variant", variant, "--random-run", str(run)]


def flags(argv):
    values = {}
    for token in FINAL['canonical_experiment_arguments'](argv)[1:]:
        key, value = token.removeprefix("--").split("=", 1)
        require(key not in values, "duplicate command option")
        values[key] = value
    return values


def verify_execution(directory, group, run, expected_commit):
    e = json.loads((directory / "execution.json").read_text())
    require((e["seed"], e["run"], e["simulation_duration_s"]) == (1, run, 1300), "execution run/horizon mismatch")
    require(e["commit"] == expected_commit and not e["worktree_dirty"], "execution identity mismatch")
    require(e["fault_mode"] == "generate" and e["protection_mode"] == "compfrr" and
        e["input_staging_policy"] == "deferred" and e["remote_busy_recovery_policy"] == "relocate" and
        e["placement_mode"] == GROUPS[group][0] and e.get("n5c_variant", "full") == GROUPS[group][1] and
        not e["audit"] and not e["shadow"], "execution scheme/controls mismatch")
    require(flags(shlex.split(e["command"][-1])) == flags(arguments(directory, group, run)), "frozen command changed")
    require(json.loads((directory / "execution-result.json").read_text())["returncode"] == 0, "incomplete execution")
    return e


def runtime_equivalence(head):
    def changed(a, b):
        return [p for p in git("diff", "--name-only", a, b).splitlines()
                if not p.endswith(".md") and not p.startswith("contrib/satcompute/tests/")]
    require(not changed(CHECKPOINT, head), "Gate A changed production code/inputs/build configuration")
    # The sole production difference after the old formal execution was a help string.
    require(changed(OLD_EXECUTION, CHECKPOINT) == ["contrib/satcompute/satcompute.cc"], "unreviewed historical runtime changes")
    path = "contrib/satcompute/satcompute.cc"
    old = git("show", f"{OLD_EXECUTION}:{path}")
    new = git("show", f"{CHECKPOINT}:{path}")
    require(old.replace('"ffp/lrl minimal, fa-ffp/fa-lrl feasibility-aware"',
        '"ffp/lrl minimal, fa-ffp/fa-lrl feasibility-aware, n5c CompFRR V4"') == new,
        "historical difference was not only CLI help")
    return dict(status="PASS", checkpoint=CHECKPOINT, old_execution=OLD_EXECUTION,
                candidate=head, runtime_and_inputs_unchanged=True, historical_cli_help_only=True)


def fixture_equivalence(first, second):
    files = [p for p in first.rglob("*") if p.suffix in (".csv", ".json")]
    require(files, "empty repeatability fixture")
    require({p.relative_to(first) for p in files} ==
            {p.relative_to(second) for p in second.rglob("*") if p.suffix in (".csv", ".json")},
            "fixture file set differs")
    for p in files:
        other = second / p.relative_to(first)
        if p.suffix == ".csv":
            require(p.read_bytes() == other.read_bytes(), f"fixture differs: {p.relative_to(first)}")
        else:
            require(MATRIX["normalize"](json.loads(p.read_text()), first) ==
                    MATRIX["normalize"](json.loads(other.read_text()), second), f"fixture differs: {p.name}")
    return dict(status="PASS", files=len(files), scope="same-code maintained runtime fixture repeatability; not a new formal run")


def prepare(root, reference):
    head = MATRIX["identity"]()
    runtime = runtime_equivalence(head)
    require(not (root / "execution-plan.json").exists(), "refuse to replace the frozen plan")
    root.mkdir(parents=True, exist_ok=True)
    sources = {}
    for group, name in OLD_GROUPS.items():
        directory = (reference / name).resolve()
        verify_execution(directory, group, 11, OLD_EXECUTION)
        sources[group] = str(directory)
    for label in ("first", "second"):
        directory = root / "equivalence" / label
        require(not directory.exists(), "refuse to overwrite a fixture")
        directory.mkdir(parents=True)
        with (directory / "run.log").open("w") as log:
            subprocess.run([str(ROOT / "ns3"), "run", "--no-build",
                f"satcompute-frequency-runtime-test --outputDir={directory}"],
                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    repeated = fixture_equivalence(root / "equivalence/first", root / "equivalence/second")
    require(MATRIX["identity"]() == head, "source changed during preparation")
    plan = dict(gate="A", commit=head, seed=1, runs=list(RUNS), groups=list(GROUPS),
        reference_run11=sources, new_executions=12, jobs_limit=MAX_JOBS, source_equivalence=runtime,
        deterministic_equivalence=repeated, automatic_gate_b=False,
        decision_rule="Report paired evidence; no T/5% threshold; joint review before Gate B.")
    (root / "execution-plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    (root / "run-11").mkdir(exist_ok=True)
    (root / "run-11/reference.json").write_text(json.dumps(sources, indent=2) + "\n")
    print(json.dumps(dict(status="GATE_A_PREPARED", **plan)), flush=True)


def run_all(root, jobs, resume):
    plan = json.loads((root / "execution-plan.json").read_text())
    head = MATRIX["identity"]()
    require(head == plan["commit"] and plan["runs"] == list(RUNS) and
            plan["groups"] == list(GROUPS), "execution plan changed")
    require(1 <= jobs <= MAX_JOBS, "use at most eight independent simulations")
    runtime_equivalence(head)
    for group, path in plan["reference_run11"].items():
        verify_execution(Path(path), group, 11, OLD_EXECUTION)
    def run(item):
        run_number, group = item
        directory = root / f"run-{run_number}" / group
        require(MATRIX["identity"]() == head, "source changed before execution")
        if directory.exists():
            require(resume, "existing output; use --resume only for completed same-code groups")
            verify_execution(directory, group, run_number, head)
            print("REUSE_COMPLETED", run_number, group, flush=True)
            return
        print("START", run_number, group, flush=True)
        result = subprocess.run(command(directory, group, run_number), cwd=ROOT)
        require(result.returncode == 0, f"failed run {run_number}/{group}")
        require(MATRIX["identity"]() == head, "source changed during execution")
        verify_execution(directory, group, run_number, head)
        print("FINISHED", run_number, group, flush=True)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run, (r, g)) for r in RUNS[1:] for g in GROUPS]
        try:
            for future in as_completed(futures):
                future.result()
        except BaseException:
            for future in futures:
                future.cancel()
            raise
    print("GATE_A_EXECUTIONS_COMPLETE; analyze before joint review, never auto-start Gate B", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--phase", choices=("prepare", "run"), required=True)
    parser.add_argument("--reference", type=Path, default=ROOT / "output/n5c-v4/formal")
    parser.add_argument("--jobs", type=int, default=MAX_JOBS)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.phase == "prepare":
        prepare(args.root.resolve(), args.reference.resolve())
    else:
        run_all(args.root.resolve(), args.jobs, args.resume)


if __name__ == "__main__":
    main()
