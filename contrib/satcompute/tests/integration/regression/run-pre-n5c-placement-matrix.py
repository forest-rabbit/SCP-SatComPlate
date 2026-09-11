#!/usr/bin/env python3
"""Frozen 32-run placement ablation; R5/R7 FA gates are part of, not extra to, the matrix."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
from itertools import zip_longest
import json
from pathlib import Path
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
MODES = ("ffp", "lrl", "fa-ffp", "fa-lrl")
SCENARIOS = {
    "R0": ("recompute", "eager", "relocate"),
    "R1": ("one-plus-one", "eager", "relocate"),
    "R2": ("fixed", "eager", "recompute"),
    "R3": ("fixed", "eager", "relocate"),
    "R4": ("compfrr", "eager", "recompute"),
    "R5": ("compfrr", "eager", "relocate"),
    "R6": ("compfrr", "deferred", "recompute"),
    "R7": ("compfrr", "deferred", "relocate"),
}
GATES = ("R5-fa-ffp", "R7-fa-ffp")


def require(value, message):
    if not value:
        raise ValueError(message)


def identity():
    require(not subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT),
            "formal run requires a clean worktree")
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def command(output, group):
    scenario, mode = group.split("-", 1)
    scheme, staging, busy = SCENARIOS[scenario]
    require(mode in MODES, "unknown placement")
    return [sys.executable, str(HERE / "run-final-scenario.py"), "--output-dir", str(output / group),
            "--protection-mode", scheme, "--placement-mode", mode,
            "--input-staging-policy", staging, "--remote-busy-recovery-policy", busy]


def normalize(value, directory):
    if isinstance(value, dict):
        return {k: normalize(v, directory) for k, v in value.items() if k not in ("wall_clock_ns", "wall_clock_s")}
    if isinstance(value, list):
        return [normalize(v, directory) for v in value]
    if isinstance(value, str) and value.startswith(str(directory.resolve())):
        return "OUTPUT" + value[len(str(directory.resolve())):]
    return value


def csv_equivalent(old, new):
    """Stream raw rows; the only CSV rename allowance is the placement_mode field."""
    with old.open() as a, new.open() as b:
        left, right = csv.DictReader(a), csv.DictReader(b)
        require(left.fieldnames == right.fieldnames, f"{old.name}: CSV columns changed")
        count = 0
        for count, (x, y) in enumerate(zip_longest(left, right), 1):
            require(x is not None and y is not None and None not in x and None not in y,
                    f"{old.name}:{count}: missing/malformed row")
            if "placement_mode" in x:
                x["placement_mode"] = {"ffp": "fa-ffp", "lrl": "fa-lrl"}.get(x["placement_mode"], x["placement_mode"])
            require(x == y, f"{old.name}:{count}: behavior changed")
    return count


def verify_gate(old, new):
    require(old.is_dir() and new.is_dir(), "missing historical or new gate run")
    a = json.loads((old / "execution.json").read_text())
    b = json.loads((new / "execution.json").read_text())
    for key in ("seed", "run", "fault_mode", "protection_mode", "input_staging_policy",
                "remote_busy_recovery_policy", "audit", "shadow", "simulation_duration_s"):
        require(a[key] == b[key], f"gate input {key} changed")
    require(a["placement_mode"] == "ffp" and b["placement_mode"] == "fa-ffp", "wrong rename gate")
    require(not a["worktree_dirty"] and not b["worktree_dirty"], "dirty gate run")
    def flags(e):
        result = {}
        for token in shlex.split(e["command"][-1])[1:]:
            key, value = token.lstrip("-").split("=", 1)
            if key not in ("outputDir", "faultTrace", "placementMode"):
                result[key] = value
        return result
    require(flags(a) == flags(b), "gate command parameters changed")
    checked = {}
    for source in sorted(old.iterdir()):
        if source.suffix not in (".csv", ".json") or source.name in ("execution.json", "execution-result.json"):
            continue
        target = new / source.name
        require(target.is_file(), f"missing equivalent file {source.name}")
        if source.suffix == ".csv":
            checked[source.name] = csv_equivalent(source, target)
        else:
            require(normalize(json.loads(source.read_text()), old) == normalize(json.loads(target.read_text()), new),
                    f"{source.name}: JSON behavior changed")
            checked[source.name] = "equal"
    require(json.loads((new / "execution-result.json").read_text())["returncode"] == 0, "gate did not finish")
    return dict(status="PASS", reference=str(old), run=str(new), compared=checked,
                allowed_changes=["placement_mode rename", "output paths", "execution identity", "wall clock"],
                new_diagnostic_files=[p.name for p in new.glob("*.csv") if not (old / p.name).exists()])


def run_batch(output, groups, jobs, head):
    def run(group):
        require(identity() == head, "execution HEAD changed")
        require(not (output / group).exists(), f"refusing to overwrite {group}")
        print(f"START {group}", flush=True)
        result = subprocess.run(command(output, group), cwd=ROOT)
        require(result.returncode == 0, f"run failed: {group}")
        print(f"FINISHED {group}", flush=True)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        pending = [pool.submit(run, g) for g in groups]
        try:
            for future in as_completed(pending):
                future.result()
        except BaseException:
            for f in pending:
                f.cancel()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "output/pre-n5c-placement-final")
    parser.add_argument("--reference-dir", type=Path, default=ROOT / "output/n5-on-capacity-resume")
    parser.add_argument("--stage", choices=("gates", "remaining", "all"), default="all")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    require(1 <= args.jobs <= 16, "jobs must be between 1 and 16")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    head = identity()
    if args.stage in ("gates", "all"):
        run_batch(output, GATES, min(2, args.jobs), head)
    proofs = {}
    for group, old in zip(GATES, ("R5-eager-relocate-busy", "R7-deferred-relocate-busy")):
        e = json.loads((output / group / "execution.json").read_text())
        require(e["commit"] == head, "gate and matrix HEAD differ")
        proofs[group] = verify_gate(args.reference_dir / old, output / group)
    (output / "rename-equivalence.json").write_text(json.dumps(proofs, indent=2) + "\n")
    print("FA historical reproduction gates PASS", flush=True)
    if args.stage in ("remaining", "all"):
        groups = [f"{scenario}-{mode}" for scenario in SCENARIOS for mode in MODES if f"{scenario}-{mode}" not in GATES]
        run_batch(output, groups, args.jobs, head)
    print(json.dumps({"stage": args.stage, "commit": head, "status": "PASS"}), flush=True)


if __name__ == "__main__":
    main()
