"""Offline causal-bound proof audit; unknown means DEFER, never an empirical bound."""
from decimal import Decimal, localcontext
from fractions import Fraction
import json
import math
from pathlib import Path
import runpy
import subprocess

PARTIAL = runpy.run_path(str(Path(__file__).with_name('input_partial_predictability_audit.py')))
CP, COINIT, NET, API = (PARTIAL[k] for k in ('CP', 'COINIT', 'NET', 'API'))
require = API['require']

# Reviewed source obligations, not a heuristic classifier or a runtime capability inference.
FACTS = (
    ('NTH_BATCH_PENDING', 'protection/mechanism/checkpoint/checkpoint-manager.cc',
     'if (state.futurePaused || state.batchInFlight || state.remoteTransferFailed)',
     'n receipts form a batch, but remote progress requires receipt plus cR; local capture continues.',
     'No invariant localWork-remoteWork <= (n-1)*delta*W, including n=1.'),
    ('BLOCKED_BACKLOG', 'protection/mechanism/checkpoint/checkpoint-manager.cc',
     'Block(state, false, "PATH")',
     'Remote PATH/STORAGE/TRANSFER_FAILED blocks do not impose a local backlog-count cap.',
     'Need a proven gap bound across holds/failures; finite storage alone is not this cadence bound.'),
    ('ON_UPDATE', 'protection/mechanism/checkpoint/checkpoint-manager.cc',
     'state.config.batchN = n;',
     'Updates change future cadence, not old records or the immutable batch target.',
     'START delta/n do not bound every later gap; shrinking n does not shrink existing records.'),
    ('INITIALIZATION', 'protection/mechanism/checkpoint/checkpoint-manager.cc',
     'result.phase = before.initialized ? "ON" : "INITIALIZING";',
     'START has no committed checkpoint; initial state generation/transfer/merge can be unfinished.',
     'Analytic init-ready time is not a guaranteed receipt; retain early and later uncertain branches.'),
    ('STRICT_PREFIX', 'protection/mechanism/checkpoint/checkpoint-progress.cc',
     'm_history.lower_bound(faultNs)',
     'A same-ns commit is unavailable to that fault; l only advances through contiguous receipts.',
     'No sender-finished or pending merge may replace the strictly prior remote prefix.'),
    ('BYTE_LAYOUT', 'protection/common/task-state-adapter.cc',
     'return Add(StateBytes(to) - StateBytes(from), m_header);',
     'Real records include H and legal tile/token rounding; retry skips elapsed capture targets.',
     'Need actual bounded record endpoints/count and headers, not Kvar times an ideal fraction.'),
    ('CURRENT_PATH_ONLY', 'traffic/network-transfer-engine.cc',
     'AdmissiblePathEstimate::TransferTimeNs',
     'Current path serialization plus propagation is an estimate, not future service reservation.',
     'Need future positive service/availability guarantee, propagation and dispatch/receipt bounds.'),
    ('RECOVERY_BRANCH', 'protection/runtime/recovery-controller.cc',
     'return TryRelocate(state);',
     'Future recovery can select REDO, TAIL, or another target with remote-state transfer.',
     'A bound on fixed-pair tail alone does not certify the eventual non-INPUT readiness barrier.'),
)


def source_proof(repo, execution_commit):
    rows = []
    for key, path, anchor, fact, missing in FACTS:
        relative = 'contrib/satcompute/' + path
        text = (repo / relative).read_text()
        executed = subprocess.run(['git', 'show', execution_commit + ':' + relative], cwd=repo,
                                  capture_output=True, text=True, check=True).stdout
        require(text == executed, 'audited production source changed: ' + relative)
        require(anchor in text, 'source proof obligation needs re-audit: ' + key)
        rows.append(dict(obligation=key, source=relative,
                         line=text[:text.index(anchor)].count('\n') + 1,
                         source_fact=fact, missing_guarantee=missing,
                         proposed_upper_status='NOT_A_VALID_GENERAL_BOUND'))
    return rows


def fraction(value):
    require(isinstance(value, (int, float)) and math.isfinite(value) and value >= 0,
            'invalid nonnegative physical/probability quantity')
    return Fraction(str(value))


def seconds(value_ns):
    """Preserve fractional-ns expected value, including values smaller than binary64."""
    with localcontext() as ctx:
        ctx.prec = 40
        return str(Decimal(value_ns.numerator) / Decimal(value_ns.denominator * 10**9))


