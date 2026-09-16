#!/usr/bin/env python3
"""Mapping-only Stage A: small passive equivalence, then canonical OFF+generate audit."""
import argparse
import csv
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[5]
SUPPORT = ROOT / 'contrib/satcompute/tests/support/protection'
SCENARIO = runpy.run_path(str(SUPPORT / 'scenario.py'))
EQUIV = runpy.run_path(str(Path(__file__).with_name('run-protection-equivalence.py')))


def execute(output, passive, small):
    output.mkdir(parents=True, exist_ok=False)
    arguments = SCENARIO['canonical_experiment_arguments'](SCENARIO['arguments'](output))
    arguments[0] = 'satcompute-protection-config-driver'
    if small:
        fixture = ROOT / 'contrib/satcompute/tests/fixtures'
        overrides = {'simulationDuration': '15',
            'constellationConfig': str(fixture / 'constellation/connected-16.csv'),
            'computeProfile': str(fixture / 'protection/fixed-compute.json'),
            'taskTrace': str(fixture / 'protection/fixed-four-profiles.json'), 'faultEnableF3': '0'}
        arguments = [a for a in arguments if a.lstrip('-').partition('=')[0] not in overrides
                     and not a.startswith(('--faultF3Mode=', '--faultF3Node=', '--faultF3Time='))]
        arguments += [f'--{k}={v}' for k, v in overrides.items()]
    arguments.append(f'--testMultitreeMapping={int(passive)}')
    (output / 'command.txt').write_text(shlex.join(arguments) + '\n')
    with (output / 'execution.log').open('w') as log:
        process = subprocess.run([sys.executable, str(ROOT / 'ns3'), 'run', '--no-build',
                                  shlex.join(arguments)], cwd=ROOT, stdout=log, stderr=log)
    if process.returncode:
        raise RuntimeError(f'preflight failed: {output}/execution.log')


def compare_passive(off, on):
    extras = {'multitree-decisions.csv', 'multitree-mapping-summary.json'}
    files = lambda root: {p.relative_to(root) for p in root.rglob('*') if p.suffix in ('.json', '.csv')}
    expected, actual = files(off), files(on)
    assert expected == actual - {Path(p) for p in extras}
    for path in expected:
        left, right = off / path, on / path
        if path.suffix == '.csv':
            assert left.read_bytes() == right.read_bytes(), path
        else:
            assert EQUIV['normalized_json'](json.loads(left.read_text()), off) == \
                   EQUIV['normalized_json'](json.loads(right.read_text()), on), path
    return {'status': 'PASS', 'identical_files': len(expected), 'audit_only_files': sorted(extras)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--main', action='store_true')
    args = parser.parse_args()
    root = args.output_root.resolve()
    if args.main:
        execute(root, True, False)
        audit = runpy.run_path(str(Path(__file__).with_name('audit-multitree-mapping.py')))['audit'](root)
        (root / 'mapping-audit.json').write_text(json.dumps(audit, indent=2, allow_nan=False) + '\n')
        print(json.dumps(audit, indent=2), flush=True)
    else:
        execute(root / 'off', False, True)
        execute(root / 'on', True, True)
        print(json.dumps(compare_passive(root / 'off', root / 'on')), flush=True)
