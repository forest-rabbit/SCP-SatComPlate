"""Offline mean-dependency INPUT value, not a future checkpoint simulator or policy."""
from collections import Counter
import json
import math
from pathlib import Path
import runpy
import subprocess

NET = runpy.run_path(str(Path(__file__).with_name('input_latency_resource_audit.py')))
COINIT, API = NET['COINIT'], NET['API']
require = API['require']
SCORES = ('P_F', 'G_I_net_pot_s', 'G_cp_s')
KNOWN = 'KNOWN_MEAN_DEPENDENCY_APPROXIMATION'
EARLY = 'UNKNOWN_PRE_OR_AT_ESTIMATED_INIT_READY'


def mean_recovery(snapshot):
    """Split the existing V6 mean, using committed cadence and actual-pair resources."""
    r, a = snapshot, snapshot['actual_post_batch_validation']
    require(a is not None and r['input_staging_policy'] == 'deferred', 'actual Deferred inputs required')
    d, n = r['delta_permille'], r['batch_n']
    require(type(d) is int and type(n) is int and 10 <= d <= 100 and n >= 1 and n*d <= 1000,
            'invalid committed Frequency configuration')
    require(all(math.isfinite(a[k]) and a[k] > 0 for k in
                ('backup_bandwidth_bytes_per_s', 'recovery_rate', 'total_work')) and
            all(math.isfinite(a[k]) and a[k] >= 0 for k in
                ('Kvar_bytes', 'cL_ns', 'cR_ns', 'state_transfer_s')), 'invalid mean-recovery inputs')
    for key, top in (('Kvar_bytes','Kvar_bytes'), ('total_work','compute_work_units'),
                     ('recovery_rate','recovery_rate'), ('cL_ns','cL_ns'), ('cR_ns','cR_ns')):
        require(a[key] == r[top], 'actual snapshot resource mismatch: '+key)
    delta = d/1000.0
    tail = a['Kvar_bytes']*(n-1)*delta/(2*a['backup_bandwidth_bytes_per_s'])
    merge = (a['cR_ns']/1e9)*(n-1)/n
    redo = a['total_work']*delta/(2*a['recovery_rate'])
    # Match the Deferred InputCostAdapter ordering; this is a causal estimate, not a receipt.
    initialization = (a['cL_ns']/1e9 + a['state_transfer_s']) + a['cR_ns']/1e9
    require(all(math.isfinite(v) for v in (tail,merge,redo,initialization)), 'mean estimate overflow')
    return dict(delta_permille=d, batch_n=n, delta_fraction=delta,
        Kvar_bytes=a['Kvar_bytes'], work_units=a['total_work'], recovery_rate=a['recovery_rate'],
        mean_tail_transfer_s=tail, mean_tail_merge_s=merge, mean_redo_s=redo,
        dependency_ready_bar_s=tail+merge, R_state_bar_s=tail+merge+redo,
        initialization_estimate_s=initialization,
        estimate_source='COMMITTED_CADENCE_ACTUAL_POST_BATCH_RESOURCES')


def dependency_gain(input_s, staged_remaining_s, dependency_s):
    """Serial catch-up computation cancels; it must never mask INPUT waiting."""
    require(all(math.isfinite(v) and v >= 0 for v in (input_s,staged_remaining_s,dependency_s))
            and staged_remaining_s <= input_s, 'invalid dependency times')
    return max(input_s,dependency_s)-max(staged_remaining_s,dependency_s)


