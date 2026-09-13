#!/usr/bin/env python3
"""N5C: two strict FA-FFP regression gates, two V4 runs, then three Deferred ablations."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import runpy
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
MATRIX = runpy.run_path(str(HERE/"run-pre-n5c-placement-matrix.py"))
BASE = runpy.run_path(str(HERE/"analyze-baseline-evaluation.py"))
GROUPS = {
    "R5-fa-ffp": ("eager","fa-ffp","full"),
    "R7-fa-ffp": ("deferred","fa-ffp","full"),
    "R5-n5c": ("eager","n5c","full"),
    "R7-n5c": ("deferred","n5c","full"),
    "R7-n5c-noR": ("deferred","n5c","noR"),
    "R7-n5c-noU": ("deferred","n5c","noU"),
    "R7-n5c-noM": ("deferred","n5c","noM"),
}
PHASES = {"gate":("R5-fa-ffp","R7-fa-ffp"), "main":("R5-n5c","R7-n5c"),
          "ablation":("R7-n5c-noR","R7-n5c-noU","R7-n5c-noM")}


def command(output, group):
    staging,placement,variant = GROUPS[group]
    return [sys.executable,str(HERE/"run-final-scenario.py"),"--output-dir",str(output/group),
            "--protection-mode","compfrr","--placement-mode",placement,
            "--input-staging-policy",staging,"--remote-busy-recovery-policy","relocate",
            "--n5c-variant",variant]


def verify_gate(reference, candidate):
    report = BASE["strict_equivalence"](reference,candidate)
    MATRIX["require"](report["passed"], f"FA-FFP behavior differs: {report}")
    # Compare ALL JSONs too, allowing only identity, paths and wall clock.
    for source in reference.glob("*.json"):
        if source.name in ("execution.json","execution-result.json"):
            continue
        a,b = (json.loads(p.read_text()) for p in (source,candidate/source.name))
        MATRIX["require"](MATRIX["normalize"](a,reference) == MATRIX["normalize"](b,candidate),source.name)
    return report


def verify_main_audit(root, head):
    """An unrelated/gate-only comparison is not an audited main experiment."""
    audit=json.loads((root/"comparison.json").read_text())
    for group in PHASES["main"]:
        current=json.loads((root/group/"execution.json").read_text())
        record=audit.get(group,{})
        MATRIX["require"](current["commit"] == head and not current["worktree_dirty"] and
                          record.get("execution") == current and record.get("n5c") is not None,
                          "audit both same-code main runs before ablations")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root",type=Path,required=True)
    parser.add_argument("--phase",choices=PHASES,required=True)
    parser.add_argument("--jobs",type=int,default=2)
    parser.add_argument("--reference",type=Path,default=ROOT/"output/pre-n5c-placement-final")
    args=parser.parse_args()
    head=MATRIX["identity"]()
    require=MATRIX["require"]
    require(1 <= args.jobs <= 2,"use one or two local simulations")
    args.root=args.root.resolve()
    args.root.mkdir(parents=True,exist_ok=True)
    if args.phase != "gate":
        gate=json.loads((args.root/"fa-ffp-equivalence.json").read_text())
        require(gate["commit"] == head and all(r["passed"] for r in gate["runs"].values()),"missing same-code FA-FFP gate")
    if args.phase == "ablation":
        for group in PHASES["main"]:
            e=json.loads((args.root/group/"execution.json").read_text())
            require(e["commit"] == head and not e["worktree_dirty"],"ablation changed code")
            require(json.loads((args.root/group/"execution-result.json").read_text())["returncode"] == 0,"main run incomplete")
        verify_main_audit(args.root,head)
    def run(group):
        require(MATRIX["identity"]() == head,"source changed before execution")
        require(not (args.root/group).exists(),"refuse to overwrite raw evidence")
        print("START",group,flush=True)
        result=subprocess.run(command(args.root,group),cwd=ROOT)
        require(result.returncode == 0,f"simulation failed: {group}")
        require(MATRIX["identity"]() == head,"source changed during execution")
        print("FINISHED",group,flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        list(workers.map(run,PHASES[args.phase]))
    if args.phase == "gate":
        report={"commit":head,"runs":{g:verify_gate(args.reference/g,args.root/g) for g in PHASES["gate"]}}
        (args.root/"fa-ffp-equivalence.json").write_text(json.dumps(report,indent=2)+"\n")
        print("FA-FFP strict equivalence PASS",flush=True)


if __name__ == "__main__":
    main()
