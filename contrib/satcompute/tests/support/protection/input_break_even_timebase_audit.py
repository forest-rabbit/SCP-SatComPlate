"""Two fixed offline INPUT screens; no runtime selector, threshold sweep or simulation."""
from collections import Counter
from decimal import Decimal, localcontext
from fractions import Fraction
import json
import math
from pathlib import Path
import runpy
import subprocess

NET = runpy.run_path(str(Path(__file__).with_name('input_latency_resource_audit.py')))
COINIT, API = NET['COINIT'], NET['API']
require = API['require']
OLD = 'SER_SYMMETRIC_BREAK_EVEN'
NEW = 'NET_READY_vs_SER_COST'


def exact(value):
    """Use the supplied decimal representation, never round an expected time to ns."""
    require(type(value) in (int, float) and math.isfinite(value) and value >= 0,
            'invalid nonnegative quantity')
    return Fraction(str(value))


def decimal_text(value):
    with localcontext() as context:
        context.prec = 40
        return str(Decimal(value.numerator) / Decimal(value.denominator))


def evaluate(ser_ns, net_ns, start_ns, steps, pf):
    """Integer event times, exact weighted comparison of canonical serialized masses.

    P_F and w_k are not fitted or renormalized. The tolerance below validates their
    existing floating representation only; it NEVER enters the action comparison.
    A fractional ser_ns is allowed only for the historical timebase diagnostic.
    """
    require(isinstance(ser_ns, (int, Fraction)) and isinstance(net_ns, (int, Fraction))
            and 0 < ser_ns <= net_ns and type(start_ns) is int and start_ns >= 0,
            'invalid transfer/start time')
    p = exact(pf)
    require(p <= 1, 'invalid P_F')
    mass, gs, gn = Fraction(), Fraction(), Fraction()
    previous = -1
    for step in steps:
        t, w = step['time_ns'], exact(step['first_failure_mass'])
        require(type(t) is int and t > previous and w <= 1, 'invalid canonical step')
        previous = t
        lead = max(0, t - start_ns)
        mass += w
        gs += w * min(ser_ns, lead)
        gn += w * min(net_ns, lead)
    require(math.isclose(float(mass), pf, rel_tol=0, abs_tol=1e-12),
            'missing/inconsistent canonical first-failure mass')
    require((p != 0 or mass == 0) and (p != 1 or mass > 0), 'inconsistent endpoint mass')
    cost = (1-p) * ser_ns
    old, new = gs > cost, gn > cost
    require(gn >= gs and (not old or new), 'monotonicity violation')
    require(net_ns != ser_ns or old == new, 'zero-propagation identity violation')
    return dict(g_ser=gs, g_net=gn, cost=cost, mass_residual=mass-p,
                old_integer_action=old, new_action=new)


def action(value):
    return 'PROACTIVE' if value else 'DEFERRED'


