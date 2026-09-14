#!/usr/bin/env python3
"""Offline audit of causal INPUT-benefit lower bounds; no simulation or production rule."""
import argparse
import json
from pathlib import Path
import runpy

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('run-dir', 'verified-stage-b-dir', 'criticalpath-dir', 'output-dir'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--engine-probe', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[5]
    api = runpy.run_path(str(Path(__file__).resolve().parents[2]/'support/protection/input_causal_bound_audit.py'))
    probe = args.engine_probe.resolve() if args.engine_probe else api['NET']['find_probe'](repo)
    result = api['write_audit'](args.run_dir.resolve(), args.verified_stage_b_dir.resolve(),
                              args.criticalpath_dir.resolve(), args.output_dir.resolve(), probe)
    print(json.dumps({k: result[k] for k in ('status', 'causal_A_upper_bound', 'simulation_started',
                                           'v4_features_identical', 'raw_evidence_read_only')}, indent=2))
