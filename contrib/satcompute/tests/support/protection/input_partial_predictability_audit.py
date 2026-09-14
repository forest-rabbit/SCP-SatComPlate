"""Offline partial-coverage sensitivity; hypothetical envelopes are not calibrated bounds."""
from collections import Counter
import json
import math
from pathlib import Path
import runpy
import subprocess

CP = runpy.run_path(str(Path(__file__).with_name('input_criticalpath_audit.py')))
COINIT, NET, API = (CP[k] for k in ('COINIT','NET','API'))
require = API['require']
LEVELS = (70,80,90,95)


def side(input_ns, barrier_ns):
    """Only the barrier-side candidate test, not a resource-aware SEND/DEFER policy."""
    require(input_ns >= 0 and barrier_ns >= 0, 'negative time')
    return 'SEND_CANDIDATE' if input_ns > barrier_ns else 'DEFER_CANDIDATE' if input_ns < barrier_ns else 'UNCERTAIN'


def interval_class(input_ns, lower_ns, upper_ns, supported=True):
    require(0 <= lower_ns <= upper_ns, 'invalid barrier interval')
    if not supported:
        return 'UNCERTAIN'
    return 'SEND_CANDIDATE' if input_ns > upper_ns else 'DEFER_CANDIDATE' if input_ns < lower_ns else 'UNCERTAIN'


def interval(row, epsilon_ns):
    require(type(epsilon_ns) is int and epsilon_ns >= 0, 'invalid hypothetical error radius')
    return max(0,row['A_hat_ns']-epsilon_ns),row['A_hat_ns']+epsilon_ns


def coverage_metrics(population, covered):
    """All-task denominators retain no-fault negatives and the two unknown early tasks."""
    ids={r['task_id'] for r in covered}
    require(len(ids)==len(covered) and ids <= {r['task_id'] for r in population}, 'bad coverage identity')
    missed=[r for r in population if r['task_id'] not in ids]
    mass=math.fsum(r['P_F'] for r in population)
    wait=sum(r['critical_wait_ns'] for r in population)
    net=math.fsum(r['G_I_net_pot_s'] for r in population)
    known=math.fsum(r['G_cp_known_contribution_s'] for r in population)
    covered_wait=sum(r['critical_wait_ns'] for r in covered)
    ratio=lambda a,b: a/b if b else None
    return dict(candidate_count=len(population),covered_tasks=len(covered),task_coverage=len(covered)/len(population),
        total_first_failure_mass=mass,covered_first_failure_mass=math.fsum(r['P_F'] for r in covered),
        first_failure_mass_share=ratio(math.fsum(r['P_F'] for r in covered),mass),
        total_observed_wait_ns=wait,covered_observed_wait_ns=covered_wait,observed_wait_share=ratio(covered_wait,wait),
        total_G_net_potential_s=net,G_net_potential_share=ratio(math.fsum(r['G_I_net_pot_s'] for r in covered),net),
        total_G_cp_known_component_s=known,
        G_cp_known_component_share=ratio(math.fsum(r['G_cp_known_contribution_s'] for r in covered),known),
        covered_unknown_gcp_tasks=sum(r['criticalpath_status']!=CP['KNOWN'] for r in covered),
        uncovered_needed_count=sum(r['label']=='NEEDED' for r in missed),
        uncovered_wait_ns=sum(r['critical_wait_ns'] for r in missed),
        uncovered_max_task_wait_ns=max((r['critical_wait_ns'] for r in missed),default=0),
        true_G_share=None)