def comparison_feature(row, steps):
    """Causal fields only; labels/profile/final targets never affect either decision."""
    if row['network_bytes'] == 0:
        return dict(task_id=row['task_id'], status='LOCAL_DELIVERY',
                    old_decision='N/A_LOCAL', new_decision='N/A_LOCAL')
    if (steps is None or row['P_F'] is None or row['M_pot'] is None or
            row['initializing_start_time_ns'] is None or row['T_I_net_ns'] is None or
            row['network_timing_status'] != 'KNOWN_CURRENT_PATH_POTENTIAL_NOT_ACTUAL_GAIN'):
        return dict(task_id=row['task_id'], status='UNKNOWN',
                    old_decision='UNKNOWN', new_decision='UNKNOWN')
    ts, tn, start = (row[k] for k in ('serialization_rounded_ns', 'T_I_net_ns',
                                     'initializing_start_time_ns'))
    require(type(ts) is int and type(tn) is int and tn-ts == row['T_I_prop_ns'],
            'native serialization/propagation timebase mismatch')
    result = evaluate(ts, tn, start, steps, row['P_F'])
    zero = evaluate(ts, ts, start, steps, row['P_F'])
    # Exact, unrounded S/B is a diagnostic, not a replacement for the frozen float rule.
    raw_ser = Fraction(row['input_bytes'] * 8 * 10**9, row['_rate_bps'])
    fractional = evaluate(raw_ser, raw_ser, start, steps, row['P_F'])
    old = row['M_pot'] > 0
    compatible = old == fractional['old_integer_action'] == result['old_integer_action']
    require(math.isclose(float(result['g_net']/10**9), row['G_I_net_pot_s'],
                         rel_tol=1e-12, abs_tol=1e-15), 'prior end-to-end potential changed')
    return dict(task_id=row['task_id'], status='PASS' if compatible else 'NUMERIC_REVIEW_REQUIRED',
        P_F=row['P_F'], initializing_start_time_ns=start, T_ser_ns=ts, T_net_ns=tn,
        propagation_ns=row['T_I_prop_ns'], T_ser_historical_s=row['T_I_s'],
        U_ser_pot=row['U_pot'], G_net_historical_s=row['G_I_net_pot_s'],
        G_ser_integer_pot_ns=decimal_text(result['g_ser']),
        G_net_pot_ns=decimal_text(result['g_net']),
        extra_serialization_cost_ns=decimal_text(result['cost']),
        new_margin_exact_ns=str(result['g_net']-result['cost']),
        old_decision=action(old), new_decision=action(result['new_action']),
        integer_old_decision=action(result['old_integer_action']),
        exact_fractional_old_decision=action(fractional['old_integer_action']),
        zero_propagation_new_decision=action(zero['new_action']),
        fractional_ns_rounding=decimal_text(ts-raw_ser),
        weight_sum_minus_canonical_pf=decimal_text(result['mass_residual']),
        historical_vs_exact_match=old == fractional['old_integer_action'],
        historical_vs_integer_match=old == result['old_integer_action'],
        zero_propagation_identity=zero['new_action'] == result['old_integer_action'],
        net_gain_ge_serial_gain=result['g_net'] >= result['g_ser'])


def metrics(group, selected, view, name):
    result = COINIT['operating_point'](group, selected, view, name)
    return dict(result, no_fault_selected=sum(r['label'] == 'NO_FAULT' for r in selected),
        fault_noncritical_selected=sum(r['label'] == 'FAULT_NONCRITICAL' for r in selected))


def difference_class(old, new):
    return ('BOTH_PROACTIVE' if new else 'OLD_ONLY') if old else (
        'NEW_ONLY' if new else 'BOTH_DEFERRED')


def tradeoff(old, new):
    db = new['planned_staged_network_bytes'] - old['planned_staged_network_bytes']
    dw = new['captured_actual_critical_wait_ns'] - old['captured_actual_critical_wait_ns']
    require(db >= 0 and dw >= 0, 'fixed-cohort superset metrics decreased')
    return dict(verdict='B. SMALL_REVISION_CROSS_TRADEOFF' if db > 0 and dw > 0
                else 'C. KEEP_ORIGINAL_68_OF_405',
        reason='MORE_BYTES_MORE_COVERED_WAIT' if db > 0 and dw > 0 else
               'DOMINATED_NO_ADDED_WAIT' if db > 0 else 'IDENTICAL_SELECTION',
        delta_planned_bytes=db, delta_captured_wait_ns=dw,
        additional_observed_wait_ms_per_planned_GB=dw*1000/db if db else None,
        interpretation='Descriptive anchor wait coverage per planned byte; NOT saved latency or actual extra traffic.')


