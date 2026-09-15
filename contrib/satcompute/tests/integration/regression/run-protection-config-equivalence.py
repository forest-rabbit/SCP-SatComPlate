#!/usr/bin/env python3
"""Old/new configuration wiring on four tasks/15 s; never a performance matrix."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[5]
EQUIVALENCE = runpy.run_path(str(Path(__file__).with_name('run-protection-equivalence.py')))


def cases(root):
    fixture = ROOT / 'contrib/satcompute/tests/fixtures'
    common = ['satcompute', '--simulationDuration=15', '--randomSeed=1', '--randomRun=11',
              f'--constellationConfig={fixture}/constellation/connected-16.csv',
              f'--taskTrace={fixture}/protection/fixed-four-profiles.json',
              f'--computeProfile={fixture}/protection/fixed-compute.json',
              '--faultMode=generate', '--faultEnableF1=1', '--faultEnableF2=1',
              '--faultEnableF3=0', '--taskCompletionPolicy=report',
              '--routingMode=global-capacity-aware-hrw', '--islBandwidthBps=10000000000',
              '--delayMode=fixed', '--fixedDelay=0.001']
    result = {}

    def add(name, mode, placement='fa-ffp', busy='relocate', policy='eager', variant='full', extra=()):
        argv = common + [f'--protectionMode={mode}', f'--placementMode={placement}',
                         f'--remoteBusyRecoveryPolicy={busy}', f'--inputPolicy={policy}',
                         f'--n5cVariant={variant}', f'--outputDir={root/name}']
        keys = {item.partition('=')[0] for item in extra}
        result[name] = [item for item in argv if item.partition('=')[0] not in keys] + list(extra)

    for placement in ('ffp', 'fa-ffp', 'lrl', 'fa-lrl'):
        for mode in ('recompute', 'one-plus-one'):
            add(f'{mode}-{placement}', mode, placement)
        for busy in ('recompute', 'relocate'):
            add(f'fixed-{placement}-{busy}', 'fixed', placement, busy)
            add(f'cb-{placement}-{busy}', 'checkbullet', placement, busy)
    for placement in ('ffp', 'fa-ffp', 'lrl', 'fa-lrl', 'n5c'):
        for policy in ('eager', 'deferred', 'selective'):
            add(f'adaptive-{placement}-{policy}', 'compfrr', placement, policy=policy)
    for variant in ('noR', 'noU', 'noM', 'rational-U'):
        add(f'p-{variant}', 'compfrr', 'n5c', policy='deferred', variant=variant)
    add('adaptive-recompute', 'compfrr', policy='selective', busy='recompute')
    add('off', 'off')
    add('shadow', 'off', extra=('--compfrr-shadow=1',))
    add('fixed-none', 'fixed', extra=('--faultMode=none',))
    add('fixed-zero-pool', 'fixed', extra=('--backupStorageBytesPerNode=0',))
    add('fixed-cadence', 'fixed', extra=('--fixedProtectionDelta=0.1', '--fixedProtectionBatchN=2'))
    for mode in ('fixed', 'compfrr', 'recompute', 'one-plus-one', 'checkbullet'):
        add(f'f3-{mode}', mode, extra=('--faultEnableF3=1', '--faultF3Mode=controlled',
                                     '--faultF3Node=3', '--faultF3Time=1.4'))
    # Preserve the old omitted CB defaults, rather than replacing them with canonical recompute.
    result['cb-old-defaults'] = common + ['--protectionMode=checkbullet', f'--outputDir={root}/cb-old-defaults']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', required=True, type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--legacy', action='store_true', help='Capture the pre-refactor binary only')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error('jobs must be in [1,4]')
    root = args.output_root.resolve()
    if root.exists():
        parser.error('preserve existing evidence; output already exists')
    commands = cases(root)
    if not args.legacy:
        translate = runpy.run_path(str(ROOT / 'contrib/satcompute/tests/support/protection/config_arguments.py'))['execution_arguments']
        commands = {name: translate(argv) for name, argv in commands.items()}
    root.mkdir(parents=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(lambda item: EQUIVALENCE['execute'](root/item[0], item[1], translate=False), commands.items()))
    result = EQUIVALENCE['compare'](args.reference.resolve(), root) if args.reference else {'status': 'BASELINE_CAPTURED'}
    result.update(cases=len(commands), tasks_per_case=4, simulation_duration_s=15)
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
