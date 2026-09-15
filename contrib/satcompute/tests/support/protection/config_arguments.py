"""Explicit old-to-new execution adapter; not a production CLI alias or model.

The old command-description API remains readable for recorded experiments. Only
launch sites use this adapter. Private baseline variants use the test driver;
canonical CB and historical omitted-busy CB must never collapse to one identity.
"""
import math

OLD = {'protectionMode', 'placementMode', 'n5cVariant', 'inputPolicy',
       'remoteBusyRecoveryPolicy', 'lrlRecoveryWeight', 'fixedProtectionDelta',
       'fixedProtectionBatchN'}
SHARED = {'backupStorageBytesPerNode'}
NEW = {'protectionScheme', 'compfrrCheckpointPolicy', 'compfrrPlacementPolicy',
       'compfrrInputPolicy', 'compfrrRecoveryPolicy', 'compfrrPressureModel',
       'compfrrPlacementAblation', 'compfrrFixedDelta', 'compfrrFixedBatchN',
       'testBaselinePlacement', 'testCbSatBusyPolicy', 'testLrlRecoveryWeight'}
PLACEMENTS = ('ffp', 'fa-ffp', 'lrl', 'fa-lrl')
INACTIVE_REASON = {
    'placementMode': 'off constructs no placement policy',
    'remoteBusyRecoveryPolicy': 'off/recompute/one-plus-one did not consume checkpoint busy policy',
    'inputPolicy': 'off and complete baselines did not consume the default eager selector',
    'fixedProtectionDelta': 'only the fixed controller consumed fixed cadence',
    'fixedProtectionBatchN': 'only the fixed controller consumed fixed batch size',
    'n5cVariant': 'default full was not consumed outside CompFRR-P placement',
    'lrlRecoveryWeight': 'only active LRL/FA-LRL placement consumed recovery weight',
    'backupStorageBytesPerNode': 'no checkpoint-pool consumer in this scheme',
}


