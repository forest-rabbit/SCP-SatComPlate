#!/usr/bin/env python3
"""Exactly eight affected U runs; reuse seven verified results without overwriting."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
PREV = runpy.run_path(str(HERE / 'run-recovery-deadline-reruns.py'))
IMPACT = runpy.run_path(str(HERE / 'audit-direct-recovery-deadline.py'))
ROOT, FINAL = PREV['ROOT'], PREV['FINAL']
BASE = 'bc721ed428e05ff5fdfe6d4d1ae9bc4b8022af52'
GROUPS, RUNS = ('full', 'noU', 'rational-U'), (11, 12, 13, 14, 15)
NEW = {(12, 'full'), (14, 'full'), (12, 'noU'), (14, 'noU'), (15, 'noU'),
       (12, 'rational-U'), (14, 'rational-U'), (15, 'rational-U')}


def frozen_scope():
    PREV['frozen_scope']()
    changed = PREV['git']('diff', '--name-only', BASE, 'HEAD').splitlines()
    outside = [p for p in changed if not p.endswith('.md') and
               not p.startswith('contrib/satcompute/tests/')]
    if outside:
        raise ValueError(f'corrected production code changed: {outside}')
    return dict(production_base=BASE, changed_only_docs_tests=changed)


def source(run, group):
    if run == 11:
        return ROOT / 'output/recovery-deadline-reruns/formal' / group
    if group == 'rational-U':
        return ROOT / f'output/n5c-rational-multirun/run-{run}' / group
    return ROOT / f'output/n5c-u-audit/run-{run}' / group


def arguments(output, run, group):
    return FINAL['arguments'](output, protection_mode='compfrr', placement_mode='n5c',
        input_staging_policy='deferred', remote_busy_recovery_policy='relocate',
        n5c_variant=group, random_run=run)


def verify(directory, run, group, commit):
    meta = json.loads((directory / 'execution.json').read_text())
    result = json.loads((directory / 'execution-result.json').read_text())
    if meta['commit'] != commit or meta['worktree_dirty'] or result['returncode'] != 0:
        raise ValueError(f'incomplete/dirty/wrong execution: {directory}')
    if (meta['run'], meta['n5c_variant'], meta['seed'], meta['simulation_duration_s']) != (run, group, 1, 1300):
        raise ValueError('incorrect execution identity')
    if PREV['normalized'](meta['command'][-1]) != PREV['normalized'](shlex.join(arguments(directory, run, group))):
        raise ValueError('frozen execution arguments differ')
    return meta


def inventory(root):
    entries = []
    for run in RUNS:
        for group in GROUPS:
            old = source(run, group)
            meta = json.loads((old / 'execution.json').read_text())
            verify(old, run, group, BASE if run == 11 else meta['commit'])
            impact = None if run == 11 else IMPACT['audit']([old])[0]['runs'][0]
            new = (run, group) in NEW
            if impact is not None and impact['rerun_required'] != new:
                raise ValueError('historical impact differs from the eight authorized runs')
            entries.append(dict(run=run, group=group, new=new, source=str(old.relative_to(ROOT)),
                directory=str((root / f'run-{run}' / group if new else old).relative_to(ROOT)),
                source_commit=meta['commit'], corrected=new or run == 11, impact=impact))
    return entries


def validate_plan(plan, root):
    if plan['entries'] != inventory(root) or len(plan['entries']) != 15:
        raise ValueError('unapproved matrix or changed historical reference')
    actual = {(e['run'], e['group']) for e in plan['entries'] if e['new']}
    if actual != NEW or plan['new_executions'] != 8 or plan['reused_executions'] != 7:
        raise ValueError('only eight new / seven reused U executions authorized')


def prepare(root):
    commit = PREV['clean']()
    scope, entries = frozen_scope(), inventory(root)
    if root.exists():
        raise ValueError('refuse to overwrite evidence')
    root.mkdir(parents=True)
    plan = dict(commit=commit, source_scope=scope, entries=entries, new_executions=8, reused_executions=7,
        jobs_limit=8, leave_one_out=dict(unit='one (run,task) symmetrically, no resimulation',
        selectors=['largest_positive', 'largest_negative'], sign='reference minus candidate',
        metrics=['paired_catch_seconds', 'total_eq_waste'], scopes=['per_run', 'pooled'],
        tie_break='ascending run and numeric task ID'), ci_merge_promotion=False)
    (root / 'execution-plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    print('PREPARED eight new / seven reused U runs', commit, flush=True)


def execute(root, jobs):
    if not 1 <= jobs <= 8:
        raise ValueError('one to eight parallel simulations only')
    plan = json.loads((root / 'execution-plan.json').read_text())
    validate_plan(plan, root)
    frozen_scope()
    if PREV['clean']() != plan['commit']:
        raise ValueError('execution commit changed')
    def one(entry):
        if PREV['clean']() != plan['commit']:
            raise ValueError('source changed before run')
        output = ROOT / entry['directory']
        if output.exists():
            raise ValueError('existing output; no overwrite or incomplete-run reuse')
        print('START', entry['run'], entry['group'], flush=True)
        subprocess.run([sys.executable, str(HERE / 'run-final-scenario.py'), '--output-dir', str(output),
            '--protection-mode', 'compfrr', '--placement-mode', 'n5c', '--input-staging-policy', 'deferred',
            '--remote-busy-recovery-policy', 'relocate', '--n5c-variant', entry['group'],
            '--random-run', str(entry['run'])], cwd=ROOT, check=True)
        verify(output, entry['run'], entry['group'], plan['commit'])
        if PREV['clean']() != plan['commit']:
            raise ValueError('source changed during run')
        print('FINISHED', entry['run'], entry['group'], flush=True)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(one, [e for e in plan['entries'] if e['new']]))
    print('EIGHT_U_RUNS_COMPLETE; audit then stop for user review', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT / 'output/recovery-u-revalidation')
    parser.add_argument('--phase', choices=('prepare', 'run', 'all'), required=True)
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    root = args.root.resolve()
    if args.phase in ('prepare', 'all'):
        prepare(root)
    if args.phase in ('run', 'all'):
        execute(root, args.jobs)