def criticalpath_potential(trajectory, pf, input_s, init_ns, ready_estimate_ns, dependency_s):
    """Retain unknown early mass, including its effect on later unconditional survival."""
    # Reuse the established probability-mass validation and nonnegative-lead contract.
    NET['COINIT']['potential'](trajectory,pf,input_s,init_ns)
    require(ready_estimate_ns >= init_ns >= 0, 'invalid estimated initialization window')
    require(math.isfinite(dependency_s) and dependency_s >= 0, 'invalid dependency mean')
    records, known, unknown, upper = [], [], [], []
    for step in trajectory:
        time, w = step['time_ns'], step['first_failure_mass']
        lead = max(0,time-init_ns)/1e9
        remaining = max(0.0,input_s-lead)
        early = time <= ready_estimate_ns
        gain = None if early else dependency_gain(input_s,remaining,dependency_s)
        contribution = None if early and w > 0 else 0.0 if early else w*gain
        (unknown if early else known).append(w)
        if early:
            # Only a bound under the unchanged INPUT-path hypothesis, not an imputed score.
            upper.append(w*min(input_s,lead))
        records.append(dict(**step, lead_s=lead, input_deferred_s=input_s,
            input_send_remaining_s=remaining, dependency_ready_bar_s=dependency_s,
            delta_dependency_wait_s=gain, weighted_gain_s=contribution,
            contribution_status=EARLY if early else KNOWN))
    unknown_mass, known_mass = math.fsum(unknown), math.fsum(known)
    partial = math.fsum(r['weighted_gain_s'] for r in records if r['weighted_gain_s'] is not None)
    upper_bound = partial+math.fsum(upper)
    require(-1e-12 <= partial <= upper_bound <= pf*input_s+1e-12, 'critical-path gain bound failed')
    return dict(G_cp_s=None if unknown_mass > 0 else partial,
        G_cp_known_contribution_s=partial, G_cp_assumption_lower_s=partial,
        G_cp_assumption_upper_s=upper_bound, known_first_failure_mass=known_mass,
        unknown_first_failure_mass=unknown_mass,
        criticalpath_status=EARLY if unknown_mass > 0 else KNOWN), records


def criticalpath_feature(snapshot, network):
    """Scores read causal snapshots only; labels are joined in the separate evaluator."""
    f = dict(network, **mean_recovery(snapshot))
    init = network['initializing_start_time_ns']
    f.update(estimated_init_ready_time_ns=None if init is None else
        init+math.ceil(f['initialization_estimate_s']*1e9),
        deadline_slack_ns=snapshot['deadline_slack_ns'],
        finish_exclusive=snapshot['predictor']['finish_exclusive'] if snapshot['predictor'] else None,
        first_sample_semantics=snapshot['predictor']['first_sample_semantics'] if snapshot['predictor'] else None,
        G_cp_s=None, G_cp_known_contribution_s=None, G_cp_assumption_lower_s=None,
        G_cp_assumption_upper_s=None, known_first_failure_mass=None, unknown_first_failure_mass=None,
        input_to_total_mean_ratio=None, input_to_dependency_mean_ratio=None,
        criticalpath_status='UNKNOWN_CAUSAL_INPUTS')
    if f['network_bytes'] == 0:
        f['criticalpath_status'] = 'NOT_APPLICABLE_LOCAL'
        return f, []
    input_s = f['T_I_net_s']
    trajectory = COINIT['TRACE']['feature_snapshot'](snapshot)['future_first_failure_trajectory']
    if input_s is None or init is None or trajectory is None:
        return f, []
    f['input_to_total_mean_ratio'] = input_s/f['R_state_bar_s'] if f['R_state_bar_s'] else None
    f['input_to_dependency_mean_ratio'] = input_s/f['dependency_ready_bar_s'] if f['dependency_ready_bar_s'] else None
    value, steps = criticalpath_potential(trajectory, f['P_F'], input_s, init,
                                        f['estimated_init_ready_time_ns'],f['dependency_ready_bar_s'])
    f.update(value)
    return f, [dict(cohort=f['cohort'],task_id=f['task_id'],**s) for s in steps]


