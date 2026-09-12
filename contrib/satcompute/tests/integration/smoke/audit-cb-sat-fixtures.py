#!/usr/bin/env python3
"""Audit actual C++ CB fixtures, then reject independently corrupted evidence copies."""
import argparse
import csv
import json
from pathlib import Path
import runpy
import shutil
import sys
import tempfile

TOOLS = Path(__file__).resolve().parents[3]/"protection/policy/baseline/checkbullet/tools"
sys.path.insert(0,str(TOOLS))
AUDIT = runpy.run_path(str(TOOLS/"audit-cb-sat-run.py"))["audit"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root",type=Path,required=True)
    args = parser.parse_args()
    paths = [p for p in sorted(args.root.iterdir()) if p.is_dir()]
    if len(paths) != 21:
        raise ValueError("expected all 21 current C++ recovery fixtures")
    for directory in paths:
        AUDIT(directory)
    corruptions = [
        ("cb-sat-recovery.csv","resume_work_units","66400"),
        ("cb-sat-recovery.csv","tail_bytes","1"),
        ("cb-sat-recovery.csv","actual_total_recovery_wu","40001"),
        ("cb-sat-tasks.csv","normal_protection_cost_ns","999999"),
        ("cb-sat-transfers.csv","bytes","399"),
        ("cb-sat-storage.csv","final_used_bytes","1"),
        ("placement-selections.csv","local_node","9"),
        ("cb-sat-checkpoints.csv","record_bytes","1"),
    ]
    for name,key,value in corruptions:
        with tempfile.TemporaryDirectory(prefix="cb-evidence-corruption-") as tmp:
            directory = Path(tmp)/"case"
            shutil.copytree(args.root/"direct",directory)
            file = directory/name
            with file.open() as stream:
                reader = csv.DictReader(stream); fields=reader.fieldnames; records=list(reader)
            records[0][key] = value
            with file.open("w") as stream:
                writer = csv.DictWriter(stream,fields); writer.writeheader(); writer.writerows(records)
            try:
                AUDIT(directory)
            except ValueError:
                continue
            raise ValueError(f"corrupted {name}:{key} accepted")
    print(json.dumps(dict(cb_recovery_evidence_cases=len(paths),rejected_corruptions=len(corruptions))))


if __name__ == "__main__":
    main()
