#!/usr/bin/env python3
"""Pure offline S-vs-historical-N audit over the frozen Stage 2B candidate snapshots."""
import json
from pathlib import Path
import runpy

ROOT=Path(__file__).resolve().parents[5]
A=runpy.run_path(str(ROOT/'contrib/satcompute/tests/support/protection/selective_sn_residual_audit.py'))
OUTPUT=ROOT/'output/compfrr/1g-residual-deadline-audit'

if __name__=='__main__':
    summary=A['analyze'](
        OUTPUT/'instrumented-run11/residual-deadline-candidates.json',OUTPUT,
        ROOT/'contrib/satcompute/tests/fixtures/protection/selective-input-ser-anchor.json',
        ROOT/'output/compfrr-input-admission/20260915-development-run11/S/input-admission-decisions.csv',
        ROOT/'output/compfrr-input-admission/20260915-development-run11/N/input-admission-decisions.csv')
    print(json.dumps(summary['answers'],indent=2))