def scoped_wait_analysis(members, full, view, scores, scope):
    """A common-score subset is explicit, never relabeled as full candidate coverage."""
    result = NET['wait_analysis'](members,view,scores,scope)
    full_wait = sum(r['critical_wait_ns'] for r in full if r['label'] == 'NEEDED')
    for table in ('sweeps','landmarks','profiles','references'):
        for row in result[table]:
            row.update(population_scope=scope, full_network_candidate_count=len(full),
                full_network_observed_wait_ns=full_wait,
                captured_full_network_wait_recall=row['captured_actual_critical_wait_ns']/full_wait if full_wait else None)
    return result


def assemble(population):
    """Descriptive common-population comparisons with an explicit unknown-coverage audit."""
    require(population and all(r['cohort'] == COINIT['COHORT'] for r in population), 'mixed cohort')
    network = [r for r in population if r['network_bytes'] > 0]
    require(all(r['network_timing_status'] == 'KNOWN_CURRENT_PATH_POTENTIAL_NOT_ACTUAL_GAIN'
                for r in network), 'v3 causal coverage incomplete')
    require(all((r['G_cp_s'] is not None) == (r['criticalpath_status'] == KNOWN)
                for r in network), 'unknown score imputed or known score missing')
    names = ('criticalpath-g-profile-summary','criticalpath-score-sweeps',
        'criticalpath-traffic-wait-pareto','criticalpath-wait-landmarks',
        'criticalpath-profile-composition','criticalpath-same-wait-comparisons',
        'criticalpath-reference-points','criticalpath-unknown-candidates')
    tables = {name:[] for name in names}
    summary = dict(status='OFFLINE_CRITICALPATH_AUDIT_COMPLETE',cohort=COINIT['COHORT'],
        purpose='DEVELOPMENT_CALIBRATION',final_performance_result=False,A0='PASS',A1='INCOMPLETE',
        selected_production_score=None,selected_production_threshold=None,selective_input_enabled=False,
        binary_decision_ready=False,exact_recovery_value='UNKNOWN',
        model='A=mean tail transfer+mean merge; C=mean redo. R=max(INPUT,A)+C; C cancels in DeltaR. G_cp=sum(unconditional first-failure mass*DeltaR).',
        limitations=['Mean dependency only, not E[max] or actual valid future receipt/recovery path.',
            'Unknown pre/at estimated initialization-ready mass is retained, never normalized or zero-filled.',
            'Unknown bounds assume the same current INPUT path; not bounds on actual system catch.',
            'Planned application bytes and captured anchor wait are not actual extra bytes or actual saved catch.',
            'No new queue, contention, future-cadence, relocation or checkpoint projector.',
            'One development run and correlated cut targets do not establish general superiority.'],views={})
    physical = ['P_F','T_I_net_s','R_state_bar_s','dependency_ready_bar_s','mean_redo_s',
                'input_to_total_mean_ratio','input_to_dependency_mean_ratio','G_I_net_pot_s','G_cp_s',
                'unknown_first_failure_mass','deadline_slack_ns']
    for view, systems in COINIT['views'](population):
        full = [r for r in systems if r['network_bytes'] > 0]
        known = [r for r in full if r['criticalpath_status'] == KNOWN]
        unknown = [r for r in full if r['criticalpath_status'] != KNOWN]
        full_result = scoped_wait_analysis(full,full,view,SCORES[:2],'FULL_NETWORK_REFERENCE')
        # This is a separate, disclosed diagnostic population. All three scores use exactly it.
        common_result = scoped_wait_analysis(known,full,view,SCORES,'NETWORK_COMPLETE_GCP') if any(
            r['label'] == 'NEEDED' for r in known) else None
        for result in (full_result,common_result):
            if result is None:
                continue
            tables['criticalpath-score-sweeps'] += result['sweeps']
            tables['criticalpath-traffic-wait-pareto'] += [r for r in result['sweeps'] if r['pareto_within_score']]
            tables['criticalpath-wait-landmarks'] += result['landmarks']
            tables['criticalpath-profile-composition'] += result['profiles']
            tables['criticalpath-reference-points'] += result['references']
        for row in unknown:
            tables['criticalpath-unknown-candidates'].append(dict(view=view,**{k:row[k] for k in
                ('cohort','task_id','profile','label','critical_wait_ns','network_bytes','P_F',
                 'unknown_first_failure_mass','G_cp_known_contribution_s','G_cp_assumption_lower_s',
                 'G_cp_assumption_upper_s','criticalpath_status')}))
        for scope, group in (('FULL_NETWORK',full),('LOCAL_DELIVERY',[r for r in systems if r['network_bytes']==0])):
            distributions = COINIT['stratified_distributions'](group,view,physical,scope)
            for row in distributions:
                subset = [r for r in group if r['profile'] == row['profile'] and
                    (row['label_group'] == 'ALL' or (r['label'] == 'NEEDED') == (row['label_group'] == 'NEEDED'))]
                row.update(candidate_count=len(subset),needed_count=sum(r['label']=='NEEDED' for r in subset),
                    unknown_gcp_count=sum(r['criticalpath_status']==EARLY for r in subset))
            tables['criticalpath-g-profile-summary'] += distributions
        differences = {}
        if common_result:
            for baseline in SCORES[:2]:
                rows = NET['compare_wait_curves'](common_result['by_score'][baseline],
                    common_result['by_score']['G_cp_s'],view,'CP_VS_'+baseline)
                for row in rows:
                    row.update(population_scope='NETWORK_COMPLETE_GCP',candidate_count=len(known),
                               full_network_candidate_count=len(full))
                tables['criticalpath-same-wait-comparisons'] += rows
                differences[baseline] = dict(attainable_wait_targets=len(rows),
                    cp_fewer_bytes=sum(r['delta_planned_bytes']<0 for r in rows),
                    same_bytes=sum(r['delta_planned_bytes']==0 for r in rows),
                    cp_more_bytes=sum(r['delta_planned_bytes']>0 for r in rows))
        zero = [r for r in known if r['G_cp_s']==0]
        behaviors = []
        for profile in sorted({r['profile'] for r in full}):
            group=[r for r in full if r['profile']==profile]
            valid=[r for r in group if r['G_cp_s'] is not None]
            behaviors.append(dict(profile=profile,candidates=len(group),needed=sum(r['label']=='NEEDED' for r in group),
                known=len(valid),unknown=len(group)-len(valid),zero_score=sum(r['G_cp_s']==0 for r in valid),
                zero_score_needed=sum(r['G_cp_s']==0 and r['label']=='NEEDED' for r in valid),
                input_exceeds_dependency_mean=sum(r['T_I_net_s']>r['dependency_ready_bar_s'] for r in group),
                input_exceeds_total_mean=sum(r['T_I_net_s']>r['R_state_bar_s'] for r in group),
                G_net_s=COINIT['distribution']([r['G_I_net_pot_s'] for r in valid]),
                G_cp_s=COINIT['distribution']([r['G_cp_s'] for r in valid])))
        summary['views'][view] = dict(system_candidates=len(systems),network_candidates=len(full),
            local_delivery_count=len(systems)-len(full),network_needed=sum(r['label']=='NEEDED' for r in full),
            comparable_candidates=len(known),comparable_needed=sum(r['label']=='NEEDED' for r in known),
            unknown_task_ids=[r['task_id'] for r in unknown],unknown_labels=dict(Counter(r['label'] for r in unknown)),
            unknown_planned_bytes=sum(r['network_bytes'] for r in unknown),
            full_gcp_sweep_status='UNAVAILABLE_UNKNOWN_CONTRIBUTIONS' if unknown else 'COMPLETE',
            common_sweep_status='COMPLETE' if common_result else 'UNDEFINED_NO_KNOWN_NEEDED_WAIT',
            common_observed_wait_fraction=sum(r['critical_wait_ns'] for r in known if r['label']=='NEEDED')/
                full_result['references'][0]['total_actual_critical_wait_ns'],
            full_references=full_result['references'],
            common_references=common_result['references'] if common_result else [],
            common_landmarks=common_result['landmarks'] if common_result else [],
            comparison_summary=differences,profile_behavior=behaviors,
            zero_score_needed=[dict(task_id=r['task_id'],profile=r['profile'],P_F=r['P_F'],
                critical_wait_ns=r['critical_wait_ns']) for r in zero if r['label']=='NEEDED'],
            zero_score_count=len(zero),zero_score_max_pf=max((r['P_F'] for r in zero),default=None))
    summary['f3_out_of_model'] = [dict(task_id=r['task_id'],label=r['label'],P_F=r['P_F'],
        G_cp_s=r['G_cp_s'],critical_wait_ns=r['critical_wait_ns']) for r in population if r['fault_source']=='F3']
    return tables,summary


