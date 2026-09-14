#!/usr/bin/env python3
"""Historical V7 offline entry; never starts a simulator or enables production JIT."""
from pathlib import Path
import runpy

_api = runpy.run_path(str(Path(__file__).resolve().parents[2] / "support/protection/jit_offline_audit.py"))
globals().update({key: value for key, value in _api.items() if not key.startswith("__")})

if __name__ == "__main__":
    raise SystemExit(main())