def gain_ns(input_ns, remaining_ns, upper_ns):
    require(type(input_ns) is int and type(remaining_ns) is int and
            type(upper_ns) is int and 0 <= remaining_ns <= input_ns and upper_ns >= 0,
            'invalid integer dependency times')
    return max(input_ns, upper_ns) - max(remaining_ns, upper_ns)


def aggregate_bounds(input_ns, start_ns, steps, upper_bounds):
    """None is unavailable, not A=0. Values conditional on the frozen INPUT-path model."""
    require(type(input_ns) is int and input_ns >= 0 and type(start_ns) is int and start_ns >= 0,
            'invalid INPUT/start time')
    require(len(steps) == len(upper_bounds), 'missing branch, never discard its probability mass')
    lower, upper, mass = Fraction(), Fraction(), Fraction()
    unknown_mass = Fraction()
    detail = []
    for step, bound in zip(steps, upper_bounds):
        require(type(step['time_ns']) is int and step['time_ns'] >= 0, 'invalid future time')
        w = fraction(step['first_failure_mass'])
        require(w <= 1, 'invalid first-failure mass')
        mass += w
        remaining = max(0, input_ns - max(0, step['time_ns'] - start_ns))
        low = 0 if bound is None else gain_ns(input_ns, remaining, bound)
        high = input_ns - remaining
        if bound is None:
            unknown_mass += w
        lower += w * low
        upper += w * high
        detail.append(dict(time_ns=step['time_ns'], first_failure_mass=step['first_failure_mass'],
                           input_deferred_ns=input_ns, input_send_remaining_ns=remaining,
                           A_upper_ns=bound, delta_lower_ns=low, delta_upper_ns=high,
                           A_upper_status='NOT_AVAILABLE' if bound is None else 'SUPPLIED_BOUND'))
    require(mass <= Fraction('1.000000000001'), 'first-failure mass exceeds one')
    require(0 <= lower <= upper, 'nonnegative benefit bounds violated')
    return dict(lower=lower, upper=upper, mass=mass, unknown_mass=unknown_mass,
                sign='ROBUST_POSITIVE' if lower > 0 else 'ROBUST_ZERO' if upper == 0 else 'SIGN_UNCERTAIN',
                action='SEND' if lower > 0 else 'DEFER', steps=detail)


def proposed_upper_ns(row):
    """Ceiling of the user's theoretical candidate; NOT a certified causal upper bound."""
    k, b, c = (fraction(row[x]) for x in ('Kvar_bytes', 'backup_bandwidth_bytes_per_s', 'cR_ns'))
    d, n = row['delta_permille'], row['batch_n']
    require(type(d) is int and type(n) is int and d > 0 and n > 0 and n*d <= 1000 and b > 0,
            'invalid committed cadence/bandwidth')
    value = k * (n-1) * d * 10**9 / (1000*b) + c
    return math.ceil(value)


def causal_features(row):
    """No labels, realized paths/faults or profile-specific rules enter this function."""
    steps, input_ns, start = row['_steps'], row['T_I_net_ns'], row['initializing_start_time_ns']
    # The source audit has found no certificate for ANY future branch at this START.
    strict = aggregate_bounds(input_ns, start, steps, [None] * len(steps))
    candidate = proposed_upper_ns(row)
    assumed = aggregate_bounds(input_ns, start, steps,
        [None if s['time_ns'] <= row['estimated_init_ready_time_ns'] else candidate for s in steps])
    require(math.isclose(float(strict['mass']), row['P_F'], rel_tol=1e-12, abs_tol=1e-12),
            'canonical probability mass changed')
    point = row['G_cp_s']
    return dict(task_id=row['task_id'], P_F=row['P_F'],
        actual_local=row['local_node'], actual_remote=row['remote_node'],
        T_I_net_ns=input_ns, A_hat_ns=row['A_hat_ns'],
        delta_permille=row['delta_permille'], batch_n=row['batch_n'],
        Kvar_bytes=row['Kvar_bytes'], backup_bandwidth_bytes_per_s=row['backup_bandwidth_bytes_per_s'],
        cR_ns=row['cR_ns'], G_hat_s=point,
        point_class='UNKNOWN' if point is None else 'POSITIVE' if point > 0 else 'ZERO',
        causal_A_upper_ns=None, causal_A_bound_status='NOT_AVAILABLE',
        causal_G_lower_s=seconds(strict['lower']), causal_G_upper_s=seconds(strict['upper']),
        causal_unknown_mass=float(strict['unknown_mass']), causal_sign=strict['sign'],
        causal_candidate_action=strict['action'],
        proposed_formula_upper_ns=candidate,
        proposed_formula_status='UNPROVEN_FORMULA_DIAGNOSTIC_ONLY',
        assumed_formula_G_lower_s=seconds(assumed['lower']),
        assumed_formula_positive=assumed['lower'] > 0,
        early_unknown_mass=float(assumed['unknown_mass'])), strict['steps']


