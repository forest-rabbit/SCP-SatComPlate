#!/usr/bin/env python3
"""Small corrected-chain semantic gate; never launch the formal 1300s workload."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[5]
SMOKE = Path(__file__).resolve().parents[1] / 'smoke'
CPP = ('frequency', 'recovery', 'recompute', 'one-plus-one', 'cb-sat-recovery')


def normalized_json(value, directory):
    """Only output location and wall-clock duration are non-semantic."""
    if isinstance(value, dict):
        return {k: normalized_json(v, directory) for k, v in value.items()
                if k not in ('wall_clock_ns', 'wall_clock_s')}
    if isinstance(value, list):
        return [normalized_json(v, directory) for v in value]
    if isinstance(value, str):
        return value.replace(str(directory), 'OUTPUT')
    return value


def compare(reference, candidate):
    def files(directory):
        return {p.relative_to(directory) for p in directory.rglob('*')
                if p.suffix in ('.csv', '.json')}
    expected, actual = files(reference), files(candidate)
    if not expected or expected != actual:
        raise AssertionError(f'output set differs: missing={expected-actual}, extra={actual-expected}')
    for path in sorted(expected):
        left, right = reference / path, candidate / path
        if path.suffix == '.csv':
            # Compare all schema, rows, ordering and values, not only selected metrics.
            for source in (left, right):
                with source.open(newline='') as stream:
                    rows = list(csv.reader(stream))
                if rows and any(len(r) != len(rows[0]) for r in rows):
                    raise AssertionError(f'malformed CSV: {source}')
            equal = left.read_bytes() == right.read_bytes()
        else:
            equal = normalized_json(json.loads(left.read_text()), reference) == normalized_json(
                json.loads(right.read_text()), candidate)
        if not equal:
            raise AssertionError(f'SEMANTIC_DIFFERENCE: {path}; stop and audit, do not refresh golden')
    return {'status': 'PASS', 'files': len(expected), 'csv': sum(p.suffix == '.csv' for p in expected)}


def execute(output, arguments):
    output.mkdir(parents=True, exist_ok=False)
    result = subprocess.run([sys.executable, str(ROOT / 'ns3'), 'run', '--no-build',
                             shlex.join(arguments)], cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'{output.name}: {result.stdout}\n{result.stderr}')
    print(f'PASS {output.name}', flush=True)


def collect(root, jobs):
    if root.exists():
        raise ValueError(f'output already exists; preserve evidence: {root}')
    root.mkdir(parents=True)
    commands = []
    targets = {'frequency': 'frequency-runtime', 'recovery': 'recovery-runtime',
               'recompute': 'recompute-baseline', 'one-plus-one': 'one-plus-one-baseline',
               'cb-sat-recovery': 'cb-sat-recovery'}
    for name in CPP:
        output = root / name
        commands.append((output, [f'satcompute-{targets[name]}-test', f'--outputDir={output}']))
    cb = runpy.run_path(str(SMOKE / 'run-cb-sat-smoke.py'))['arguments']
    for busy in ('recompute', 'relocate'):
        output = root / f'cb-{busy}'
        commands.append((output, cb(output, 'fa-ffp', busy)))
    fixture = ROOT / 'contrib/satcompute/tests/fixtures'
    for name, placement, staging, variant in (
        ('f-eager', 'fa-ffp', 'eager', 'full'),
        ('f-deferred', 'fa-ffp', 'deferred', 'full'),
        ('p-cumulative', 'n5c', 'deferred', 'full'),
        ('p-idle-aware', 'n5c', 'deferred', 'rational-U')):
        output = root / name
        commands.append((output, ['satcompute', '--simulationDuration=15', '--randomSeed=1',
            '--randomRun=11', f'--constellationConfig={fixture}/constellation/connected-16.csv',
            f'--taskTrace={fixture}/protection/fixed-four-profiles.json',
            f'--computeProfile={fixture}/protection/fixed-compute.json', '--faultMode=generate',
            '--faultEnableF1=1', '--faultEnableF2=1', '--faultEnableF3=0',
            '--taskCompletionPolicy=report', '--compfrr-shadow=0', '--faultProbabilityAudit=1',
            '--protectionMode=compfrr', f'--placementMode={placement}', f'--n5cVariant={variant}',
            f'--inputStagingPolicy={staging}', '--remoteBusyRecoveryPolicy=relocate',
            '--routingMode=global-capacity-aware-hrw', '--islBandwidthBps=10000000000',
            '--delayMode=fixed', '--fixedDelay=0.001', f'--outputDir={output}']))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(lambda item: execute(*item), commands))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--compare-only', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8:
        parser.error('--jobs must be in [1,8]')
    if args.compare_only and not args.reference:
        parser.error('--compare-only requires --reference')
    output = args.output_root.resolve()
    if not args.compare_only:
        collect(output, args.jobs)
    result = compare(args.reference.resolve(), output) if args.reference else {
        'status': 'BASELINE_CAPTURED', 'root': str(output)}
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