def cardinality_references(population, view):
    """Sharp, metric-specific hindsight limits and uniform-subset expectations; no RNG."""
    n=len(population)
    metrics={'first_failure_mass':'P_F','observed_wait_ns':'critical_wait_ns',
             'G_net_potential_s':'G_I_net_pot_s','G_cp_known_component_s':'G_cp_known_contribution_s'}
    result=[]
    waits=sorted(r['critical_wait_ns'] for r in population)
    needed=sum(r['label']=='NEEDED' for r in population)
    for level in LEVELS:
        m=(n*level+99)//100
        u=n-m
        # Expected maximum in a uniformly chosen uncovered subset, not a Monte Carlo run.
        expected_max=math.fsum(waits[j]*math.comb(j,u-1)/math.comb(n,u) for j in range(u-1,n)) if u else 0
        for name,key in metrics.items():
            values=sorted(r[key] for r in population)
            total=math.fsum(values)
            result.append(dict(view=view,target_task_coverage=level/100,candidate_count=n,covered_tasks=m,
                actual_task_coverage=m/n,metric=name,total=total,
                coverage_share_min=math.fsum(values[:m])/total if total else None,
                coverage_share_max=math.fsum(values[n-m:])/total if total else None,
                uniform_subset_expected_share=m/n if total else None,
                uncovered_needed_min=max(0,needed-m),uncovered_needed_max=min(needed,u),
                uniform_expected_uncovered_needed=needed*u/n,
                uncovered_wait_min_ns=sum(waits[:u]),uncovered_wait_max_ns=sum(waits[n-u:]) if u else 0,
                uniform_expected_uncovered_wait_ns=sum(waits)*u/n,
                uncovered_max_task_wait_min_ns=waits[u-1] if u else 0,
                uncovered_max_task_wait_max_ns=waits[-1] if u else 0,
                uniform_expected_uncovered_max_task_wait_ns=expected_max,
                interpretation='Independent marginal hindsight bounds; extrema need not share one subset. Uniform expectation assumes coverage independent of values. Neither is achieved predictor reliability.'))
    return result


def conditional_gain_bounds(row, epsilon_ns):
    """If every future A_k is in this interval, bound value without a checkpoint projector."""
    lower,upper=interval(row,epsilon_ns)
    lo=[]; hi=[]
    for step in row['_steps']:
        w=step['first_failure_mass']
        lead=min(row['T_I_net_ns'],max(0,step['time_ns']-row['initializing_start_time_ns']))
        early=step['time_ns'] <= row['estimated_init_ready_time_ns']
        lo.append(0.0 if early else w*min(lead,max(0,row['T_I_net_ns']-upper))/1e9)
        hi.append(w*lead/1e9 if early else w*min(lead,max(0,row['T_I_net_ns']-lower))/1e9)
    return math.fsum(lo),math.fsum(hi)


def share_bounds(population, covered, epsilon_ns):
    ids={r['task_id'] for r in covered}
    values=[(r['task_id'] in ids,*conditional_gain_bounds(r,epsilon_ns)) for r in population]
    lc=math.fsum(l for selected,l,u in values if selected)
    uc=math.fsum(u for selected,l,u in values if selected)
    lu=math.fsum(l for selected,l,u in values if not selected)
    uu=math.fsum(u for selected,l,u in values if not selected)
    return dict(assumed_G_covered_lower_s=lc,assumed_G_covered_upper_s=uc,
        assumed_G_uncovered_lower_s=lu,assumed_G_uncovered_upper_s=uu,
        assumed_G_coverage_share_lower=lc/(lc+uu) if lc+uu else None,
        assumed_G_coverage_share_upper=uc/(uc+lu) if uc+lu else None)


def realized_barriers(population, recoveries):
    """Only one observed failure per recovered task; unobserved future steps stay unobserved."""
    by_id=API['unique'](recoveries,'task_id')
    result=[]
    for r in population:
        rec=by_id.get(r['task_id'])
        reason='NO_REALIZED_FAULT' if rec is None else 'DIFFERENT_RECOVERY_TARGET' if int(
            rec['recovery_node'])!=r['remote_node'] else 'UNKNOWN_EARLY_MODEL' if r['criticalpath_status']!=CP['KNOWN'] else None
        out=dict(task_id=r['task_id'],fault_source=r['fault_source'],status=reason or 'OBSERVED_SAME_TARGET',
            label=r['label'],critical_wait_ns=r['critical_wait_ns'],A_hat_ns=r['A_hat_ns'],
            T_I_snapshot_ns=r['T_I_net_ns'],A_observed_ns=None,T_I_observed_ns=None,A_error_ns=None,
            A_error_direction=None,observed_fault_time_ns=None,canonical_step_observed=False,
            observed_first_failure_mass=None,snapshot_I_predicted_side=None,snapshot_I_oracle_side=None,
            observed_I_predicted_side=None,observed_I_oracle_side=None,
            snapshot_I_barrier_flip=None,observed_I_barrier_flip=None)
        if reason:
            result.append(out); continue
        fields=('recovery_accept_time_ns','state_ready_time_ns','input_received_time_ns','recovery_compute_start_time_ns')
        if not all(rec.get(k) for k in fields):
            out['status']='UNKNOWN_OBSERVED_TIMELINE'; result.append(out); continue
        accept,state,input_ready,compute=(int(rec[k]) for k in fields)
        require(compute==max(accept,state,input_ready),'unmodeled recovery-compute admission wait')
        a=max(0,state-accept); i=max(0,input_ready-accept)
        require(max(0,i-a)==r['critical_wait_ns'],'observed barrier disagrees with verified wait label')
        err=r['A_hat_ns']-a
        fault=int(rec['fault_time_ns'])
        step=next((s for s in r['_steps'] if s['time_ns']==fault),None)
        out.update(A_observed_ns=a,T_I_observed_ns=i,A_error_ns=err,
            A_error_direction='OVER' if err>0 else 'UNDER' if err<0 else 'EXACT',observed_fault_time_ns=fault,
            canonical_step_observed=step is not None,
            observed_first_failure_mass=step['first_failure_mass'] if step else None)
        for name,time in (('snapshot_I',r['T_I_net_ns']),('observed_I',i)):
            before,after=side(time,r['A_hat_ns']),side(time,a)
            out[name+'_predicted_side']=before; out[name+'_oracle_side']=after
            out[name+'_barrier_flip']=None if 'UNCERTAIN' in (before,after) else before!=after
        result.append(out)
    return result


