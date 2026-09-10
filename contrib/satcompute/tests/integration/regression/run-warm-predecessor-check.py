#!/usr/bin/env python3
"""Two real tasks with frozen orbit/fault parameters; not a formal algorithm run."""
import argparse
import csv
import json
from pathlib import Path
import runpy
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[5]
RUN = runpy.run_path(str(Path(__file__).with_name("run-final-scenario.py")))


def rows(directory, name):
    with (directory / name).open() as stream:
        return list(csv.DictReader(stream))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("refusing to overwrite existing evidence")
    output.mkdir(parents=True)
    source = json.loads((ROOT / RUN["SCENE"] / "workload/task-trace.json").read_text())
    source["tasks"] = [t for t in source["tasks"] if t["task_id"] in (120, 801)]
    trace = output / "two-tasks.json"
    trace.write_text(json.dumps(source, indent=2) + "\n")
    command = RUN["arguments"](output, audit=True)
    command = [f"--taskTrace={trace}" if a.startswith("--taskTrace=") else a for a in command]
    with (output / "run.log").open("w") as log:
        subprocess.run([str(ROOT / "ns3"), "run", "--no-build", shlex.join(command)], cwd=ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    tasks = {int(r["task_id"]): r for r in rows(output, "task-summary.csv")}
    warm, target = tasks[801], tasks[120]
    assert warm["final_state"] == "COMPLETED", warm
    assert int(warm["compute_service_time_ns"]) == 6_000_000_000
    gap = int(target["compute_start_time_ns"]) - int(warm["compute_complete_time_ns"])
    assert 0 <= gap < 100_000_000, gap
    states = [r for r in rows(output, "fault-model-state.csv") if r["node_id"] == "62"
              and int(target["compute_start_time_ns"]) <= int(r["simulation_time_ns"]) <= 1027055770726]
    assert states and all(float(r["temperature_c"]) > 20 and float(r["p_f1"]) > 0 for r in states)
    assert int(target["failure_time_ns"]) == 1027055770726, target
    evidence = {"tasks": 2, "warmup_bytes": int(warm["input_bytes"]),
                "warmup_compute_seconds": 6, "gap_ns": gap,
                "target_start_ns": int(target["compute_start_time_ns"]),
                "target_failure_ns": int(target["failure_time_ns"]), "target_state_samples": states,
                "parameters": "unchanged production F1/F2/F3; protection off; audit only here"}
    (output / "warmup-check.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
