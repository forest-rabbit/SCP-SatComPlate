#!/usr/bin/env python3
"""Frozen six-scheme run11 comparison; no tuning, recalibration, or automatic Git/CI."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import runpy
import sys

ROOT = Path(__file__).resolve().parents[5]
SUPPORT = ROOT / 'contrib/satcompute/tests/support/protection'
CB_TOOLS = ROOT / 'contrib/satcompute/protection/baseline/checkbullet/tools'
CONTROL = runpy.run_path(str(CB_TOOLS / 'cb_tools.py'))
SCENE = runpy.run_path(str(SUPPORT / 'scenario.py'))
GROUPS = {
    'recompute': ('Recompute / FA-FFP', 'recompute'),
    'one-plus-one': ('1+1 / FA-FFP', 'one-plus-one'),
    'cb-sat': ('CB-SAT / FA-FFP / Recompute', 'cb-sat'),
    'multitree': ('Multi-tree (Published FT Rule) / FA-FFP', 'multitree'),
    'compfrr-p': ('CompFRR / CompFRR-P / Selective / Relocate', 'compfrr'),
    'compfrr-fa-ffp': ('CompFRR / FA-FFP / Selective / Relocate', 'compfrr'),
}


def arguments(output, group, small=False):
    argv = SCENE['canonical_experiment_arguments'](SCENE['arguments'](output))
    argv = [x for x in argv if not x.startswith('--protectionScheme=')]
    scheme = GROUPS[group][1]
    argv += [f'--protectionScheme={scheme}']
    if scheme == 'compfrr':
        placement = 'compfrr' if group == 'compfrr-p' else 'fa-ffp'
        argv += ['--compfrrCheckpointPolicy=adaptive', f'--compfrrPlacementPolicy={placement}',
                 '--compfrrInputPolicy=selective', '--compfrrRecoveryPolicy=relocate']
        if placement == 'compfrr':
            argv += ['--compfrrPressureModel=cumulative', '--compfrrPlacementAblation=none']
    # Baseline private defaults are explicitly recorded in execution identity;
    # no inapplicable CompFRR INPUT/busy flags are pushed into native RS/RP.
    if scheme in ('cb-sat', 'compfrr'):
        argv += ['--backupStorageBytesPerNode=10000000000']
    if small:
        fixture = ROOT / 'contrib/satcompute/tests/fixtures'
        override = dict(simulationDuration='15', faultEnableF3='0',
            constellationConfig=str(fixture / 'constellation/connected-16.csv'),
            computeProfile=str(fixture / 'protection/fixed-compute.json'),
            taskTrace=str(fixture / 'protection/fixed-four-profiles.json'))
        argv = [x for x in argv if x.lstrip('-').partition('=')[0] not in override and
                not x.startswith(('--faultF3Mode=', '--faultF3Node=', '--faultF3Time='))]
        argv += [f'--{k}={v}' for k, v in override.items()]
    return argv


def audit_all(root):
    api = runpy.run_path(str(SUPPORT / 'multitree_comparison_audit.py'))
    report = api['comparison'](root, GROUPS)
    CONTROL['write_json'](root / 'comparison.json', report)
    (root / 'comparison.md').write_text(api['markdown'](report))
    print((root / 'comparison.md').read_text(), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', required=True, type=Path)
    parser.add_argument('--jobs', type=int, choices=range(1, 7), default=6)
    parser.add_argument('--smoke', action='store_true', help='15s four-profile wiring gate, not formal evidence')
    parser.add_argument('--audit-only', action='store_true')
    parser.add_argument('--describe', action='store_true', help='Read-only command/profile preview')
    args = parser.parse_args()
    root = args.output_root.resolve()
    commands = {g: arguments(root / g, g, args.smoke) for g in GROUPS}
    if args.describe:
        print(json.dumps(commands, indent=2)); return
    if args.audit_only:
        audit_all(root); return
    head = CONTROL['clean_head']()
    scene = CONTROL['scene_identity']() if not args.smoke else {'fixture': 'fixed-four-profiles', 'duration_s': 15}
    root.mkdir(parents=True, exist_ok=False)
    profile = json.loads((CB_TOOLS.parent / 'calibration/frozen-mtbf-profile.json').read_text())
    statuses = {g: 'PENDING' for g in GROUPS}
    def save():
        CONTROL['write_json'](root / 'matrix-status.json', dict(commit=head, groups=statuses))
    def execute(group):
        scheme = GROUPS[group][1]
        identity = dict(stage='multitree-smoke' if args.smoke else 'multitree-comparison-run11',
            scene=scene, label=GROUPS[group][0], protection_mode=scheme,
            placement_mode='compfrr' if group == 'compfrr-p' else 'fa-ffp',
            remote_busy_recovery_policy=('relocate' if scheme == 'compfrr' else
                                        'recompute' if scheme == 'cb-sat' else 'not-applicable'),
            input_policy='selective' if scheme == 'compfrr' else 'scheme-native')
        if scheme == 'cb-sat': identity['mtbf_seconds'] = profile['mtbf_seconds']
        return CONTROL['execute'](root / group, commands[group], head, identity)
    save()
    failures = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {}
        for group in GROUPS:
            statuses[group] = 'RUNNING'; save()
            print(f'START {group}', flush=True)
            futures[pool.submit(execute, group)] = group
        for future in as_completed(futures):
            group = futures[future]
            try:
                future.result(); statuses[group] = 'FINISHED'
            except Exception as error:
                statuses[group] = 'FAILED'; failures.append(f'{group}: {error}')
            save()
    if failures: raise RuntimeError('; '.join(failures))
    audit_all(root)


if __name__ == '__main__':
    main()