def selection_metrics(group, selected):
    wait = sum(r['critical_wait_ns'] for r in group)
    total_bytes = sum(r['network_bytes'] for r in group)
    needed = sum(r['label'] == 'NEEDED' for r in group)
    missed = [r for r in group if r['task_id'] not in {s['task_id'] for s in selected}]
    selected_wait = sum(r['critical_wait_ns'] for r in selected)
    selected_needed = sum(r['label'] == 'NEEDED' for r in selected)
    return dict(candidate_count=len(group), selected_count=len(selected),
        selected_fraction=len(selected)/len(group) if group else None,
        planned_staged_application_bytes=sum(r['network_bytes'] for r in selected),
        all_stage_bytes=total_bytes,
        planned_bytes_fraction=sum(r['network_bytes'] for r in selected)/total_bytes if total_bytes else None,
        total_needed=needed, selected_needed=selected_needed,
        needed_recall=selected_needed/needed if needed else None,
        selected_no_need=len(selected)-selected_needed,
        selected_no_fault=sum(r['label'] == 'NO_FAULT' for r in selected),
        captured_wait_ns=selected_wait, total_wait_ns=wait,
        captured_wait_share=selected_wait/wait if wait else None,
        uncovered_needed=sum(r['label'] == 'NEEDED' for r in missed),
        uncovered_wait_ns=sum(r['critical_wait_ns'] for r in missed),
        uncovered_max_task_wait_ns=max((r['critical_wait_ns'] for r in missed), default=0))


def class_metrics(group, members):
    """Bytes/waits owned by a sign class, not bytes sent or waits captured by a rule."""
    m = selection_metrics(group, members)
    return dict(candidate_count=len(group), member_count=len(members),
        member_fraction=m['selected_fraction'], member_input_bytes=m['planned_staged_application_bytes'],
        member_bytes_fraction=m['planned_bytes_fraction'], member_needed=m['selected_needed'],
        member_no_need=m['selected_no_need'], member_no_fault=m['selected_no_fault'],
        member_observed_wait_ns=m['captured_wait_ns'], member_observed_wait_share=m['captured_wait_share'])


