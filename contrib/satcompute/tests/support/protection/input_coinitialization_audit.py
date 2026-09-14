"""Offline fixed-rate INPUT potential; not a checkpoint/recovery predictor or policy."""
from collections import Counter
import json
import math
from pathlib import Path
import runpy
import subprocess

TRACE = runpy.run_path(str(Path(__file__).with_name('input_start_trace_audit.py')))
API = TRACE['API']
require, rows, unique = (API[k] for k in ('require', 'rows', 'unique'))
COHORT = TRACE['COHORT']
SCORES = ('P_F', 'U_pot', 'G_I_pot_s', 'M_pot')
LEVELS = (.8, .9, 1.0)
REJECT = 'DEFINITELY_REJECT_UNDER_THIS_BREAK_EVEN'
POSSIBLE = 'POSSIBLE_CANDIDATE'


def initializing_time(snapshot_ns, physical_ns, admission_ns, initializing_ns,
                      *, synchronous_contract=False, decision_known_init_ns=None):
    """Actual delayed admission is retrospective unless its time was decision-known."""
    require(0 <= snapshot_ns <= physical_ns <= admission_ns and
            physical_ns <= initializing_ns <= admission_ns, 'invalid initialization ordering')
    same = snapshot_ns == physical_ns == admission_ns == initializing_ns
    if decision_known_init_ns is not None:
        require(decision_known_init_ns >= snapshot_ns and decision_known_init_ns == initializing_ns,
                'decision-known schedule conflicts with actual initialization')
        causal, provenance = decision_known_init_ns, 'DECISION_KNOWN_SCHEDULE'
    elif synchronous_contract and same:
        causal, provenance = initializing_ns, 'SYNCHRONOUS_CODE_AND_THREE_ADMISSION_LEDGERS'
    else:
        causal, provenance = None, 'RETROSPECTIVE_ONLY_NO_CAUSAL_SCHEDULE'
    return dict(snapshot_time_ns=snapshot_ns, physical_start_time_ns=physical_ns,
        admission_time_ns=admission_ns, initializing_start_time_ns=initializing_ns,
        delta_ns=initializing_ns-snapshot_ns, causal_initializing_start_time_ns=causal,
        provenance=provenance, status='PASS' if causal is not None else 'UNKNOWN')


def potential(trajectory, pf, input_s, init_ns):
    """Use all unconditional first-failure mass, including faults before initialization."""
    require(math.isfinite(input_s) and input_s > 0, 'positive network serialization required')
    require(math.isfinite(pf) and 0 <= pf <= 1, 'invalid P_F')
    require(all(math.isfinite(s['first_failure_mass']) and 0 <= s['first_failure_mass'] <= 1
                for s in trajectory), 'invalid first-failure mass')
    require(math.isclose(math.fsum(s['first_failure_mass'] for s in trajectory), pf, abs_tol=1e-12),
            'incomplete unconditional first-failure mass')
    full, partial, zero, gain, loss = [], [], [], [], []
    for step in trajectory:
        lead = max(0, step['time_ns']-init_ns)/1e9
        w = step['first_failure_mass']
        (zero if lead == 0 else full if lead >= input_s else partial).append(w)
        gain.append(w * min(1.0, lead/input_s))
        loss.append(w * (1-min(1.0, lead/input_s)))
    # Clamp only roundoff against the analytically proven bound, never rescale weights.
    # Algebraically sum(w*a) = P_F - sum(w*(1-a)). This preserves the exact
    # U=P_F identity when all positive mass is fully ready, without epsilon tie bins.
    u = pf-math.fsum(loss) if any(gain) else 0.0
    require(-1e-12 <= u <= pf+1e-12, 'potential outside probability bound')
    u = min(pf, max(0.0, u))
    margin = u-(1-pf)
    return dict(U_pot=u, G_I_pot_s=input_s*u, M_pot=margin,
        P_I_full_pot=math.fsum(full), P_I_partial_pot=math.fsum(partial),
        P_I_zero_lead_pot=math.fsum(zero),
        break_even_class=POSSIBLE if margin > 0 else REJECT)