def error_summary(observations, view):
    rows=[r for r in observations if r['status']=='OBSERVED_SAME_TARGET' and (view=='ALL_FAULT' or r['fault_source']!='F3')]
    result=[]
    for direction in ('ALL','OVER','UNDER','EXACT'):
        group=[r for r in rows if direction=='ALL' or r['A_error_direction']==direction]
        for basis in ('snapshot_I','observed_I'):
            binary=[r for r in group if r[basis+'_barrier_flip'] is not None]
            flips=[r for r in binary if r[basis+'_barrier_flip']]
            result.append(dict(view=view,error_direction=direction,basis=basis,observed_count=len(group),
                binary_comparable=len(binary),equality_uncertain=len(group)-len(binary),flips=len(flips),
                flip_fraction=len(flips)/len(binary) if binary else None,
                false_send=sum(r[basis+'_predicted_side']=='SEND_CANDIDATE' for r in flips),
                false_defer=sum(r[basis+'_predicted_side']=='DEFER_CANDIDATE' for r in flips),
                false_defer_observed_wait_ns=sum(r['critical_wait_ns'] for r in flips if r[basis+'_predicted_side']=='DEFER_CANDIDATE'),
                max_abs_error_ns=max((abs(r['A_error_ns']) for r in group),default=None),
                actual_policy_action_flip_fraction=None,
                interpretation='Barrier-side flip holding INPUT time fixed; not an executable SEND/DEFER decision or whole-population accuracy.'))
    return result


def interval_point(population, observations, view, epsilon_ns):
    """An assumed uniform error envelope, not a fitted/calibrated confidence interval."""
    groups={'SEND_CANDIDATE':[],'DEFER_CANDIDATE':[],'UNCERTAIN':[]}
    classes={}
    for r in population:
        cls=interval_class(r['T_I_net_ns'],*interval(r,epsilon_ns),supported=r['criticalpath_status']==CP['KNOWN'])
        groups[cls].append(r); classes[r['task_id']]=cls
    confident=groups['SEND_CANDIDATE']+groups['DEFER_CANDIDATE']
    row=dict(view=view,epsilon_ns=epsilon_ns,**coverage_metrics(population,confident),
        **share_bounds(population,confident,epsilon_ns))
    for cls,prefix in (('SEND_CANDIDATE','confident_send'),('DEFER_CANDIDATE','confident_defer'),('UNCERTAIN','uncertain')):
        group=groups[cls]
        row[prefix+'_count']=len(group)
        row[prefix+'_needed_count']=sum(r['label']=='NEEDED' for r in group)
        row[prefix+'_wait_ns']=sum(r['critical_wait_ns'] for r in group)
        row[prefix+'_max_wait_ns']=max((r['critical_wait_ns'] for r in group),default=0)
    row['confident_send_wait_share']=row['confident_send_wait_ns']/row['total_observed_wait_ns'] if row['total_observed_wait_ns'] else None
    obs=[r for r in observations if r['status']=='OBSERVED_SAME_TARGET' and r['task_id'] in classes]
    row['observed_validation_count']=len(obs)
    row['observed_bound_violations']=sum(abs(r['A_error_ns'])>epsilon_ns for r in obs)
    row['observed_confident_count']=sum(classes[r['task_id']]!='UNCERTAIN' for r in obs)
    row['observed_confident_barrier_errors']=sum(classes[r['task_id']]!='UNCERTAIN' and
        r['snapshot_I_oracle_side']!='UNCERTAIN' and classes[r['task_id']]!=r['snapshot_I_oracle_side'] for r in obs)
    row['bound_reliability']='HYPOTHETICAL_NOT_CALIBRATED'
    return row,classes


