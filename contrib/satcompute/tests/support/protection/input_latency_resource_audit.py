"""Descriptive traffic/wait frontiers; causal scores never read historical outcomes."""
import json
import math
from pathlib import Path
import runpy
import subprocess

COINIT = runpy.run_path(str(Path(__file__).with_name('input_coinitialization_audit.py')))
API = COINIT['API']
require = API['require']
WAIT_LEVELS = (80, 90, 95, 99, 100)
SERIAL_SCORES = ('P_F', 'U_pot', 'G_I_pot_s')
NETWORK_SCORES = ('P_F', 'G_I_pot_s', 'G_I_net_pot_s')


def find_probe(repo):
    matches = list((repo/'build/contrib/satcompute/tests').glob('*-satcompute-input-timing-audit-*'))
    require(len(matches) == 1 and matches[0].is_file(),
            'build the pure satcompute-input-timing-audit target first (no simulation)')
    return matches[0]


def native_estimates(probe, cases):
    """Use the existing C++ method for every estimate; no Python network-model fallback."""
    request = []
    for i, (size, rate, prop, admissible, local) in enumerate(cases):
        require(isinstance(size, int) and 0 <= size < 2**64 and isinstance(rate, int) and 0 <= rate < 2**64
                and isinstance(prop, int) and -2**63 <= prop < 2**63
                and admissible in (0,1) and local in (0,1), 'invalid native estimator input')
        request.append(f'{i}\t{size}\t{rate}\t{prop}\t{int(admissible)}\t{int(local)}\n')
    result = subprocess.run([str(probe)], input=''.join(request), text=True, capture_output=True, check=True)
    output = [line.split('\t') for line in result.stdout.splitlines()]
    require(len(output) == len(cases) and all(len(row) == 2 and row[0] == str(i)
                for i,row in enumerate(output)), 'native estimator result identity mismatch')
    return [None if row[1] == 'UNKNOWN' else int(row[1]) for row in output]


def network_feature(snapshot, base, transfer_ns):
    """Extend frozen causal features, never use fault outcomes or a final recovery target."""
    path = snapshot['input_path']
    local = base['path_observation'] == 'LOCAL'
    f = dict(base, T_I_ser_s=base['T_I_s'], T_I_prop_ns=path['propagation_ns'],
        T_I_net_ns=None if local else transfer_ns, T_I_net_s=None if local or transfer_ns is None else transfer_ns/1e9,
        serialization_rounded_ns=None if local or transfer_ns is None else transfer_ns-path['propagation_ns'],
        local_delivery_boundary_ns=transfer_ns if local else None,
        U_net_pot=None, G_I_net_pot_s=0.0 if local else None,
        P_I_net_full_pot=None, P_I_net_partial_pot=None, P_I_net_zero_lead_pot=None,
        network_timing_status='NOT_APPLICABLE_LOCAL' if local else 'UNKNOWN')
    if local:
        require(transfer_ns == 1 and base['network_bytes'] == 0, 'LocalDelivery is a causal boundary, not UDP')
        return f
    causal = COINIT['TRACE']['feature_snapshot'](snapshot)
    trajectory, init = causal['future_first_failure_trajectory'], base['initializing_start_time_ns']
    if not path['admissible'] or transfer_ns is None or init is None or trajectory is None:
        return f
    require(transfer_ns > 0 and transfer_ns >= path['propagation_ns'] >= 0, 'invalid native completion estimate')
    value = COINIT['potential'](trajectory, base['P_F'], transfer_ns/1e9, init)
    f.update(U_net_pot=value['U_pot'], G_I_net_pot_s=value['G_I_pot_s'],
        P_I_net_full_pot=value['P_I_full_pot'], P_I_net_partial_pot=value['P_I_partial_pot'],
        P_I_net_zero_lead_pot=value['P_I_zero_lead_pot'],
        network_timing_status='KNOWN_CURRENT_PATH_POTENTIAL_NOT_ACTUAL_GAIN')
    return f