def timing_feature(snapshot, time):
    """Causal fields only. Outcomes/profile labels never participate in score calculation."""
    f = TRACE['feature_snapshot'](snapshot)
    r, actual = snapshot, snapshot['actual_post_batch_validation']
    require(r['input_bytes'] > 0 and actual is not None, 'missing actual-pair physical inputs')
    init = time['causal_initializing_start_time_ns']
    trajectory = f['future_first_failure_trajectory']
    local = f['path_observation'] == 'LOCAL'
    rate = None if local else r['input_path']['admitted_rate_bps']
    backup = actual['backup_bandwidth_bytes_per_s']
    require(backup > 0 and math.isfinite(backup), 'invalid actual backup bandwidth')
    if rate is not None:
        require(math.isfinite(rate) and math.isclose(actual['input_bandwidth_bytes_per_s'], rate/8),
                'actual INPUT bandwidth differs from post-batch path')
    feature = dict(cohort=COHORT, task_id=f['task_id'], profile=f['profile'],
        start_trigger=r['start_trigger'], local_node=f['local_node'], remote_node=f['remote_node'],
        snapshot_time_ns=f['start_time_ns'], initializing_start_time_ns=init,
        initialization_time_status=time['status'], path_observation=f['path_observation'],
        input_bytes=f['input_bytes'], network_bytes=f['planned_network_input_bytes'],
        T_I_s=f['input_serialization_s'], input_bandwidth_bytes_per_s=None if rate is None else rate/8,
        backup_bandwidth_bytes_per_s=backup, propagation_ns=r['input_path']['propagation_ns'],
        Kvar_over_input=r['Kvar_bytes']/r['input_bytes'], cL_ns=r['cL_ns'], cR_ns=r['cR_ns'],
        initial_variable_state_bytes=r['initial_variable_state_bytes'],
        initial_variable_state_over_input=r['initial_variable_state_bytes']/r['input_bytes'],
        full_state_serialization_reference_s=r['Kvar_bytes']/backup,
        earliest_check_minus_init_s=(trajectory[0]['time_ns']-init)/1e9 if trajectory and init is not None else None,
        remaining_compute_s=f['remaining_compute_s'], P_F=f['P_F'],
        U_pot=None, G_I_pot_s=0.0 if local else None, M_pot=None,
        P_I_full_pot=None, P_I_partial_pot=None, P_I_zero_lead_pot=None,
        break_even_class='NOT_APPLICABLE_LOCAL' if local else 'UNKNOWN',
        timing_status='NOT_APPLICABLE_LOCAL' if local else 'UNKNOWN',
        U_exact=None, G_I_exact_s=None, P_Iimpact=None, P_Iddl=None,
        checkpoint_state_tail_forecast=None)
    if not local and init is not None and trajectory is not None and f['input_serialization_s'] is not None:
        feature.update(potential(trajectory, f['P_F'], f['input_serialization_s'], init))
        feature['timing_status'] = 'KNOWN_FIXED_RATE_POTENTIAL_ONLY'
    return feature


def distribution(values):
    """Linear-interpolated sample quantiles; empty strata are undefined, not zero."""
    data = sorted(v for v in values if v is not None)
    require(all(math.isfinite(v) for v in data), 'nonfinite distribution input')
    result = dict(count=len(data), small_sample=len(data) < 10)
    for key, q in (('min', 0), ('P10', .1), ('P25', .25), ('P50', .5),
                   ('P75', .75), ('P90', .9), ('max', 1)):
        position = (len(data)-1)*q
        lo = max(0, math.floor(position))
        result[key] = (data[lo] + (data[min(lo+1, len(data)-1)]-data[lo])*(position-lo)) if data else None
    return result


def views(population):
    return (('ALL_FAULT', population),
            ('F1_F2_PREDICTABLE', [r for r in population if r['fault_source'] != 'F3']))