def assemble(population):
    require(population and all(r['cohort'] == COINIT['COHORT'] for r in population), 'mixed/empty cohort')
    require(len({r['task_id'] for r in population}) == len(population), 'duplicate candidate')
    causal = [comparison_feature(r, r['_steps']) for r in population]
    joined = [dict(r, **f) for r, f in zip(population, causal)]
    network = [r for r in joined if r['network_bytes'] > 0]
    tables = dict(candidate_comparison=[], decision_diff=[], profile_comparison=[],
                  formula_audit=[f for f in causal if f['status'] != 'LOCAL_DELIVERY'],
                  local_delivery_audit=[dict(task_id=r['task_id'], profile=r['profile'],
                      label=r['label'], network_bytes=0, status='N/A_NETWORK_SCREEN')
                      for r in joined if r['network_bytes'] == 0])
    summary = dict(status='TIMEBASE_AUDIT_COMPLETE', purpose='DEVELOPMENT_CALIBRATION',
        final_performance_result=False, production_rule=None, production_threshold=None,
        simulation_started=False, selective_input_enabled=False,
        timebase='Native ceil serialization and propagation in integer ns; exact Fraction of serialized canonical w/P_F for weighted comparisons; expected times not rounded.',
        historical_rule='Unchanged historical float M_pot > 0; distinct exact fractional/integer diagnostics must match before comparison.',
        probability_tolerance='1e-12 validates source mass only; no epsilon in either decision; no renormalization.',
        total_candidates=len(joined), network_candidates=len(network),
        population_labels=dict(Counter(r['label'] for r in joined)),
        network_labels=dict(Counter(r['label'] for r in network)),
        numeric_or_coverage_blockers=[r['task_id'] for r in network if r['status'] != 'PASS'],
        views={})
    if summary['numeric_or_coverage_blockers']:
        summary.update(status='STOP_FOR_NUMERIC_OR_COVERAGE_REVIEW', verdict='D. INSUFFICIENT_EVIDENCE')
        return tables, summary
    for r in network:
        old, new = r['old_decision'] == 'PROACTIVE', r['new_decision'] == 'PROACTIVE'
        require(not old or new, 'OLD_ONLY: stop for numeric/contract review')
        r['decision_class'] = difference_class(old, new)
        detail = {k:r[k] for k in ('task_id', 'profile', 'P_F', 'input_bytes',
            'T_ser_ns', 'T_net_ns', 'propagation_ns', 'U_ser_pot', 'G_net_pot_ns',
            'extra_serialization_cost_ns', 'new_margin_exact_ns', 'old_decision',
            'new_decision', 'decision_class', 'label')}
        detail.update(planned_INPUT_bytes=r['network_bytes'], observed_wait_ns=r['critical_wait_ns'],
                      historical_fault_source=r['fault_source'],
                      propagation_fraction=r['propagation_ns']/r['T_net_ns'])
        tables['decision_diff'].append(detail)
        tables['candidate_comparison'].append(dict(detail,
            actual_local=r['local_node'], actual_remote=r['remote_node'],
            initializing_start_time_ns=r['initializing_start_time_ns'],
            integer_old_decision=r['integer_old_decision']))
    for view, group in COINIT['views'](network):
        old = [r for r in group if r['old_decision'] == 'PROACTIVE']
        new = [r for r in group if r['new_decision'] == 'PROACTIVE']
        added = [r for r in group if r['decision_class'] == 'NEW_ONLY']
        a, b = metrics(group, old, view, OLD), metrics(group, new, view, NEW)
        summary['views'][view] = dict(old=a, new=b, new_only=metrics(group, added, view, 'NEW_ONLY'),
            difference_counts=dict(Counter(r['decision_class'] for r in group)), tradeoff=tradeoff(a,b))
        for name, chosen in ((OLD, old), (NEW, new), ('NEW_ONLY', added)):
            for profile in sorted({r['profile'] for r in group}):
                members = [r for r in group if r['profile'] == profile]
                tables['profile_comparison'].append(dict(profile=profile, **metrics(members,
                    [r for r in chosen if r['profile'] == profile], view, name)))
    summary['verdict'] = summary['views']['ALL_FAULT']['tradeoff']['verdict']
    summary['f3_out_of_model'] = [dict(task_id=r['task_id'], label=r['label'],
        observed_wait_ns=r['critical_wait_ns'], old_decision=r['old_decision'], new_decision=r['new_decision'])
        for r in network if r['fault_source'] == 'F3']
    return tables, summary


