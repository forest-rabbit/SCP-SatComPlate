#!/usr/bin/env python3
"""Four public placements x two REMOTE_BUSY policies, with fixed independent calibration."""
import argparse
import json
from pathlib import Path
import runpy
from cb_tools import (ROOT, PROFILE, REGRESSION, SCENE_HELPER, GROUPS, batch, execute, clean_head, new_execution,
                      require, scene_identity, write_json)


def deterministic(reference, candidate):
    checked = []
    def normalize(value, directory):
        if isinstance(value, dict):
            return {k:normalize(v,directory) for k,v in value.items() if k not in ("wall_clock_ns","wall_clock_s")}
        if isinstance(value,list):
            return [normalize(v,directory) for v in value]
        if isinstance(value,str):
            return value.replace(str(directory),"OUTPUT")
        return value
    for source in sorted(reference.iterdir()):
        if source.suffix not in (".csv",".json") or source.name in ("execution.json","execution-result.json"):
            continue
        target = candidate/source.name
        require(target.is_file(),f"repeat missing {source.name}")
        if source.suffix == ".csv":
            require(source.read_bytes() == target.read_bytes(),f"repeat differs: {source.name}")
        else:
            require(normalize(json.loads(source.read_text()),reference) == normalize(json.loads(target.read_text()),candidate),
                    f"repeat differs: {source.name}")
        checked.append(source.name)
    require("cb-sat-recovery.csv" in checked and "cb-sat-transfers.csv" in checked,"repeat has no CB evidence")
    return dict(status="PASS",files=checked,ignored=["execution identity/output paths","wall_clock_ns","wall_clock_s"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage",choices=("smoke","formal"),default="smoke")
    parser.add_argument("--jobs",type=int,default=2)
    args = parser.parse_args()
    head = clean_head()
    require(PROFILE.is_file(),"independent MTBF calibration must be frozen first")
    profile = json.loads(PROFILE.read_text())
    require(profile["runs"] == list(range(101,111)),"not the independent production profile")
    root = new_execution(args.stage)
    if args.stage == "smoke":
        make = runpy.run_path(str(REGRESSION.parent/"smoke/run-cb-sat-smoke.py"))["arguments"]
        identity = dict(stage="smoke",fixture="existing fixed-four-profiles",mtbf_seconds=profile["mtbf_seconds"])
    else:
        make = lambda path,placement,busy: SCENE_HELPER["arguments"](path,protection_mode="checkbullet",
            placement_mode=placement,remote_busy_recovery_policy=busy)
        identity = dict(stage="formal",scene=scene_identity(),mtbf_seconds=profile["mtbf_seconds"])
    commands = {f"CB-{p}-{b}":make(root/f"CB-{p}-{b}",p,b) for p,b in GROUPS}
    batch(root,args.jobs,commands,head,identity)
    audit = runpy.run_path(str(Path(__file__).with_name("audit-cb-sat-run.py")))["audit"]
    statuses = {}
    for name in commands:
        try:
            result = audit(root/name)
            require(result["summary"]["completed"] == 4 if args.stage == "smoke" else
                    result["samples"]["tasks"] == 800,"smoke outcome or formal task count wrong")
            statuses[name] = "AUDIT_PASS"
        except Exception as error:
            statuses[name] = "AUDIT_FAIL"
            write_json(root/name/"cb-sat-audit.json",dict(status="FAIL",error=str(error)))
    write_json(root/"matrix-status.json",dict(commit=head,stage=args.stage,groups=statuses))
    require(all(v == "AUDIT_PASS" for v in statuses.values()), f"CB matrix audit failed; retained at {root}")
    if args.stage == "smoke":
        repeat = root/"repeat-CB-fa-lrl-relocate"
        execute(repeat,make(repeat,"fa-lrl","relocate"),head,identity)
        audit(repeat)
        write_json(root/"determinism.json",deterministic(root/"CB-fa-lrl-relocate",repeat))
    print(root,flush=True)


if __name__ == "__main__":
    main()