def join_labels(features, labels):
    """Attach evaluation outcomes only after the causal feature/score builder finishes."""
    require(all(r['cohort'] == COHORT for r in features+labels), 'cohorts must not be pooled')
    f, lab = unique(features, 'task_id'), unique(labels, 'task_id')
    require(f.keys() == lab.keys(), 'feature/label population mismatch')
    result = []
    for tid, feature in f.items():
        label = lab[tid]
        require(label['label'] in ('NEEDED', 'FAULT_NONCRITICAL', 'NO_FAULT'), 'A0 incomplete label')
        wait = label['input_critical_wait_ns']
        require(label['label'] == 'NO_FAULT' or wait is not None, 'missing fault waiting outcome')
        result.append(dict(feature, label=label['label'], fault_source=label['historical_fault_source'],
                           critical_wait_ns=wait if wait is not None else 0))
    return result


def operating_point(population, selected, view, name, score=None, cut=None, scope='NETWORK_COMMON'):
    row = API['pf_operating_point'](population, selected, view, name)
    del row['pf_cut']
    return dict(cohort=COHORT, population_scope=scope, score=score, score_cut=cut, **row)


def ranking(population, view, score):
    require(all(r['network_bytes'] > 0 and r.get(score) is not None and math.isfinite(r[score])
                for r in population), 'common network score coverage incomplete')
    # Negative M cuts are essential: fixed M > 0 is a separate theoretical screen.
    return [operating_point(population, [r for r in population if r[score] >= cut],
                            view, 'UNIQUE_SCORE_CUT', score, cut)
            for cut in sorted({r[score] for r in population}, reverse=True)]


def fixed_screen(population, view):
    selected = [r for r in population if r['M_pot'] > 0]
    require(all(r['P_F'] > .5 for r in selected), 'break-even necessary condition violated')
    point = operating_point(population, selected, view, 'FIXED_M_STRICTLY_POSITIVE', 'M_pot', 0)
    bound = operating_point(population, [r for r in population if r['P_F'] > .5],
                             view, 'NECESSARY_PF_STRICTLY_ABOVE_HALF', 'P_F', .5)
    return dict(point=point, necessary_condition_upper_bound=bound,
        unreachable_recall_landmarks=[x for x in LEVELS if point['recall'] is not None and x > point['recall']],
        interpretation='Only this equal-byte fixed-rate theoretical screen; not a safe real-recovery rejection.')


def stratified_distributions(members, view, fields, scope):
    result = []
    for profile in sorted({r['profile'] for r in members}):
        group = [r for r in members if r['profile'] == profile]
        for label, subset in (('ALL', group), ('NEEDED', [r for r in group if r['label'] == 'NEEDED']),
                              ('NO_NEED', [r for r in group if r['label'] != 'NEEDED'])):
            for field in fields:
                result.append(dict(cohort=COHORT, view=view, population_scope=scope,
                    profile=profile, label_group=label, field=field,
                    **distribution([r[field] for r in subset])))
    return result


def profile_points(members, selected, view, name, score, cut, scope):
    result = []
    for profile in sorted({r['profile'] for r in members}):
        group = [r for r in members if r['profile'] == profile]
        chosen = [r for r in selected if r['profile'] == profile]
        result.append(dict(profile=profile, selection_rate=len(chosen)/len(group),
            selected_profile_fraction=len(chosen)/len(selected) if selected else None,
            **operating_point(group, chosen, view, name, score, cut, scope)))
    return result