def assemble(population, recoveries):
    require(population and all(r['cohort']==COINIT['COHORT'] for r in population),'mixed cohort')
    require(all(r['network_bytes']>0 for r in population),'LocalDelivery must remain separate')
    observations=realized_barriers(population,recoveries)
    tables={name:[] for name in ('cardinality-references','interval-coverage-sweep','coverage-landmarks',
        'landmark-membership','barrier-error-summary')}
    tables['realized-barriers']=observations
    summary=dict(status='PARTIAL_PREDICTABILITY_OFFLINE_COMPLETE',purpose='DEVELOPMENT_CALIBRATION',
        final_performance_result=False,production_rule=None,selected_threshold=None,selected_error_radius=None,
        exact_future_G='UNKNOWN',actual_policy_action_flip_fraction=None,views={},
        contract=['Task coverage is not wait coverage, mass coverage or correct-action coverage.',
            'sum(P_F) is total unconditional first-failure mass across candidates, not P(any task fails).',
            'G_net is INPUT timing potential; G_cp_known is a mean-model subtotal, not true G.',
            'Assume |A_k-A_hat|<=epsilon for all supported future steps; this is not a calibrated confidence interval.',
            'Equality and early initialization are uncertain. Predicted initialization-ready is not an actual receipt.',
            'Current INPUT-path uncertainty, relocation, dynamic checkpoints, contention and final action cost are not predicted.',
            'All epsilon transitions are scanned; coverage landmarks are descriptive, not chosen production thresholds.',
            'Only realized same-target faults validate an A observation; no-fault negatives remain in every task-coverage denominator.',
            'Observed barrier errors are selected failure cases, not whole-population accuracy. F3 is out of model.'])
    for view,group in COINIT['views'](population):
        tables['cardinality-references']+=cardinality_references(group,view)
        tables['barrier-error-summary']+=error_summary(observations,view)
        # Integer-ns transition points include both equality and each open side, preserving ties.
        margins={abs(r['T_I_net_ns']-r['A_hat_ns']) for r in group if r['criticalpath_status']==CP['KNOWN']}
        radii=sorted({0}|margins|{m-1 for m in margins if m>0},reverse=True)
        sweep=[interval_point(group,observations,view,e)[0] for e in radii]
        require(all(a['covered_tasks']<=b['covered_tasks'] for a,b in zip(sweep,sweep[1:])),
                'confidence coverage not monotone in decreasing radius')
        tables['interval-coverage-sweep']+=sweep
        marks=[]
        for level in LEVELS:
            count=(len(group)*level+99)//100
            point=next((r for r in sweep if r['covered_tasks']>=count),None)
            require(point is not None,'requested hypothetical coverage unreachable; report separately')
            index=sweep.index(point)
            mark=dict(point,target_task_coverage=level/100,required_covered_tasks=count,
                preceding_attainable_coverage=sweep[index-1]['task_coverage'] if index else None)
            marks.append(mark)
            _,classes=interval_point(group,observations,view,point['epsilon_ns'])
            for r in group:
                tables['landmark-membership'].append(dict(view=view,target_task_coverage=level/100,
                    epsilon_ns=point['epsilon_ns'],task_id=r['task_id'],hypothetical_class=classes[r['task_id']],
                    P_F=r['P_F'],G_net_potential_s=r['G_I_net_pot_s'],label=r['label'],
                    fault_source=r['fault_source'],critical_wait_ns=r['critical_wait_ns']))
        tables['coverage-landmarks']+=marks
        observed=[r for r in observations if r['status']=='OBSERVED_SAME_TARGET' and r['task_id'] in {x['task_id'] for x in group}]
        largest_error=max((abs(r['A_error_ns']) for r in observed),default=None)
        # A retrospective support check only; do not feed the largest error into the causal sweep.
        empirical=interval_point(group,observations,view,largest_error)[0] if largest_error is not None else None
        summary['views'][view]=dict(candidate_count=len(group),needed_count=sum(r['label']=='NEEDED' for r in group),
            no_fault_count=sum(r['label']=='NO_FAULT' for r in group),first_failure_mass=math.fsum(r['P_F'] for r in group),
            total_observed_wait_ns=sum(r['critical_wait_ns'] for r in group),
            future_step_count=sum(len(r['_steps']) for r in group),
            observations=len(observed),observed_canonical_steps=sum(r['canonical_step_observed'] for r in observed),
            observed_step_mass=math.fsum(r['observed_first_failure_mass'] or 0 for r in observed if r['fault_source']!='F3'),
            unobserved_candidate_count=len(group)-len(observed),landmarks=marks,
            radius_transition_count=len(radii),maximum_hypothetical_confidence=sweep[-1]['task_coverage'],
            retrospective_max_abs_error_ns=largest_error,retrospective_envelope_point=empirical)
    summary['f3_out_of_model']=[dict(task_id=r['task_id'],critical_wait_ns=r['critical_wait_ns']) for r in population if r['fault_source']=='F3']
    return tables,summary


