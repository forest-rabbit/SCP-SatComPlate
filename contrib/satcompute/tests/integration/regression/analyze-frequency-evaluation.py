#!/usr/bin/env python3
"""Compatibility entry; reusable implementation lives in tests/support/protection/frequency_audit.py."""
from pathlib import Path
import runpy

_api = runpy.run_path(str(Path(__file__).resolve().parents[2] / "support/protection/frequency_audit.py"))
globals().update({key: value for key, value in _api.items() if not key.startswith("__")})

if __name__ == "__main__":
    raise SystemExit(main())
