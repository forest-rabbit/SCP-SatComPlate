#!/usr/bin/env python3
"""Eight-task G4 causal/equivalence checks; never runs the full frozen scene."""
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / "contrib/satcompute/tools/generation"))
from task_workload_model import image_budget, llm_budget
CHECK = runpy.run_path(str(ROOT / "contrib/satcompute/tools/validation/compfrr-shadow/summarize.py"))


def run(output, trace, profile, shadow, audit=False, f3_time="63"):
    arguments = ["satcompute", "--simulationDuration=100", "--randomSeed=1", "--randomRun=11",
        "--constellationConfig=contrib/satcompute/tests/fixtures/constellation/connected-16.csv",
        f"--taskTrace={trace}", f"--computeProfile={profile}", "--faultMode=generate",
        "--faultEnableF1=1", "--faultEnableF2=0", "--faultEnableF3=1", "--faultF3Mode=controlled",
        "--faultF3Node=3", f"--faultF3Time={f3_time}", "--taskCompletionPolicy=report",
        f"--faultProbabilityAudit={int(audit)}", f"--compfrr-shadow={int(shadow)}", f"--outputDir={output}"]
    completed = subprocess.run([str(ROOT / "ns3"), "run", "--no-build", shlex.join(arguments)],
                               cwd=ROOT, text=True, capture_output=True)
    if completed.returncode:
        raise AssertionError(completed.stdout + completed.stderr)


def main():
    with tempfile.TemporaryDirectory(prefix="satcompute-g4-smoke-") as temporary:
        root = Path(temporary)
        budgets = [image_budget("dense-image", 10**9, "1"), image_budget("dense-image", 10**9, "2"),
                   llm_budget(400, 4000, 1000), image_budget("compression", 10**9, "4"),
                   image_budget("sparse-inference", 10**8, "5"), image_budget("dense-image", 10**9, "6"),
                   llm_budget(600, 8000, 2000), image_budget("sparse-inference", 3 * 10**8, "8")]
        trace, profile = root / "tasks.json", root / "compute.json"
        profile.write_text(json.dumps({"compute_nodes": [{"node_id": 3, "compute_rate_work_units_per_second": 100000}]}))
        trace.write_text(json.dumps({"tasks": [dict(task_id=i, task_profile=b.task_profile,
            input_bytes=b.input_bytes, output_bytes=b.output_bytes, compute_work_units=b.compute_work_units,
            arrival_time_ns=i * 100000000, source_node_id=0, compute_node_id=3, result_node_id=0)
            for i, b in enumerate(budgets, 1)]}))
        off, on, repeat, audit = [root / name for name in ("off", "on", "repeat", "audit")]
        run(off, trace, profile, False)
        run(on, trace, profile, True)
        same = CHECK["compare_business"](off, on)
        assert same["pass"], same
        assert not (off / "shadow").exists()
        assert not (on / "fault-predictions.csv").exists(), "shadow incorrectly requires the audit recorder"
        summary = CHECK["summarize"](on / "shadow")
        assert summary["task_count"] == 8
        assert summary["start_count"] > 0 and summary["initialization_success_count"] > 0
        assert summary["N_L"] > 0 and summary["N_R"] > 0
        assert summary["primary_direct_fault_count"] > 0
        assert len(summary["f3_appendix"]) == 1
        assert summary["delta_changes"] + summary["n_changes"] > 0
        run(repeat, trace, profile, True)
        assert CHECK["compare_business"](on, repeat)["pass"]
        for path in (on / "shadow").iterdir():
            assert path.read_bytes() == (repeat / "shadow" / path.name).read_bytes(), path.name
        # Audit-only switch must not alter the independently queried decisions.
        run(audit, trace, profile, True, audit=True)
        for path in (on / "shadow").iterdir():
            assert path.read_bytes() == (audit / "shadow" / path.name).read_bytes(), path.name
        # Test-only same-ns interruption: the real scheduled fault wins over the
        # later virtual initialization completion, with no partial recovery credit.
        started = [r for r in CHECK["rows"](on / "shadow", "shadow-task-summary.csv") if r["ever_start"] == "true"]
        first = min(started, key=lambda r: int(r["start_time_ns"]))
        at = int(first["init_planned_complete_time_ns"])
        seconds, nanoseconds = divmod(at, 10**9)
        boundary = root / "init-boundary"
        run(boundary, trace, profile, True, f3_time=f"{seconds}.{nanoseconds:09d}")
        check = CHECK["summarize"](boundary / "shadow")
        victim = check["f3_appendix"][0]
        assert victim["task_id"] == first["task_id"]
        assert victim["mode_at_fault"] == "INITIALIZING"
        assert victim["fault_classification"] == "RECOMPUTE_INIT_MISS"
        assert float(victim["normal_cost_before_fault"]) == 0
        assert float(victim["T_catch_shadow"]) == float(victim["T_catch_all_off"])
        print(json.dumps({"shadow_smoke": "passed", "tasks": 8, "starts": summary["start_count"],
                          "primary_modes": summary["primary_modes"], "deterministic": True,
                          "shadow_independent_of_probability_csv": True}))


if __name__ == "__main__":
    main()
