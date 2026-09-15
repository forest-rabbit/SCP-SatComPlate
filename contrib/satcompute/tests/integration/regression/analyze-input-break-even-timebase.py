#!/usr/bin/env python3
"""Compare two fixed offline INPUT screens against existing Stage B evidence only."""
import argparse
import json
from pathlib import Path
import runpy

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('run-dir', 'verified-stage-b-dir', 'coinitialization-dir',
                 'latency-resource-dir', 'output-dir'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--engine-probe', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[5]
    api = runpy.run_path(str(Path(__file__).resolve().parents[2]/
                            'support/protection/input_break_even_timebase_audit.py'))
    probe = args.engine_probe.resolve() if args.engine_probe else api['NET']['find_probe'](repo)
    result = api['write_audit'](args.run_dir.resolve(), args.verified_stage_b_dir.resolve(),
        args.coinitialization_dir.resolve(), args.latency_resource_dir.resolve(), args.output_dir.resolve(), probe)
    print(json.dumps({k:result[k] for k in ('status', 'verdict', 'network_candidates',
        'numeric_or_coverage_blockers', 'views', 'raw_evidence_read_only')}, indent=2))
    if result['numeric_or_coverage_blockers']:
        raise SystemExit(2)
