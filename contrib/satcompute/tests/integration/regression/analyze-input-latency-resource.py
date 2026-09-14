#!/usr/bin/env python3
"""Offline INPUT traffic/wait audit using the native pure time estimator, not a simulation."""
import argparse
import json
from pathlib import Path
import runpy

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir',type=Path,required=True)
    parser.add_argument('--verified-stage-b-dir',type=Path,required=True)
    parser.add_argument('--coinitialization-dir',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--engine-probe',type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[5]
    api = runpy.run_path(str(Path(__file__).resolve().parents[2]/'support/protection/input_latency_resource_audit.py'))
    probe = args.engine_probe.resolve() if args.engine_probe else api['find_probe'](repo)
    result = api['write_audit'](args.run_dir.resolve(),args.verified_stage_b_dir.resolve(),
        args.coinitialization_dir.resolve(),args.output_dir.resolve(),probe)
    print(json.dumps({k:result[k] for k in ('status','A0','A1','native_estimator','raw_evidence_read_only')},indent=2))