def write_audit(root, verified, coinit, latency, output, probe):
    """Reconstruct causal evidence and check v2/v3 identity; write a fresh offline directory."""
    canonical = Path(json.loads((root/'execution.json').read_text())['canonical_reference'])
    protected = [root,verified,coinit,latency,canonical]
    API['HISTORY']['output_guard'](output,protected)
    before = API['evidence_metadata'](protected)
    data = COINIT['collect_audit'](root,verified)
    old = json.loads((latency/'summary.json').read_text())
    require(old['source_identity']==data['identity'] and old['raw_evidence_read_only'], 'v3 identity mismatch')
    def csv_identity(features, folder, name):
        require([{k:'' if v is None else str(v) for k,v in r.items()} for r in features]==API['rows'](folder,name),
                'historical causal features changed: '+name)
    csv_identity(data['features'],coinit,'input-timing-features.csv')
    repo = Path(__file__).resolve().parents[5]
    names = ['traffic/network-transfer-engine.cc',
        'protection/policy/compfrr/frequency/compfrr-frequency-policy.cc',
        'protection/policy/compfrr/input/input-cost-adapter.cc',
        'protection/runtime/checkpoint-recovery-estimate.h']
    for name in names:
        relative='contrib/satcompute/'+name
        executed=subprocess.run(['git','show',data['identity']['execution_commit']+':'+relative],cwd=repo,
                                check=True,capture_output=True,text=True).stdout
        require((repo/relative).read_text()==executed, 'reviewed execution source changed: '+relative)
    cases = [(r['input_bytes'],r['input_path']['admitted_rate_bps'] or 0,r['input_path']['propagation_ns'],
              r['input_path']['admissible'],r['input_path']['local_delivery']) for r in data['snapshots']]
    times = NET['native_estimates'](probe,cases)
    network = [NET['network_feature'](r,f,t) for r,f,t in zip(data['snapshots'],data['features'],times)]
    csv_identity(network,latency,'input-end-to-end-features.csv')
    features, steps = [], []
    for snapshot, base in zip(data['snapshots'],network):
        f,s=criticalpath_feature(snapshot,base)
        features.append(f); steps.extend(s)
    tables, summary=assemble(COINIT['join_labels'](features,data['trace']['labels']))
    tables['criticalpath-g-features']=features
    tables['criticalpath-future-steps']=steps
    summary.update(source_identity=data['identity'],initialization_contract=data['contract'],
        prior_runtime_equivalence=old['prior_runtime_equivalence'],v2_v3_causal_features_identical=True,
        production_source_matches_execution=names,simulation_started=False,
        native_estimator=dict(method='AdmissiblePathEstimate::TransferTimeNs',probe=str(probe),calls=len(times)),
        sources=dict(raw=str(root),verified=str(verified),coinit=str(coinit),latency=str(latency)),
        raw_evidence_read_only=before==API['evidence_metadata'](protected))
    require(summary['raw_evidence_read_only'],'historical evidence changed')
    output.mkdir(parents=True,exist_ok=False)
    for name,records in tables.items():
        API['HISTORY']['write_csv'](output/(name+'.csv'),records)
    (output/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
    require(before==API['evidence_metadata'](protected),'historical evidence changed during output')
    return summary