def assemble(population, recoveries):
    require(population and all(r['network_bytes'] > 0 and r['cohort'] == COINIT['COHORT'] for r in population),
            'mixed/non-network cohort')
    features, branches = [], []
    for row in population:
        f, steps = causal_features(row)
        features.append(f)
        branches += [dict(task_id=row['task_id'], **s) for s in steps]
    # Only after causal values are frozen, attach retrospective labels for evaluation.
    joined = [dict(r, **f) for r, f in zip(population, features)]
    tables = {'gi-causal-bound-features': features, 'gi-causal-bound-branches': branches,
              'gi-admission-summary': [], 'gi-profile-summary': [],
              'gi-sign-class-summary': [], 'gi-sign-profile-summary': []}
    selectors = {
        'CAUSAL_GL_POSITIVE': lambda r: r['causal_candidate_action'] == 'SEND',
        'UNPROVEN_FORMULA_POSITIVE_DIAGNOSTIC': lambda r: r['assumed_formula_positive'],
        'PRIOR_POINT_POSITIVE_REFERENCE': lambda r: r['point_class'] == 'POSITIVE',
        'ALL_STAGE_REFERENCE': lambda r: True,
        'NONE_STAGE_REFERENCE': lambda r: False,
    }
    summary = dict(status='CAUSAL_UPPER_BOUND_AUDIT_COMPLETE', purpose='DEVELOPMENT_CALIBRATION',
        production_ready=False, production_rule=None, production_threshold=None, epsilon=None,
        causal_A_upper_bound='NOT_AVAILABLE', proposed_formula='NOT_A_VALID_GENERAL_BOUND',
        audit_candidate='SEND iff certified G_L > 0; every other case remains Deferred',
        bound_scope='G range conditional on frozen INPUT timing and common recovery-dependency model; not actual intervention gain.',
        unknown_is_not_zero_gain=True, views={})
    for view, group in COINIT['views'](joined):
        rows = []
        for name, select in selectors.items():
            selected = [r for r in group if select(r)]
            row = dict(view=view, cohort_class=name, **selection_metrics(group, selected))
            rows.append(row)
            for profile in sorted({r['profile'] for r in group}):
                members = [r for r in group if r['profile'] == profile]
                tables['gi-profile-summary'].append(dict(view=view, cohort_class=name, profile=profile,
                    **selection_metrics(members, [r for r in members if select(r)])))
        tables['gi-admission-summary'] += rows
        summary['views'][view] = rows
        for sign in ('ROBUST_POSITIVE', 'ROBUST_ZERO', 'SIGN_UNCERTAIN'):
            members = [r for r in group if r['causal_sign'] == sign]
            tables['gi-sign-class-summary'].append(dict(view=view, sign_class=sign,
                **class_metrics(group, members)))
            for profile in sorted({r['profile'] for r in group}):
                all_profile = [r for r in group if r['profile'] == profile]
                tables['gi-sign-profile-summary'].append(dict(view=view, sign_class=sign, profile=profile,
                    **class_metrics(all_profile, [r for r in members if r['profile'] == profile])))
    observations = PARTIAL['realized_barriers'](population, recoveries)
    by_id = {f['task_id']: f for f in features}
    audit = []
    for obs in observations:
        f = by_id[obs['task_id']]
        value = obs['A_observed_ns']
        audit.append(dict(task_id=obs['task_id'], fault_source=obs['fault_source'],
            status=obs['status'], A_observed_ns=value,
            proposed_formula_upper_ns=f['proposed_formula_upper_ns'],
            observed_exceeds_proposal=None if value is None else value > f['proposed_formula_upper_ns'],
            observed_gap_wu=None if value is None else
                int(next(r for r in recoveries if r['task_id'] == obs['task_id'])['local_work_units']) -
                int(next(r for r in recoveries if r['task_id'] == obs['task_id'])['remote_work_units']),
            interpretation='Retrospective fixed-target observation only, never a bound input/certificate.'))
    tables['a-proposed-bound-observation-audit'] = audit
    summary['observed_formula_checks'] = {view: dict(
        comparable=len(group), violations=sum(r['observed_exceeds_proposal'] for r in group))
        for view, group in COINIT['views']([r for r in audit if r['A_observed_ns'] is not None])}
    return tables, summary


def write_audit(root, verified, criticalpath, output, probe):
    population, data, prior, protected, before = PARTIAL['load_validated_population'](
        root, verified, criticalpath, probe)
    API['HISTORY']['output_guard'](output, protected)
    repo = Path(__file__).resolve().parents[5]
    proof = source_proof(repo, data['identity']['execution_commit'])
    result = subprocess.run([str(probe), '--checkpoint-bound-witness'], capture_output=True, text=True, check=True)
    witness = []
    for line in result.stdout.splitlines():
        key, actual, claimed = line.split('\t')
        require(int(actual) > int(claimed), 'native counterexample missing')
        witness.append(dict(witness=key, observed=int(actual), proposed_limit=int(claimed),
                            scope='PURE_CALLBACK_CONTRACT_NOT_A_SIMULATION'))
    require(len(witness) == 7, 'incomplete native proof witnesses')
    tables, summary = assemble([r for r in population if r['network_bytes'] > 0],
                               API['rows'](root, 'recovery-summary.csv'))
    tables['a-causal-bound-audit'] = proof
    tables['a-native-contract-witnesses'] = witness
    tables['local-delivery-reference'] = [dict(task_id=r['task_id'], profile=r['profile'],
        label=r['label'], network_bytes=0, interpretation='Preserve logical LocalDelivery; no UDP or network admission.')
        for r in population if r['network_bytes'] == 0]
    summary.update(source_identity=data['identity'], prior_runtime_equivalence=prior['prior_runtime_equivalence'],
        source_obligations=proof, native_witness_count=len(witness), simulation_started=False,
        production_source_unchanged=True, v4_features_identical=True,
        sources=dict(raw=str(root), verified=str(verified), criticalpath=str(criticalpath)),
        raw_evidence_read_only=before == API['evidence_metadata'](protected))
    require(summary['raw_evidence_read_only'], 'historical evidence changed')
    output.mkdir(parents=True, exist_ok=False)
    for name, records in tables.items():
        API['HISTORY']['write_csv'](output/(name+'.csv'), records)
    (output/'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False)+'\n')
    require(before == API['evidence_metadata'](protected), 'historical evidence changed during output')
    return summary
