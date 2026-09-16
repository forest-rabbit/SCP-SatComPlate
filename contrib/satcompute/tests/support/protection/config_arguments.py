"""Compatibility import for historical experiment replay and identity tests only.

Current scenario execution emits canonical controls directly and never calls this
adapter. Retained callers are archived comparison runners and equivalence gates.
"""
from pathlib import Path
import runpy

_api = runpy.run_path(str(Path(__file__).with_name('historical_config_arguments.py')))
globals().update({key: value for key, value in _api.items() if not key.startswith('__')})
