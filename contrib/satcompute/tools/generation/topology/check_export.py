#!/usr/bin/env python3
"""Check one generated SatCompute topology trace and its authoritative hashes."""

import argparse
from pathlib import Path
import sys

from common.manifest import TraceValidationError, validate_trace


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace-dir", required=True, type=Path)
    arguments = parser.parse_args()
    result = validate_trace(arguments.trace_dir)
    print(
        "PASS: validated topology trace "
        f"{result['run_name']} ({result['satellite_count']} satellites, "
        f"{result['slice_count']} slices)"
    )


if __name__ == "__main__":
    try:
        main()
    except (OSError, TraceValidationError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