def frontier(points):
    """Keep attainable nondominated points, not an interpolated/convexified envelope."""
    best_at_bytes = {}
    for row in points:
        size, wait = row['planned_staged_network_bytes'], row['captured_actual_critical_wait_ns']
        best_at_bytes[size] = max(best_at_bytes.get(size, -1), wait)
    frontier_pairs, best_wait = set(), -1
    for size, wait in sorted(best_at_bytes.items()):
        if wait > best_wait:
            frontier_pairs.add((size, wait))
            best_wait = wait
    # Keep distinct score/cut identities with the same objective pair.
    return [(r['planned_staged_network_bytes'], r['captured_actual_critical_wait_ns']) in frontier_pairs
            for r in points]


def first_cover(points, target_wait_ns):
    """First actual cut meeting an integer waiting target; never split a tied task group."""
    return next((p for p in points if p['captured_actual_critical_wait_ns'] >= target_wait_ns), None)


def wait_analysis(members, view, scores, phase):
    """Outcomes evaluate full score-ranked sets only; they never select arbitrary tasks."""
    sweeps, landmarks, profiles, by_score = [], [], [], {}
    for score in scores:
        points = COINIT['ranking'](members, view, score)
        points.insert(0, COINIT['operating_point'](members, [], view, 'NONE_STAGE', score))
        flags = frontier(points)
        by_score[score] = points
        for p, flag in zip(points, flags):
            row = dict(p, phase=phase, pareto_within_score=flag)
            sweeps.append(row)
            chosen = [] if p['score_cut'] is None else [r for r in members if r[score] >= p['score_cut']]
            profiles.extend(dict(r, phase=phase) for r in COINIT['profile_points'](
                members, chosen, view, p['operating_point'], score, p['score_cut'], 'NETWORK_COMMON'))
        total = points[-1]['total_actual_critical_wait_ns']
        require(total > 0, 'waiting coverage undefined without observed NEEDED wait')
        for level in WAIT_LEVELS:
            target = (total*level + 99)//100
            point = first_cover(points, target)
            require(point is not None, 'complete score sweep failed full wait coverage')
            landmarks.append(dict(point, phase=phase, target_wait_coverage=level/100,
                                  required_wait_ns=target, status='REACHABLE'))
    for p, flag in zip(sweeps, frontier(sweeps)):
        p['pareto_among_compared_scores'] = flag
    references = [dict(COINIT['operating_point'](members, selected, view, name, 'REFERENCE'), phase=phase)
                  for name, selected in (('ALL_STAGE', members), ('NONE_STAGE', []),
                    ('ORACLE_NEEDED', [r for r in members if r['label'] == 'NEEDED']))]
    return dict(sweeps=sweeps, landmarks=landmarks, profiles=profiles, by_score=by_score, references=references)


def compare_wait_curves(left, right, view, comparison):
    """Compare all positive attainable waiting targets; counts are descriptive, not p-values."""
    targets = sorted({p['captured_actual_critical_wait_ns'] for p in left+right
                      if p['captured_actual_critical_wait_ns'] > 0})
    result = []
    for target in targets:
        a, b = first_cover(left, target), first_cover(right, target)
        require(a is not None and b is not None, 'incomparable waiting coverage')
        ab, bb = a['planned_staged_network_bytes'], b['planned_staged_network_bytes']
        result.append(dict(cohort=COINIT['COHORT'], view=view, comparison=comparison,
            target_captured_wait_ns=target, target_wait_coverage=target/a['total_actual_critical_wait_ns'],
            baseline_score=a['score'], candidate_score=b['score'], baseline_cut=a['score_cut'], candidate_cut=b['score_cut'],
            baseline_planned_bytes=ab, candidate_planned_bytes=bb, delta_planned_bytes=bb-ab,
            relative_bytes_saved=(ab-bb)/ab if ab else None,
            baseline_actual_wait_coverage=a['captured_wait_recall'], candidate_actual_wait_coverage=b['captured_wait_recall'],
            baseline_needed_recall=a['recall'], candidate_needed_recall=b['recall'],
            baseline_selected=a['selected_task_count'], candidate_selected=b['selected_task_count']))
    return result


