#!/usr/bin/env python3
"""Offline Stage 2B, requiring byte-equivalent instrumentation evidence; no simulator call."""
import argparse
import json
from pathlib import Path
import runpy

ROOT=Path(__file__).resolve().parents[5]
A=runpy.run_path(str(ROOT/'contrib/satcompute/tests/support/protection/residual_deadline_audit.py'))
R=runpy.run_path(str(Path(__file__).with_name('run-residual-deadline-development.py')))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    R['audit']()
    summary=A['analyze'](R['OUT']/'instrumented-run11',R['OUT'])
    print(json.dumps(summary,indent=2))
