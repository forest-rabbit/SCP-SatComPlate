#!/usr/bin/env python3
"""Frozen FA-LRL: two named JIT groups plus R4/R5/R7 old-mode equivalence anchors."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import runpy
import subprocess
import sys

HERE = Path(__file__).resolve().parent
SCENE = runpy.run_path(str(HERE / "run-final-scenario.py"))
ROOT = SCENE["ROOT"]
GROUPS = {
    "CompFRR-JIT-V6START": ("jit", "relocate", False),
    "CompFRR-JIT-V7": ("jit", "relocate", True),
    "reference-R4-eager-recompute": ("eager", "recompute", True),
    "reference-R5-eager-relocate": ("eager", "relocate", True),
    "reference-R7-deferred-relocate": ("deferred", "relocate", True),
}
REFERENCES = dict(zip(tuple(GROUPS)[2:], ("R4-fa-lrl", "R5-fa-lrl", "R7-fa-lrl")))


def require(value, message):
    if not value:
        raise ValueError(message)


def clean_head():
    require(not subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT), "formal run requires clean HEAD")
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def command(output, group):
    staging, busy, benefit = GROUPS[group]
    # Validate against the sole frozen-scenario argument source before launching.
    SCENE["arguments"](output, protection_mode="compfrr", placement_mode="fa-lrl",
                       remote_busy_recovery_policy=busy, input_staging_policy=staging, jit_start_benefit=benefit)
    return [sys.executable, str(HERE / "run-final-scenario.py"), "--output-dir", str(output),
            "--protection-mode", "compfrr", "--placement-mode", "fa-lrl",
            "--remote-busy-recovery-policy", busy, "--input-staging-policy", staging,
            "--jit-start-benefit", str(int(benefit))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--jobs", type=int, choices=range(1, 6), default=2)
    args = parser.parse_args()
    head = clean_head()
    output = args.output_root.resolve()
    require(not output.exists(), "refusing to overwrite existing matrix")
    commands = {name: command(output / name, name) for name in GROUPS}
    output.mkdir(parents=True)
    statuses = {}
    def execute(name):
        require(clean_head() == head, "HEAD changed during matrix")
        print(f"START {name}", flush=True)
        process = subprocess.run(commands[name], cwd=ROOT, text=True, capture_output=True)
        (output / f"{name}.runner.log").write_text(process.stdout + process.stderr)
        require(clean_head() == head, "execution HEAD/worktree changed")
        print(f"FINISHED {name}: exit {process.returncode}", flush=True)
        return process.returncode
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(execute, name): name for name in GROUPS}
        for future in as_completed(futures):
            name = futures[future]
            try:
                statuses[name] = {"returncode": future.result()}
            except Exception as error:
                statuses[name] = {"error": str(error)}
    (output / "matrix-status.json").write_text(json.dumps(dict(commit=head, groups=statuses), indent=2)+"\n")
    require(all(s.get("returncode") == 0 for s in statuses.values()), f"matrix failed; evidence retained at {output}")
    audit = runpy.run_path(str(HERE / "audit-jit-input.py"))["audit"]
    for name in tuple(GROUPS)[:2]:
        (output / name / "v7-jit-independent-audit.json").write_text(json.dumps(audit(output/name), indent=2)+"\n")
    compare = runpy.run_path(str(HERE / "analyze-protection-accounting.py"))["compare"]
    equivalence = {}
    for name, old_name in REFERENCES.items():
        old = ROOT / "output/pre-n5c-placement-final" / old_name
        new = output / name
        result = compare(old, new)
        extra = {}
        for file in ("protection-events.csv", "protection-transfers.csv", "protection-task-summary.csv",
                     "protection-node-storage-summary.csv", "recovery-summary.csv", "recovery-events.csv",
                     "frequency-decisions.csv", "frequency-pause-intervals.csv", "frequency-capacity-waits.csv"):
            extra[file] = (old/file).read_bytes() == (new/file).read_bytes()
        equivalence[name] = dict(business=result, protection=extra,
            historical_execution=json.loads((old/"execution.json").read_text())["commit"], new_execution=head)
    (output/"old-mode-equivalence.json").write_text(json.dumps(equivalence, indent=2)+"\n")
    require(all(all(e["protection"].values()) for e in equivalence.values()),
            "old-mode protection changed; inspect evidence before accepting comparisons")
    print(output, flush=True)


if __name__ == "__main__":
    main()