def assemble(population):
    """Freeze both populations, then report differences without selecting a production rule."""
    require(population and all(r['cohort'] == COINIT['COHORT'] for r in population), 'mixed cohort')
    network = [r for r in population if r['network_bytes'] > 0]
    require(all(r['network_timing_status'] == 'KNOWN_CURRENT_PATH_POTENTIAL_NOT_ACTUAL_GAIN'
                for r in network), 'end-to-end coverage incomplete; do not drop unknown candidates')
    tables = {k:[] for k in ('traffic-wait-pareto','traffic-wait-landmarks','traffic-wait-profile-composition',
        'network-timing-score-sweeps','network-timing-pareto','network-timing-landmarks',
        'input-end-to-end-profile-summary','same-wait-comparisons','reference-points')}
    summary = dict(status='LATENCY_RESOURCE_OFFLINE_AUDIT_PASS', cohort=COINIT['COHORT'],
        purpose='DEVELOPMENT_CALIBRATION', final_performance_result=False, A0='PASS', A1='INCOMPLETE',
        selected_production_score=None, selected_production_threshold=None, selective_input_enabled=False,
        interpretation='Planned staged application bytes versus captured observed INPUT-critical wait. Neither actual extra traffic nor actual saved catch. Current-path readiness potential only; no queues, contention or future state/tail forecast.',
        curve_contract='All unique cuts with ties intact; nondominated attainable points only. No interpolation, label-driven task picking or production budget optimizer.',
        gates='Report G_ser vs P_F separately from G_net vs G_ser; single-run descriptive evidence, no universal or statistical PASS claim.',
        binary_decision_ready=False,
        missing_for_online_decision=['causal valid checkpoint/other dependency readiness and recovery feasibility',
            'a reviewed operational definition of sufficiently similar benefit, not chosen in this audit'],
        views={})
    comparisons = (('P_F','G_I_pot_s','SERIAL_VS_PF'),
                   ('G_I_pot_s','G_I_net_pot_s','NET_VS_SERIAL'),
                   ('P_F','G_I_net_pot_s','NET_VS_PF'))
    for view, members in COINIT['views'](network):
        serial = wait_analysis(members,view,SERIAL_SCORES,'SERIAL')
        net = wait_analysis(members,view,NETWORK_SCORES,'NETWORK')
        tables['traffic-wait-pareto'] += serial['sweeps']
        tables['traffic-wait-landmarks'] += serial['landmarks']
        tables['traffic-wait-profile-composition'] += serial['profiles']+net['profiles']
        tables['network-timing-score-sweeps'] += net['sweeps']
        tables['network-timing-pareto'] += [r for r in net['sweeps'] if r['pareto_within_score']]
        tables['network-timing-landmarks'] += net['landmarks']
        tables['reference-points'] += serial['references']+net['references']
        screen = COINIT['fixed_screen'](members, view)
        tables['reference-points'].append(dict(screen['point'], phase='EQUAL_COST_THEORETICAL_ONLY'))
        systems = dict(COINIT['views'](population))[view]
        for scope, group in (('NETWORK_COMMON',members),('LOCAL_DELIVERY',[r for r in systems if r['network_bytes']==0])):
            tables['input-end-to-end-profile-summary'] += COINIT['stratified_distributions'](group,view,
                ['T_I_ser_s','T_I_prop_ns','T_I_net_s','U_pot','U_net_pot','G_I_pot_s','G_I_net_pot_s'],scope)
        differences = {}
        for baseline, candidate, name in comparisons:
            rows = compare_wait_curves(net['by_score'][baseline],net['by_score'][candidate],view,name)
            tables['same-wait-comparisons'] += rows
            differences[name] = dict(attainable_wait_targets=len(rows),
                candidate_fewer_bytes=sum(r['delta_planned_bytes']<0 for r in rows),
                same_bytes=sum(r['delta_planned_bytes']==0 for r in rows),
                candidate_more_bytes=sum(r['delta_planned_bytes']>0 for r in rows),
                relative_bytes_saved=COINIT['distribution']([r['relative_bytes_saved'] for r in rows]))
        summary['views'][view] = dict(system_candidates=len(systems), network_candidates=len(members),
            needed_count=sum(r['label']=='NEEDED' for r in members),
            no_fault_count=sum(r['label']=='NO_FAULT' for r in members),
            local_delivery_count=len(systems)-len(members),
            total_observed_wait_ns=net['references'][0]['total_actual_critical_wait_ns'],
            serial_landmarks=serial['landmarks'], network_landmarks=net['landmarks'],
            comparison_summary=differences,
            fixed_equal_cost_reference=screen['point'],
            unique_cuts={score:len(points)-1 for score,points in net['by_score'].items()},
            references=net['references'])
    summary['f3_out_of_model'] = [dict(task_id=r['task_id'],P_F=r['P_F'],label=r['label'],
        input_critical_wait_ns=r['critical_wait_ns']) for r in population if r['fault_source']=='F3']
    return tables, summary


