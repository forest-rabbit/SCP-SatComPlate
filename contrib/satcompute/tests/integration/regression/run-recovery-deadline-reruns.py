#!/usr/bin/env python3
"""User-scoped run11 only: FULL/noU/Rational-U on one corrected execution commit."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import runpy
import re
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
FINAL = runpy.run_path(str(HERE / 'run-final-scenario.py'))
ROOT = FINAL['ROOT']
AUDIT = ROOT / 'output/audits/recovery-direct-deadline-impact-complete.json'
SOURCES = {
    'output/n5c-v4/formal/R7-n5c': 'full',
    'output/n5c-v4/formal/R7-n5c-noU': 'noU',
    'output/n5c-rational-u/run-11/rational-U': 'rational-U',
}


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def clean():
    if git('status', '--porcelain'):
        raise ValueError('formal execution requires a clean worktree')
    return git('rev-parse', 'HEAD')


def frozen_scope():
    paths = ['contrib/satcompute/input',
             'contrib/satcompute/fault', 'contrib/satcompute/routing', 'contrib/satcompute/traffic',
             'contrib/satcompute/protection/policy/compfrr',
             'contrib/satcompute/protection/runtime/frequency-protection-controller.cc',
             'contrib/satcompute/protection/runtime/frequency-n5c-adapter.cc',
             'contrib/satcompute/protection/runtime/n5c-placement-tracker.cc',
             'contrib/satcompute/protection/policy/baseline/checkbullet']
    if git('diff', '--name-only', 'c1a8704cd', 'HEAD', '--', *paths):
        raise ValueError('frozen model/input/runtime scope changed')
    for path in ('contrib/satcompute/para.cc', 'contrib/satcompute/para.h'):
        # Parameter documentation changes only; no defaults/types added or changed.
        old = re.sub(r'//[^\n]*', '', git('show', f'c1a8704cd:{path}'))
        new = re.sub(r'//[^\n]*', '', git('show', f'HEAD:{path}'))
        if old != new:
            raise ValueError('parameter values/types changed')


def normalized(command):
    return [part for part in shlex.split(command) if not
            part.startswith(('--outputDir=', '--faultTrace='))]


def prepare(root):
    commit = clean()
    frozen_scope()
    audit = json.loads(AUDIT.read_text())
    if root.exists():
        raise ValueError('refuse to overwrite rerun evidence')
    entries = []
    for item in audit['runs']:
        if item['directory'] not in SOURCES:
            continue
        old = ROOT / item['directory']
        meta = json.loads((old / 'execution.json').read_text())
        name = SOURCES[item['directory']]
        output = root / 'formal' / name
        args = FINAL['arguments'](output, protection_mode=meta['protection_mode'],
            placement_mode=meta['placement_mode'], input_staging_policy=meta['input_staging_policy'],
            remote_busy_recovery_policy=meta['remote_busy_recovery_policy'],
            n5c_variant=meta.get('n5c_variant', 'full'), random_run=meta['run'])
        if normalized(meta['command'][-1]) != normalized(shlex.join(args)):
            raise ValueError(f'frozen execution arguments differ: {old}')
        entries.append(dict(source=item['directory'], destination=str(output.relative_to(ROOT)),
            source_commit=meta['commit'], run=meta['run'],
            deadline_tasks=item['affected_tasks'], input_path_tasks=item['input_path_fallback_tasks'],
            metadata=meta))
    if len(entries) != 3 or any(e['run'] != 11 for e in entries):
        raise ValueError('only the three run11 U variants are authorized')
    root.mkdir(parents=True)
    (root / 'execution-plan.json').write_text(json.dumps(dict(commit=commit, audit=str(AUDIT),
        jobs_limit=3, entries=entries, deferred_historical_runs=[r for r in audit['runs']
            if r['directory'] not in SOURCES], scope='User narrowed to run11 FULL/noU/Rational-U only',
        frozen_base='c1a8704cd', cb_sat_unchanged=True), indent=2) + '\n')
    print('PREPARED', len(entries), 'run11 U variants; no other simulations', flush=True)


def execute(root, jobs):
    if not 1 <= jobs <= 3:
        raise ValueError('jobs must be between 1 and 3')
    plan = json.loads((root / 'execution-plan.json').read_text())
    if {e['source'] for e in plan['entries']} != set(SOURCES) or len(plan['entries']) != 3:
        raise ValueError('unapproved execution matrix')
    if clean() != plan['commit']:
        raise ValueError('execution commit changed')
    frozen_scope()
    def one(entry):
        if clean() != plan['commit']:
            raise ValueError('source changed during execution')
        meta = entry['metadata']
        output = ROOT / entry['destination']
        if output.exists():
            raise ValueError('existing output; never overwrite or reuse incomplete execution')
        print('START', entry['source'], flush=True)
        command = [sys.executable, str(HERE / 'run-final-scenario.py'), '--output-dir', str(output),
            '--protection-mode', meta['protection_mode'], '--placement-mode', meta['placement_mode'],
            '--input-staging-policy', meta['input_staging_policy'], '--remote-busy-recovery-policy',
            meta['remote_busy_recovery_policy'], '--n5c-variant', meta.get('n5c_variant', 'full'),
            '--random-run', str(meta['run'])]
        subprocess.run(command, cwd=ROOT, check=True)
        new = json.loads((output / 'execution.json').read_text())
        if clean() != plan['commit'] or new['commit'] != plan['commit'] or new['worktree_dirty']:
            raise ValueError('execution identity changed')
        if normalized(new['command'][-1]) != normalized(meta['command'][-1]):
            raise ValueError('execution arguments changed')
        print('FINISHED', entry['source'], flush=True)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(one, plan['entries']))
    print('THREE_RUN11_VARIANTS_COMPLETE', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT / 'output/recovery-deadline-reruns')
    parser.add_argument('--phase', choices=('prepare', 'run', 'all'), required=True)
    parser.add_argument('--jobs', type=int, default=3)
    args = parser.parse_args()
    if args.phase in ('prepare', 'all'):
        prepare(args.root.resolve())
    if args.phase in ('run', 'all'):
        execute(args.root.resolve(), args.jobs)


if __name__ == '__main__':
    main()
