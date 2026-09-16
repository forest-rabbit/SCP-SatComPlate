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
CB_PROFILE_OLD = 'contrib/satcompute/protection/policy/baseline/checkbullet/calibration/frozen-mtbf-profile.json'
CB_PROFILE_NEW = 'contrib/satcompute/protection/baseline/checkbullet/calibration/frozen-mtbf-profile.json'
CB_PROFILE_REFERENCE = 'fcdfe2db8ed96ee403d9fa99e717e63855208d05'


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


def compare(reference, candidate, *, allow_cb_profile_relocation=False, allow_retired_input_snapshot=False,
            allow_placement_rename=False, allow_retired_recent_fixture=False):
    legacy = runpy.run_path(str(ROOT / 'contrib/satcompute/tests/support/protection/historical_placement.py'))
    def key(path):
        return legacy['canonical_gate_path'](path) if allow_placement_rename else path
    def files(directory):
        result = {}
        for p in directory.rglob('*'):
            if p.suffix not in ('.csv', '.json'): continue
            relative = p.relative_to(directory)
            if key(relative) in result: raise AssertionError('duplicate normalized evidence path')
            result[key(relative)] = relative
        return result
    left_files, right_files = files(reference), files(candidate)
    expected, actual = set(left_files), set(right_files)
    retired_recent = {p for p in expected if p.parts[:2] ==
                      ('frequency', 'online-compfrr-placement-recent-U')}
    if allow_retired_recent_fixture:
        if not retired_recent or retired_recent & actual:
            raise AssertionError('reference must have the retired fixture and candidate must omit it entirely')
        expected -= retired_recent
    audit_files = {p for p in expected - actual if p.name == 'input-start-snapshots.json'}
    if allow_retired_input_snapshot:
        if not audit_files:
            raise AssertionError('reference has no retired development snapshot')
        expected -= audit_files
    if not expected or expected != actual:
        raise AssertionError(f'output set differs: missing={expected-actual}, extra={actual-expected}')
    relocations = []
    for path in sorted(expected):
        left, right = reference / left_files[path], candidate / right_files[path]
        if path.suffix == '.csv':
            # Compare all schema, rows, ordering and values, not only selected metrics.
            for source in (left, right):
                with source.open(newline='') as stream:
                    rows = list(csv.reader(stream))
                if rows and any(len(r) != len(rows[0]) for r in rows):
                    raise AssertionError(f'malformed CSV: {source}')
            if allow_placement_rename:
                with left.open(newline='') as stream:
                    before = [[legacy['canonical_gate_scalar'](v) for v in row] for row in csv.reader(stream)]
                with right.open(newline='') as stream:
                    after = [[legacy['canonical_gate_scalar'](v) for v in row] for row in csv.reader(stream)]
                equal = before == after
            else:
                equal = left.read_bytes() == right.read_bytes()
        else:
            before = normalized_json(json.loads(left.read_text()), reference)
            after = normalized_json(json.loads(right.read_text()), candidate)
            equal = before == after
            if (not equal and allow_cb_profile_relocation and path in {
                    Path('cb-recompute/cb-sat-parameters.json'),
                    Path('cb-relocate/cb-sat-parameters.json')}):
                # Authorized owner move only: never ignore arbitrary paths or parameters.
                equal = (before.get('profile_path') == str(ROOT / CB_PROFILE_OLD) and
                         after.get('profile_path') == str(ROOT / CB_PROFILE_NEW) and
                         dict(before, profile_path=after['profile_path']) == after)
                if equal:
                    relocations.append(str(path))
        if not equal:
            raise AssertionError(f'SEMANTIC_DIFFERENCE: {path}; stop and audit, do not refresh golden')
    result = {'status': 'PASS', 'files': len(expected), 'csv': sum(p.suffix == '.csv' for p in expected)}
    if allow_placement_rename:
        result['placement_rename_only'] = True
    if allow_retired_recent_fixture:
        result['removed_recent_fixture_files'] = len(retired_recent)
    if allow_retired_input_snapshot:
        result['removed_development_snapshot_files'] = len(audit_files)
    if allow_cb_profile_relocation:
        result['authorized_cb_profile_path_relocations'] = relocations
    return result


def execute(output, arguments, *, translate=True):
    output.mkdir(parents=True, exist_ok=False)
    if translate:
        adapter = runpy.run_path(str(ROOT / 'contrib/satcompute/tests/support/protection/config_arguments.py'))
        arguments = adapter['execution_arguments'](arguments)
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
            f'--inputPolicy={staging}', '--remoteBusyRecoveryPolicy=relocate',
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
    parser.add_argument('--allow-placement-rename', action='store_true',
                        help='Only exact canonical placement filename/identity/reason substitutions')
    parser.add_argument('--allow-retired-recent-fixture', action='store_true',
                        help='Allow only removal of frequency/online-compfrr-placement-recent-U; compare every other output')
    parser.add_argument('--allow-retired-input-snapshot', action='store_true',
                        help='Allow only removal of the retired development JSON; compare all production output')
    parser.add_argument('--allow-cb-profile-relocation', action='store_true',
                        help='Audit only the exact N5R CB profile owner move; verify frozen profile bytes')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8:
        parser.error('--jobs must be in [1,8]')
    if args.compare_only and not args.reference:
        parser.error('--compare-only requires --reference')
    output = args.output_root.resolve()
    if args.allow_cb_profile_relocation:
        old_profile = subprocess.check_output(['git', 'show', f'{CB_PROFILE_REFERENCE}:{CB_PROFILE_OLD}'], cwd=ROOT)
        if old_profile != (ROOT / CB_PROFILE_NEW).read_bytes():
            raise AssertionError('frozen CB profile changed; owner relocation exception rejected')
    if not args.compare_only:
        collect(output, args.jobs)
    result = compare(args.reference.resolve(), output,
                     allow_cb_profile_relocation=args.allow_cb_profile_relocation,
                     allow_retired_input_snapshot=args.allow_retired_input_snapshot,
                     allow_placement_rename=args.allow_placement_rename,
                     allow_retired_recent_fixture=args.allow_retired_recent_fixture) if args.reference else {
        'status': 'BASELINE_CAPTURED', 'root': str(output)}
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