def write_audit(root, verified, coinit, output, probe):
    """Only new offline files; native bridge evaluates a pure method without running ns-3."""
    canonical = Path(json.loads((root/'execution.json').read_text())['canonical_reference'])
    protected = [root,verified,coinit,canonical]
    API['HISTORY']['output_guard'](output,protected)
    before = API['evidence_metadata'](protected)
    data = COINIT['collect_audit'](root,verified)
    legacy = API['rows'](coinit,'input-timing-features.csv')
    require([{k:'' if v is None else str(v) for k,v in r.items()} for r in data['features']]==legacy,
            'v2 causal features changed; stop rather than rewrite historical outputs')
    old = json.loads((coinit/'summary.json').read_text())
    require(old['source_identity']==data['identity'] and old['raw_evidence_read_only'], 'v2 source identity mismatch')
    repo = Path(__file__).resolve().parents[5]
    relative = 'contrib/satcompute/traffic/network-transfer-engine.cc'
    executed = subprocess.run(['git','show',data['identity']['execution_commit']+':'+relative],cwd=repo,
                              check=True,capture_output=True,text=True).stdout
    require((repo/relative).read_text()==executed, 'transfer estimator differs from Stage B execution source')
    cases = [(r['input_bytes'],r['input_path']['admitted_rate_bps'] or 0,r['input_path']['propagation_ns'],
              r['input_path']['admissible'],r['input_path']['local_delivery']) for r in data['snapshots']]
    ns = native_estimates(probe,cases)
    require(len(ns)==len(data['features']), 'native coverage mismatch')
    features = [network_feature(r,f,t) for r,f,t in zip(data['snapshots'],data['features'],ns)]
    tables, summary = assemble(COINIT['join_labels'](features,data['trace']['labels']))
    tables['input-end-to-end-features'] = features
    # Preserve the exact historical PF-90%-task-recall comparison alongside the new fixed waiting levels.
    summary['historical_10_percent_claim_check'] = []
    members = [r for r in COINIT['join_labels'](features,data['trace']['labels']) if r['network_bytes']>0]
    for view, group in COINIT['views'](members):
        cut = next(r['point']['pf_cut'] for r in data['trace']['reduced']['summary']['views'][view]['recall_landmarks']
                   if r['target_recall']==.9)
        point = COINIT['operating_point'](group,[r for r in group if r['P_F']>=cut],view,
                                         'HISTORICAL_PF_TASK_RECALL_90','P_F',cut)
        alternative = first_cover(COINIT['ranking'](group,view,'G_I_pot_s'),point['captured_actual_critical_wait_ns'])
        summary['historical_10_percent_claim_check'].append(dict(view=view,baseline=point,alternative=alternative,
            relative_bytes_saved=1-alternative['planned_staged_network_bytes']/point['planned_staged_network_bytes']))
    summary.update(source_identity=data['identity'], initialization_contract=data['contract'],
        prior_runtime_equivalence=old['prior_runtime_equivalence'],
        native_estimator=dict(method='AdmissiblePathEstimate::TransferTimeNs',probe=str(probe),
            source=relative,source_matches_stage_b_execution=True,native_calls=len(ns),simulation_started=False,
            contract='ceil(bytes*8e9/rate_bps) + propagation_ns; LocalDelivery 1 ns boundary is not network time'),
        sources=dict(raw=str(root),verified_stage_b=str(verified),coinitialization_v2=str(coinit)),
        serial_features_identical_to_v2=True, raw_evidence_read_only=before==API['evidence_metadata'](protected))
    require(summary['raw_evidence_read_only'],'raw evidence modified')
    output.mkdir(parents=True,exist_ok=False)
    for name,records in tables.items():
        API['HISTORY']['write_csv'](output/(name+'.csv'),records)
    (output/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
    require(before==API['evidence_metadata'](protected),'raw evidence modified during output')
    return summary