def translate_protection_arguments(argv):
    """Map current pre-hierarchy semantics; older mechanism versions need their own audit.

    Return original argv and explicit inactive fields alongside the launch argv.
    No writes, subprocesses, RNG, inferred new profiles, or default promotion.
    """
    argv = list(argv)
    if not argv or argv[0] not in ('satcompute', 'satcompute-protection-config-driver'):
        return dict(argv=argv, original=argv, identity=None, inactive={}, inactive_reasons={})
    flags, rest = {}, []
    index = 1
    while index < len(argv):
        token = argv[index]
        key, sep, value = token.lstrip('-').partition('=')
        if not token.startswith('-') or key not in OLD | NEW | SHARED:
            rest.append(token)
            index += 1
            continue
        if not sep:
            index += 1
            if index == len(argv) or argv[index].startswith('--'):
                raise ValueError(f'missing value: {key}')
            value = argv[index]
        if key in flags:
            raise ValueError(f'duplicate protection option: {key}')
        flags[key] = value
        index += 1
    if not flags.keys() & OLD:
        return dict(argv=argv, original=argv, identity=None, inactive={}, inactive_reasons={})
    if flags.keys() & NEW:
        raise ValueError('conflicting old/new protection configuration')

    mode = flags.get('protectionMode', 'off')
    placement = flags.get('placementMode', 'fa-ffp')
    busy = flags.get('remoteBusyRecoveryPolicy', 'relocate')
    policy = flags.get('inputPolicy', 'eager')
    variant = flags.get('n5cVariant', 'full')
    weight = int(flags.get('lrlRecoveryWeight', '1'))
    delta = float(flags.get('fixedProtectionDelta', '.05'))
    batch = int(flags.get('fixedProtectionBatchN', '4'))
    if mode not in ('off', 'fixed', 'compfrr', 'recompute', 'one-plus-one', 'checkbullet'):
        raise ValueError('unknown historical protection scheme')
    if placement not in (*PLACEMENTS, 'n5c') or (placement == 'n5c' and mode != 'compfrr'):
        raise ValueError('unsupported historical placement')
    if mode == 'off' and placement in ('lrl', 'fa-lrl'):
        raise ValueError('historical off did not accept LRL')
    if busy not in ('recompute', 'relocate') or policy not in ('eager', 'deferred', 'selective'):
        raise ValueError('unsupported historical recovery/INPUT option')
    if policy != 'eager' and mode != 'compfrr':
        raise ValueError('historical non-eager INPUT required adaptive CompFRR')
    variants = {'full': ('cumulative', 'none'), 'noR': ('cumulative', 'noR'),
                'noU': ('cumulative', 'noU'), 'noM': ('cumulative', 'noM'),
                'rational-U': ('idle-aware', 'none')}
    if variant not in variants or (placement != 'n5c' and variant != 'full'):
        raise ValueError('historical-only/unsupported placement variant; do not execute recent-U')
    if (not math.isfinite(delta) or not 0 < delta <= 1 or
            abs(delta*1000-round(delta*1000)) > 1e-9 or
            not 1 <= batch <= 1000//max(1, round(delta*1000)) or not 0 <= weight < 2**32):
        raise ValueError('invalid historical fixed cadence or LRL weight')
    if 'backupStorageBytesPerNode' in flags and not 0 <= int(flags['backupStorageBytesPerNode']) < 2**64:
        raise ValueError('invalid historical checkpoint capacity')
    scheme = {'fixed': 'compfrr', 'checkbullet': 'cb-sat'}.get(mode, mode)
    result = [f'--protectionScheme={scheme}']
    consumed = {'protectionMode'}
    driver = False
    if mode in ('fixed', 'compfrr'):
        result += [f'--compfrrCheckpointPolicy={"fixed" if mode == "fixed" else "adaptive"}',
                   f'--compfrrPlacementPolicy={"compfrr" if placement == "n5c" else placement}',
                   f'--compfrrRecoveryPolicy={busy}', f'--compfrrInputPolicy={policy}']
        consumed |= {'placementMode', 'remoteBusyRecoveryPolicy', 'inputPolicy'}
        if mode == 'fixed':
            result += [f'--compfrrFixedDelta={flags.get("fixedProtectionDelta", ".05")}',
                       f'--compfrrFixedBatchN={batch}']
            consumed |= {'fixedProtectionDelta', 'fixedProtectionBatchN'}
        if placement == 'n5c':
            pressure, ablation = variants[variant]
            result += [f'--compfrrPressureModel={pressure}', f'--compfrrPlacementAblation={ablation}']
            consumed.add('n5cVariant')
    elif mode != 'off':
        consumed.add('placementMode')
        if placement != 'fa-ffp':
            result += [f'--testBaselinePlacement={placement}']
            driver = True
        if mode == 'checkbullet':
            consumed.add('remoteBusyRecoveryPolicy')
            if busy != 'recompute':
                result += [f'--testCbSatBusyPolicy={busy}']
                driver = True
    if mode in ('fixed', 'compfrr', 'checkbullet') and 'backupStorageBytesPerNode' in flags:
        result += [f'--backupStorageBytesPerNode={flags["backupStorageBytesPerNode"]}']
        consumed.add('backupStorageBytesPerNode')
    if mode != 'off' and placement in ('lrl', 'fa-lrl'):
        consumed.add('lrlRecoveryWeight')
        if weight != 1:
            result += [f'--testLrlRecoveryWeight={weight}']
            driver = True
    identity = dict(scheme=scheme,
                    checkpoint_policy=('fixed' if mode == 'fixed' else 'adaptive') if mode in ('fixed', 'compfrr') else None,
                    placement=placement if mode != 'off' else None,
                    fallback=busy if mode in ('fixed', 'compfrr', 'checkbullet') else None,
                    input_policy=policy if mode in ('fixed', 'compfrr') else None,
                    pressure_ablation=variants[variant] if placement == 'n5c' else None,
                    lrl_weight=weight if mode != 'off' and placement in ('lrl', 'fa-lrl') else None)
    inactive = {key: value for key, value in flags.items() if key not in consumed}
    return dict(argv=['satcompute-protection-config-driver' if driver else 'satcompute', *rest, *result],
                original=argv, identity=identity,
                inactive=inactive, inactive_reasons={key: INACTIVE_REASON[key] for key in inactive})


def execution_arguments(argv):
    """Return executable arguments; callers retain their original evidence separately."""
    return translate_protection_arguments(argv)['argv']


def canonical_protection_arguments(argv):
    """Read-only experiment identity, including archived recent-U (never executable).

    Compare active options with defaults expanded. Ignore only the old explicitly
    inactive fields identified by the adapter, not arbitrary command parameters.
    The test driver and ordinary driver share runtime wiring. Their executable
    names normalize, but private placement/busy overrides remain in the identity.
    """
    argv = list(argv)
    archived = '--n5cVariant=recent-U' in argv
    if archived:
        argv[argv.index('--n5cVariant=recent-U')] = '--n5cVariant=full'
    argv = execution_arguments(argv)
    flags, other = {}, []
    for token in argv[1:]:
        key, sep, value = token.removeprefix('--').partition('=')
        if key not in NEW | SHARED:
            other.append(token)
        else:
            if not sep or key in flags:
                raise ValueError('duplicate/malformed protection evidence option')
            flags[key] = value
    scheme = flags.get('protectionScheme', 'off')
    if scheme not in ('off', 'compfrr', 'recompute', 'one-plus-one', 'cb-sat'):
        raise ValueError('unsupported protection evidence scheme')
    canonical = {'protectionScheme': scheme}
    if scheme == 'compfrr':
        canonical.update(compfrrCheckpointPolicy=flags.get('compfrrCheckpointPolicy', 'adaptive'),
                         compfrrPlacementPolicy=flags.get('compfrrPlacementPolicy', 'fa-ffp'),
                         compfrrInputPolicy=flags.get('compfrrInputPolicy', 'eager'),
                         compfrrRecoveryPolicy=flags.get('compfrrRecoveryPolicy', 'relocate'))
        placement = canonical['compfrrPlacementPolicy']
        if canonical['compfrrCheckpointPolicy'] == 'fixed':
            canonical.update(compfrrFixedDelta=format(float(flags.get('compfrrFixedDelta', '.05')), '.15g'),
                             compfrrFixedBatchN=str(int(flags.get('compfrrFixedBatchN', '4'))))
        if placement == 'compfrr':
            canonical.update(compfrrPressureModel=flags.get('compfrrPressureModel', 'cumulative'),
                             compfrrPlacementAblation=flags.get('compfrrPlacementAblation', 'none'))
            if archived:
                canonical['compfrrPressureModel'] = 'historical-only:recent-U'
    elif scheme != 'off':
        placement = flags.get('testBaselinePlacement', 'fa-ffp')
        canonical['testBaselinePlacement'] = placement
        if scheme == 'cb-sat':
            canonical['testCbSatBusyPolicy'] = flags.get('testCbSatBusyPolicy', 'recompute')
    else:
        placement = None
    if scheme in ('compfrr', 'cb-sat'):
        canonical['backupStorageBytesPerNode'] = str(int(flags.get('backupStorageBytesPerNode', '10000000000')))
    if placement in ('lrl', 'fa-lrl'):
        canonical['testLrlRecoveryWeight'] = str(int(flags.get('testLrlRecoveryWeight', '1')))
    if flags.keys() - canonical.keys():
        raise ValueError('inactive or cross-scheme new evidence options')
    return ['satcompute', *sorted(other + [f'--{key}={value}' for key, value in canonical.items()])]