def analyze_population(population, full_pf_summary):
    """Descriptive sweeps/strata; no fit, optimizer, per-profile cut or policy choice."""
    require(population and all(r['cohort'] == COHORT for r in population), 'wrong development cohort')
    network = [r for r in population if r['network_bytes'] > 0]
    require(all(r['timing_status'] == 'KNOWN_FIXED_RATE_POTENTIAL_ONLY' and
                all(r[k] is not None and math.isfinite(r[k]) for k in SCORES) for r in network),
            'common four-score network coverage incomplete; do not select a known-only subset')
    tables = {name: [] for name in ('profile-summary', 'profile-pf-distributions',
        'profile-selection-landmarks', 'input-timing-profile-summary',
        'local-break-even-profile-summary', 'multi-score-sweeps', 'multi-score-landmarks',
        'multi-score-profile-composition', 'physical-scale-profile-summary')}
    summary = dict(status='OFFLINE_COINITIALIZATION_AUDIT_PASS', cohort=COHORT,
        purpose='DEVELOPMENT_CALIBRATION', final_performance_result=False,
        selected_production_threshold=None, selected_production_score=None, selective_input_enabled=False,
        A0='PASS', A1='INCOMPLETE', exact_recovery_value='UNKNOWN',
        advanced_recovery_ranking='SKIPPED_A1_INCOMPLETE', views={},
        quantiles='Linear interpolation at (count-1)*p; count<10 flagged, no inference of population quantiles.',
        interpretation='Fixed-current-rate serialization potential only; no propagation/contention/state-tail forecast. Captured anchor wait is not actual saved catch; planned bytes are not actual sent or wasted bytes.')
    for view, members in views(population):
        net = [r for r in members if r['network_bytes'] > 0]
        local = [r for r in members if r['network_bytes'] == 0]
        expected = full_pf_summary['views'][view]
        require(len(members) == expected['candidate_count'], 'full Stage B P_F cohort changed')
        for profile in sorted({r['profile'] for r in members}):
            group = [r for r in members if r['profile'] == profile]
            counts = Counter(r['label'] for r in group)
            byte_sum = sum(r['network_bytes'] for r in group)
            needed_bytes = sum(r['network_bytes'] for r in group if r['label'] == 'NEEDED')
            tables['profile-summary'].append(dict(cohort=COHORT, view=view, profile=profile,
                candidate_count=len(group), network_count=sum(r['network_bytes'] > 0 for r in group),
                local_delivery_count=sum(r['network_bytes'] == 0 for r in group),
                needed_count=counts['NEEDED'], fault_noncritical_count=counts['FAULT_NONCRITICAL'],
                no_fault_count=counts['NO_FAULT'], needed_rate=counts['NEEDED']/len(group),
                planned_input_network_bytes=byte_sum, planned_needed_network_bytes=needed_bytes,
                planned_no_need_network_bytes=byte_sum-needed_bytes))
        tables['profile-pf-distributions'] += stratified_distributions(members, view, ['P_F'], 'SYSTEM')
        # The approved Stage B full-population P_F landmarks are reused, not retuned per profile.
        for name, cut in [('ALL_STAGE', None)]+[(f'RECALL_{level:g}', next(
            x['point']['pf_cut'] for x in expected['recall_landmarks'] if x['target_recall'] == level))
            for level in LEVELS]:
            selected = members if cut is None else [r for r in members if r['P_F'] >= cut]
            tables['profile-selection-landmarks'] += profile_points(members, selected, view, name,
                                                                    'P_F', cut, 'SYSTEM')
        tables['input-timing-profile-summary'] += stratified_distributions(net, view,
            ['U_pot','G_I_pot_s','M_pot','P_I_full_pot','P_I_partial_pot','P_I_zero_lead_pot'], 'NETWORK_COMMON')
        physical = ['T_I_s','input_bytes','input_bandwidth_bytes_per_s','backup_bandwidth_bytes_per_s',
                    'Kvar_over_input','cR_ns','initial_variable_state_over_input',
                    'earliest_check_minus_init_s','full_state_serialization_reference_s','propagation_ns']
        for scope, group in (('NETWORK_COMMON', net), ('LOCAL_DELIVERY', local)):
            tables['physical-scale-profile-summary'] += stratified_distributions(group, view, physical, scope)
        screen = fixed_screen(net, view)
        for profile in sorted({r['profile'] for r in net}):
            group = [r for r in net if r['profile'] == profile]
            accepted = [r for r in group if r['M_pot'] > 0]
            point = operating_point(group, accepted, view, 'FIXED_M_STRICTLY_POSITIVE', 'M_pot', 0)
            tables['local-break-even-profile-summary'].append(dict(profile=profile,
                possible_count=len(accepted), reject_count=len(group)-len(accepted),
                possible_rate=len(accepted)/len(group),
                rejected_needed_count=sum(r['label'] == 'NEEDED' and r['M_pot'] <= 0 for r in group), **point))
        references = [operating_point(net, chosen, view, name, 'REFERENCE') for name, chosen in (
            ('ALL_STAGE', net), ('NONE_STAGE', []), ('ORACLE_NEEDED', [r for r in net if r['label'] == 'NEEDED']))]
        tables['multi-score-landmarks'] += [dict(r, target_recall=None, status='REFERENCE') for r in references]
        score_summary = {}
        for score in SCORES:
            points = ranking(net, view, score)
            require(points[-1]['recall'] in (None, 1.0), 'full unique-score sweep cannot lose NEEDED')
            tables['multi-score-sweeps'] += points
            marks = [dict(next(p for p in points if p['recall'] is not None and p['recall'] >= level),
                          target_recall=level, status='REACHABLE') for level in LEVELS]
            tables['multi-score-landmarks'] += marks
            for p in points:
                chosen = [r for r in net if r[score] >= p['score_cut']]
                tables['multi-score-profile-composition'] += profile_points(net, chosen, view,
                    'UNIQUE_SCORE_CUT', score, p['score_cut'], 'NETWORK_COMMON')
            score_summary[score] = dict(unique_cuts=len(points), recall_landmarks=marks)
        timing = []
        for profile in sorted({r['profile'] for r in net}):
            group = [r for r in net if r['profile'] == profile]
            timing.append(dict(profile=profile, network_count=len(group),
                ready_by_first_check_count=sum(r['earliest_check_minus_init_s'] is not None and
                    r['earliest_check_minus_init_s'] >= r['T_I_s'] for r in group),
                u_equals_pf_count=sum(math.isclose(r['U_pot'], r['P_F'], rel_tol=0, abs_tol=1e-12) for r in group),
                pf_minus_u=distribution([r['P_F']-r['U_pot'] for r in group]),
                G_I_pot_s=distribution([r['G_I_pot_s'] for r in group])))
        summary['views'][view] = dict(system_count=len(members), network_count=len(net),
            system_needed=sum(r['label'] == 'NEEDED' for r in members),
            network_needed=sum(r['label'] == 'NEEDED' for r in net),
            local_delivery_count=len(local), local_labels=dict(Counter(r['label'] for r in local)),
            references=references, scores=score_summary, fixed_screen=screen, timing_information=timing)
    tables['local-break-even-screen'] = [dict(cohort=COHORT, task_id=r['task_id'], profile=r['profile'],
        P_F=r['P_F'], U_pot=r['U_pot'], M_pot=r['M_pot'], break_even_class=r['break_even_class'],
        historical_label=r['label'], historical_fault_source=r['fault_source']) for r in network]
    summary['f3_out_of_model'] = [dict(task_id=r['task_id'], P_F=r['P_F'], label=r['label'],
        input_critical_wait_ns=r['critical_wait_ns']) for r in population if r['fault_source'] == 'F3']
    return tables, summary


