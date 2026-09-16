"""Read-only identity/equivalence helpers for retired recent-U experiment evidence.

No simulation entry point, subprocess execution or production placement policy.
"""
import json
from pathlib import Path
import runpy
import shlex

HERE = Path(__file__).resolve().parents[2] / "integration/regression"
ROOT = HERE.parents[4]
OLD = runpy.run_path(str(HERE / "run-n5c-u-audit.py"))
MATRIX, FINAL = OLD["MATRIX"], OLD["FINAL"]
require = MATRIX["require"]
RUNS = OLD["RUNS"]
GATE_A_HEAD = "625fff908104f4ba14443ed47ee856ec49d40adf"

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