def load_validated(root, verified, coinit, latency, probe):
    """Reconstruct unchanged v2/v3 using their native helper, not the A/critical-path audit."""
    canonical = Path(json.loads((root/'execution.json').read_text())['canonical_reference'])
    protected = [root, verified, coinit, latency, canonical]
    before = API['evidence_metadata'](protected)
    data = COINIT['collect_audit'](root, verified)
    old, net = (json.loads((p/'summary.json').read_text()) for p in (coinit, latency))
    for prior in (old, net):
        require(prior['source_identity'] == data['identity'] and prior['raw_evidence_read_only'],
                'prior audit provenance mismatch')
    stringify = lambda rows: [{k:'' if v is None else str(v) for k,v in r.items()} for r in rows]
    require(stringify(data['features']) == API['rows'](coinit, 'input-timing-features.csv'),
            'historical serial features changed')
    repo = Path(__file__).resolve().parents[5]
    changed = subprocess.run(['git', 'diff', '--name-only', data['identity']['execution_commit'], '--',
        'contrib/satcompute', ':!contrib/satcompute/tests', ':!contrib/satcompute/CMakeLists.txt'],
        cwd=repo, capture_output=True, text=True, check=True).stdout
    require(not changed.strip(), 'production/input differs from executed Stage B: '+changed)
    cases = [(r['input_bytes'], r['input_path']['admitted_rate_bps'] or 0,
        r['input_path']['propagation_ns'], r['input_path']['admissible'],
        r['input_path']['local_delivery']) for r in data['snapshots']]
    estimates = NET['native_estimates'](probe, cases)
    features = [NET['network_feature'](r, f, t) for r,f,t in zip(data['snapshots'], data['features'], estimates)]
    require(stringify(features) == API['rows'](latency, 'input-end-to-end-features.csv'),
            'historical native end-to-end features changed')
    population = COINIT['join_labels'](features, data['trace']['labels'])
    for row, snapshot in zip(population, data['snapshots']):
        require(row['task_id'] == str(snapshot['task_id']), 'snapshot order mismatch')
        row['_steps'] = COINIT['TRACE']['feature_snapshot'](snapshot)['future_first_failure_trajectory']
        row['_rate_bps'] = snapshot['input_path']['admitted_rate_bps']
    require(before == API['evidence_metadata'](protected), 'prior evidence changed during reconstruction')
    return population, data, old, protected, before


def write_tables(output, tables):
    """Keep partial UNKNOWN diagnostics even when their rows have fewer columns."""
    for name, records in tables.items():
        columns = list(dict.fromkeys(k for row in records for k in row))
        API['HISTORY']['write_csv'](output/(name+'.csv'),
            [{k:row.get(k) for k in columns} for row in records])


def write_audit(root, verified, coinit, latency, output, probe):
    API['HISTORY']['output_guard'](output, [root, verified, coinit, latency])
    population, data, prior, protected, before = load_validated(root, verified, coinit, latency, probe)
    API['HISTORY']['output_guard'](output, protected)
    tables, summary = assemble(population)
    if not summary['numeric_or_coverage_blockers']:
        for view, entry in summary['views'].items():
            previous = prior['views'][view]['fixed_screen']['point']
            for key in ('selected_task_count', 'selected_needed', 'planned_staged_network_bytes',
                        'planned_no_need_network_bytes', 'captured_actual_critical_wait_ns'):
                require(entry['old'][key] == previous[key], 'historical selection/metrics changed: '+key)
    summary.update(source_identity=data['identity'], initialization_contract=data['contract'],
        prior_runtime_equivalence=prior['prior_runtime_equivalence'],
        source_features_identical=True, production_source_unchanged=True,
        native_estimator=dict(method='AdmissiblePathEstimate::TransferTimeNs', probe=str(probe),
                              calls=len(population), simulation_started=False),
        sources=dict(raw=str(root), verified=str(verified), coinit=str(coinit), latency=str(latency)),
        raw_evidence_read_only=before == API['evidence_metadata'](protected))
    require(summary['raw_evidence_read_only'], 'historical evidence changed')
    output.mkdir(parents=True, exist_ok=False)
    write_tables(output, tables)
    (output/'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False)+'\n')
    require(before == API['evidence_metadata'](protected), 'historical evidence changed during output')
    return summary