def source_contract(repo, execution_commit):
    """Read the executed source, not a newer manager's contract. Git IDs are provenance, not checksums."""
    names = ('protection/policy/compfrr/compfrr-controller.cc',
             'protection/mechanism/checkpoint/checkpoint-manager.cc')
    sources = []
    for name in names:
        relative = 'contrib/satcompute/'+name
        executed = subprocess.run(['git','show',execution_commit+':'+relative], cwd=repo,
                                  check=True, capture_output=True, text=True).stdout
        require((repo/relative).read_text() == executed, 'initialization implementation changed; audit separately')
        sources.append(executed)
    controller, manager = sources
    a = controller.index('CaptureInputStart(row, time)')
    b = controller.index('m_manager.Execute(context', a)
    c = controller.index('if (inventory && inventory->active) m_inputStartRecords.push_back', b)
    require(a < b < c, 'snapshot/admission sequence changed')
    for token in ('context.nowNs != Now()', 'layout.Floor(actual), Now(), rate',
                  'summary.startNs = now', 'Later(state, Now() + state.summary.localCostNs',
                  'state.initial == 0 ? 0'):
        require(token in manager, 'reviewed synchronous initialization/cL contract missing: '+token)
    return dict(status='PASS', execution_commit=execution_commit, files=list(names),
        contract='Synchronous Execute creates INITIALIZING state at Now(); active inventory and ACCEPTED confirm admission, START log alone does not. Nonzero INIT_STATE is generated after cL, then transferred/merged; zero initial state has no UDP.')