def load_validated_population(root, verified, criticalpath, probe):
    """Shared read-only provenance/feature reconstruction for subsequent bound audits."""
    prior=json.loads((criticalpath/'summary.json').read_text())
    canonical=Path(json.loads((root/'execution.json').read_text())['canonical_reference'])
    protected=[root,verified,criticalpath,canonical]+[Path(prior['sources'][k]) for k in ('coinit','latency')]
    before=API['evidence_metadata'](protected)
    data=COINIT['collect_audit'](root,verified)
    require(data['identity']==prior['source_identity'] and prior['raw_evidence_read_only'],'v4 provenance mismatch')
    repo=Path(__file__).resolve().parents[5]
    for name in prior['production_source_matches_execution']:
        relative='contrib/satcompute/'+name
        executed=subprocess.run(['git','show',data['identity']['execution_commit']+':'+relative],cwd=repo,
                                capture_output=True,text=True,check=True).stdout
        require((repo/relative).read_text()==executed,'production execution source changed: '+relative)
    cases=[(r['input_bytes'],r['input_path']['admitted_rate_bps'] or 0,r['input_path']['propagation_ns'],
            r['input_path']['admissible'],r['input_path']['local_delivery']) for r in data['snapshots']]
    features=[]
    for r,f,t in zip(data['snapshots'],data['features'],NET['native_estimates'](probe,cases)):
        features.append(CP['criticalpath_feature'](r,NET['network_feature'](r,f,t))[0])
    require([{k:'' if v is None else str(v) for k,v in f.items()} for f in features]==
            API['rows'](criticalpath,'criticalpath-g-features.csv'),'v4 features changed')
    population=COINIT['join_labels'](features,data['trace']['labels'])
    for f,r in zip(population,data['snapshots']):
        require(f['task_id']==str(r['task_id']),'snapshot identity order mismatch')
        f['A_hat_ns']=math.ceil(f['dependency_ready_bar_s']*1e9)
        f['_steps']=COINIT['TRACE']['feature_snapshot'](r)['future_first_failure_trajectory']
        require(math.isclose(math.fsum(s['first_failure_mass'] for s in f['_steps']),f['P_F'],abs_tol=1e-12),
                'first-failure mass changed')
    require(before==API['evidence_metadata'](protected),'historical evidence changed during reconstruction')
    return population,data,prior,protected,before


def write_audit(root, verified, criticalpath, output, probe):
    """Revalidate the existing trace and causal v4 features; no changes to old artifacts."""
    population,data,prior,protected,before=load_validated_population(root,verified,criticalpath,probe)
    API['HISTORY']['output_guard'](output,protected)
    network=[r for r in population if r['network_bytes']>0]
    tables,summary=assemble(network,API['rows'](root,'recovery-summary.csv'))
    summary.update(source_identity=data['identity'],prior_runtime_equivalence=prior['prior_runtime_equivalence'],
        simulation_started=False,production_source_unchanged=True,v4_features_identical=True,
        A_hat_ns_rounding='ceil(v4 mean dependency seconds * 1e9), at most one ns; does not change v4 scores',
        local_delivery=[dict(task_id=r['task_id'],P_F=r['P_F'],label=r['label'],network_bytes=0) for r in population if r['network_bytes']==0],
        sources=dict(raw=str(root),verified=str(verified),criticalpath_v4=str(criticalpath)),
        raw_evidence_read_only=before==API['evidence_metadata'](protected))
    require(summary['raw_evidence_read_only'],'historical evidence changed')
    output.mkdir(parents=True,exist_ok=False)
    for name,records in tables.items():
        API['HISTORY']['write_csv'](output/(name+'.csv'),records)
    (output/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
    require(before==API['evidence_metadata'](protected),'historical evidence changed during output')
    return summary
