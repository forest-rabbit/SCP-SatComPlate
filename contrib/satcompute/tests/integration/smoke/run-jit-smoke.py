#!/usr/bin/env python3
"""Small actual JIT lifecycle and online epoch regression, never a formal scene rerun."""
import argparse
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[5]


def verify(output):
    subprocess.run([str(ROOT / "ns3"), "run", "--no-build", "satcompute-jit-input-staging-test"], cwd=ROOT, check=True)
    subprocess.run([str(ROOT / "ns3"), "run", "--no-build",
                    f"satcompute-jit-runtime-test --outputDir={output}"], cwd=ROOT, check=True)
    runpy.run_path(str(Path(__file__).with_name("audit-jit-fixtures.py")))["verify"](output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--outputDir", type=Path)
    args = parser.parse_args()
    if args.outputDir:
        verify(args.outputDir.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="satcompute-jit-smoke-") as directory:
            verify(Path(directory))