def collect_audit(root, verified):
    """Reusable read-only source/admission/causal-feature join; no output or simulation."""
    execution = json.loads((root/'execution.json').read_text())
    canonical = Path(execution['canonical_reference'])
    identity = TRACE['execution_identity'](execution, json.loads((root/'execution-result.json').read_text()),
        json.loads((root/'run-summary.json').read_text()), json.loads((canonical/'execution.json').read_text()))
    require(identity == json.loads((verified/'source-identity.json').read_text()), 'verified Stage B source differs')
    repo = Path(__file__).resolve().parents[5]
    contract = source_contract(repo, identity['execution_commit'])
    trace = TRACE['audit_trace'](root)
    require(trace['reduced']['summary'] == json.loads((verified/'pf-summary.json').read_text()),
            'reconstructed Stage B P_F or labels differ from verified evidence')
    document = json.loads((root/'input-start-snapshots.json').read_text())
    events = unique([r for r in rows(root,'protection-events.csv') if r['event'] == 'START'], 'task_id')
    admissions = unique([r for r in rows(root,'placement-selections.csv') if r['actual_admission'] == 'ACCEPTED'], 'task_id')
    summaries = unique(rows(root,'protection-task-summary.csv'), 'task_id')
    times, features = [], []
    for r in document['candidates']:
        tid = str(r['task_id'])
        require(tid in events and tid in admissions and tid in summaries, 'missing physical/admission ledger')
        for ledger in (events[tid], admissions[tid], summaries[tid]):
            require(all(int(ledger[k]) == r[k] for k in ('local_node','remote_node')), 'actual pair ledger mismatch')
        t = initializing_time(r['start_time_ns'], int(events[tid]['time_ns']),
            int(admissions[tid]['time_ns']), int(summaries[tid]['start_time_ns']), synchronous_contract=True)
        times.append(dict(cohort=COHORT, task_id=tid, **t))
        features.append(timing_feature(r, t))
    return dict(identity=identity, contract=contract, trace=trace, snapshots=document['candidates'],
                times=times, features=features)


def write_audit(root, verified, output):
    """Rejoin existing evidence, write only a fresh audit directory, never launch a simulation."""
    execution = json.loads((root/'execution.json').read_text())
    canonical = Path(execution['canonical_reference'])
    protected = [root, verified, canonical]
    API['HISTORY']['output_guard'](output, protected)
    before = API['evidence_metadata'](protected)
    data = collect_audit(root, verified)
    identity, contract, trace, times, features = (data[k] for k in ('identity','contract','trace','times','features'))
    population = join_labels(features, trace['labels'])
    tables, summary = analyze_population(population, trace['reduced']['summary'])
    tables['initializing-time-audit'] = times
    tables['input-timing-features'] = features
    summary.update(source_identity=identity, source_contract=contract,
        sources=dict(raw=str(root), verified_stage_b=str(verified), canonical=str(canonical)),
        initialization=dict(candidate_count=len(times), status_counts=dict(Counter(t['status'] for t in times)),
            same_ns_count=sum(t['delta_ns'] == 0 for t in times),
            initial_zero_state_count=sum(r['initial_variable_state_bytes'] == 0 for r in features),
            initial_nonzero_state_count=sum(r['initial_variable_state_bytes'] > 0 for r in features)),
        prior_runtime_equivalence=json.loads((verified/'summary.json').read_text())['runtime_equivalence'],
        raw_evidence_read_only=before == API['evidence_metadata'](protected))
    require(summary['raw_evidence_read_only'], 'raw evidence modified')
    output.mkdir(parents=True, exist_ok=False)
    for name, records in tables.items():
        API['HISTORY']['write_csv'](output/(name+'.csv'), records)
    (output/'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False)+'\n')
    require(before == API['evidence_metadata'](protected), 'raw evidence modified during output')
    return summary
