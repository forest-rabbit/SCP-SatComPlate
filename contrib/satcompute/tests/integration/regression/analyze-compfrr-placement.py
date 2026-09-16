#!/usr/bin/env python3
"""Canonical entry for the shared CompFRR-P causal placement audit."""
from pathlib import Path
import runpy

_api = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'support/protection/placement_audit.py'))
globals().update({key: value for key, value in _api.items() if not key.startswith('__')})

if __name__ == '__main__':
    raise SystemExit(main())
