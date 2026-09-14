#!/usr/bin/env python3
"""Read-only Stage B causal trace/label/P_F audit; never choose a threshold."""
import argparse
import json
from pathlib import Path
import runpy

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    api = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'support/protection/input_start_trace_audit.py'))
    print(json.dumps(api['write_audit'](args.run_dir.resolve(), args.output_dir.resolve()), indent=2))
