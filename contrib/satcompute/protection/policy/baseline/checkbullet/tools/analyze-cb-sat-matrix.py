#!/usr/bin/env python3
"""Summarize actual CB runs without overwriting historical comparison evidence."""
import argparse
import csv
import json
from pathlib import Path
import runpy
from cb_tools import require, write_json


def analyze(root):
    audit = runpy.run_path(str(Path(__file__).with_name("audit-cb-sat-run.py")))["audit"]
    results, failures = [], []
    for path in sorted(root.rglob("execution.json")):
        execution = json.loads(path.read_text())
        if execution.get("protection_mode") != "checkbullet":
            continue
        directory = path.parent
        try:
            outcome = json.loads((directory/"execution-result.json").read_text())
            require(outcome["returncode"] == 0,"simulation failed")
            result = audit(directory)
            result["summary"].update(group=directory.name,stage=execution["stage"],commit=execution["commit"],
                placement=execution["placement_mode"],busy=execution["remote_busy_recovery_policy"],
                mtbf_seconds=execution["mtbf_seconds"],directory=str(directory),audit_status="PASS")
            results.append(result["summary"])
        except Exception as error:
            failures.append(dict(directory=str(directory),error=str(error)))
            write_json(directory/"cb-sat-audit.json",dict(status="FAIL",error=str(error)))
    require(results or failures,"no actual CB execution evidence found")
    write_json(root/"cb-sat-matrix-summary.json",dict(runs=results,failed_or_incomplete=failures,
        note="Eight configurations, not independent repeats. Calibration pilots are not success-rate samples."))
    if results:
        keys = list(dict.fromkeys(k for r in results for k,v in r.items() if not isinstance(v,dict)))
        with (root/"cb-sat-matrix-summary.csv").open("w") as out:
            writer = csv.DictWriter(out,keys,extrasaction="ignore"); writer.writeheader(); writer.writerows(results)
    return results,failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root",type=Path,required=True)
    args = parser.parse_args()
    runs,failures = analyze(args.root.resolve())
    print(json.dumps(dict(audited_runs=len(runs),failed_or_incomplete=len(failures))))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
