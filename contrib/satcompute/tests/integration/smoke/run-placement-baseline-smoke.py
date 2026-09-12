#!/usr/bin/env python3
"""Four placement modes across native replica/recompute and checkpoint controllers."""
import json
from pathlib import Path
import runpy
import tempfile

API = runpy.run_path(str(Path(__file__).with_name("run-frequency-smoke.py")))


def verify(root):
    count = 0
    for scheme in ("fixed", "compfrr", "recompute", "one-plus-one"):
        for mode in ("ffp", "lrl", "fa-ffp", "fa-lrl"):
            directory = root / f"{scheme}-{mode}"
            API["run"](directory, mode=scheme, placement=mode)
            rows = API["rows"]
            assert len(rows(directory, "task-summary.csv")) == 4
            selections = rows(directory, "placement-selections.csv")
            assert all(r["placement_mode"] == mode and r["selected_by_minimal_policy"] ==
                       str(int(not mode.startswith("fa-"))) for r in selections)
            assert len({(r["task_id"], r["time_ns"]) for r in selections}) == len(selections)
            if scheme != "compfrr":
                assert len({r["task_id"] for r in selections}) == len(selections)
            if scheme in ("recompute", "one-plus-one"):
                assert all(not r["local_node"] and not r["remote_node"] for r in selections)
            else:
                assert all(not r["backup_node"] for r in selections)
            for r in rows(directory, "placement-node-summary.csv"):
                assert int(r["active_backup_assignments"]) == int(r["active_recoveries"]) == 0
            final = json.loads((directory / "protection-finalization.json").read_text())
            assert final["quiescent"]
            count += 1
    return count


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="scp-placement-smoke-") as directory:
        print(json.dumps({"placement_smoke_groups": verify(Path(directory)), "status": "PASS"}))
