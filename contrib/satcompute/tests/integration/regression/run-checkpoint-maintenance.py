#!/usr/bin/env python3
"""All affected U runs only, on one clean maintenance revision; preserve old outputs."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
FINAL = runpy.run_path(str(HERE / 'run-final-scenario.py'))
ROOT = FINAL['ROOT']
GROUPS = ('full', 'noU', 'rational-U')
BASE = '08236b8af'


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def identity():
    if git('status', '--porcelain'):
        raise ValueError('formal execution requires a clean worktree')
    frozen = ['contrib/satcompute/' + path for path in (
        'input', 'para.cc', 'para.h', 'fault', 'routing', 'traffic', 'task',
        'protection/runtime/recovery-controller.cc', 'protection/runtime/recovery-controller.h',
        'protection/runtime/checkpoint-recovery-estimate.h',
        'protection/runtime/frequency-n5c-adapter.cc',
        'protection/policy/compfrr/frequency/compfrr-frequency-policy.cc',
        'protection/policy/compfrr/placement/n5c-placement-policy.cc',
        'protection/policy/baseline/checkbullet', 'protection/baseline/checkbullet')]
    if git('diff', '--name-only', BASE, 'HEAD', '--', *frozen):
        raise ValueError('frozen algorithms/recovery/input/fault/routing changed')
    return git('rev-parse', 'HEAD')


def normalized(command):
    return [x for x in shlex.split(command) if not x.startswith(('--outputDir=', '--faultTrace='))]


def entries(root):
    previous = json.loads((ROOT / 'output/recovery-u-revalidation/execution-plan.json').read_text())
    result = []
    for old in previous['entries']:
        run, group = old['run'], old['group']
        source = ROOT / old['directory']
        meta = json.loads((source / 'execution.json').read_text())
        dest = root / f'run-{run}' / group
        args = FINAL['arguments'](dest, protection_mode='compfrr', placement_mode='n5c',
            input_staging_policy='deferred', remote_busy_recovery_policy='relocate',
            n5c_variant=group, random_run=run)
        if normalized(meta['command'][-1]) != normalized(shlex.join(args)):
            raise ValueError('frozen invocation differs')
        result.append(dict(run=run, group=group, source=str(source.relative_to(ROOT)),
                           source_commit=meta['commit'], directory=str(dest.relative_to(ROOT)),
                           arguments=args))
    if {(r['run'], r['group']) for r in result} != {(r, g) for r in range(11, 16) for g in GROUPS} or len(result) != 15:
        raise ValueError('U-only five-run matrix differs')
    return result


def prepare(root):
    plan = dict(commit=identity(), base=BASE, entries=entries(root), jobs_limit=8,
                new_executions=15, reused_executions=0, scope='affected U only; no other baselines')
    root.mkdir(parents=True, exist_ok=False)
    (root / 'execution-plan.json').write_text(json.dumps(plan, indent=2)+'\n')


def execute(root, jobs):
    plan = json.loads((root / 'execution-plan.json').read_text())
    if not 1 <= jobs <= 8 or identity() != plan['commit'] or plan['entries'] != entries(root):
        raise ValueError('execution identity/matrix changed')

    def one(row):
        if identity() != plan['commit']:
            raise ValueError('worktree changed during formal execution')
        dest = ROOT / row['directory']
        print('START', row['run'], row['group'], flush=True)
        subprocess.run([sys.executable, str(HERE/'run-final-scenario.py'), '--output-dir', str(dest),
            '--protection-mode', 'compfrr', '--placement-mode', 'n5c', '--input-staging-policy', 'deferred',
            '--remote-busy-recovery-policy', 'relocate', '--n5c-variant', row['group'],
            '--random-run', str(row['run'])], cwd=ROOT, check=True)
        meta = json.loads((dest/'execution.json').read_text())
        if meta['commit'] != plan['commit'] or meta['worktree_dirty'] or identity() != plan['commit']:
            raise ValueError('formal identity changed')
        if normalized(meta['command'][-1]) != normalized(shlex.join(row['arguments'])):
            raise ValueError('formal invocation changed')
        print('COMPLETE', row['run'], row['group'], flush=True)

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(one, plan['entries']))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT/'output/checkpoint-maintenance-fixed')
    parser.add_argument('--phase', choices=('prepare', 'run', 'all'), required=True)
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    if args.phase in ('prepare', 'all'): prepare(args.root.resolve())
    if args.phase in ('run', 'all'): execute(args.root.resolve(), args.jobs)
