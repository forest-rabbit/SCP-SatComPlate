"""INPUT binary admission execution audit; physical lifetimes, no policy tuning."""
from collections import Counter, defaultdict
import json
from pathlib import Path
import runpy

BASE = runpy.run_path(str(Path(__file__).with_name('baseline_audit.py')))
ACCOUNTING = BASE['ACCOUNTING']
rows, require = ACCOUNTING['rows'], ACCOUNTING['require']


def distribution(values):
    result = BASE['stats'](values)
    result['mean'] = sum(values)/len(values) if values else None
    return result


def critical_wait_distribution(recoveries):
    """Legacy Eager omits the Deferred dependency barrier; missing is not zero."""
    values=[max(0,int(r['input_received_time_ns'])-int(r['state_ready_time_ns']))/1e6
            for r in recoveries if r.get('input_received_time_ns') and r.get('state_ready_time_ns')]
    return distribution(values),len(recoveries)-len(values)


def audit_prefetch(root):
    summary_path = root/'input-prefetch-summary.json'
    if not summary_path.exists(): return None
    summary = json.loads(summary_path.read_text())
    decisions = rows(root,'input-admission-decisions.csv')
    decision = {r['task_id']:r for r in decisions}
    require(len(decision)==len(decisions), 'duplicate START selector decision')
    snapshots = {str(r['task_id']):r for r in json.loads((root/'input-start-snapshots.json').read_text())['candidates']}
    requests = {str(r['task_id']):r for r in summary['tasks']}
    require(len(requests)==len(summary['tasks']), 'duplicate proactive lifetime')
    require(set(requests)=={k for k,r in decision.items() if r['decision']=='SEND'}, 'SEND/request population mismatch')
    transfers = {r['transfer_id']:r for r in rows(root,'transfer-summary.csv',True)}
    protected = rows(root,'protection-transfers.csv')
    recoveries = {r['task_id']:r for r in rows(root,'recovery-summary.csv')}
    events = rows(root,'input-prefetch-events.csv')
    categories = Counter(); total=used=normal=post=0
    for key,r in requests.items():
        d = decision[key]; s = snapshots[key]
        require(r['target']==int(d['remote'])==s['remote_node'] and
                r['requested_ns']==int(d['start_time_ns'])==s['start_time_ns'] and s['admitted'],
                'INPUT is not causally tied to the admitted actual pair')
        sent = r['sent_bytes']; total+=sent; used+=r['used_bytes']
        normal+=r['normal_sent_bytes']; post+=r['post_fault_sent_bytes']
        categories[r['byte_category']]+=sent
        require(sent==r['used_bytes']+r['unused_bytes']==r['normal_sent_bytes']+r['post_fault_sent_bytes'],
                'mixed proactive lifetime ranges')
        require(r['state'] in ('ABSENT','FAILED','RELEASED'), 'live proactive INPUT at end')
        flows = [f for f in protected if f['task_id']==key]
        pflows = [f for f in flows if f['kind']=='PREFETCH_INPUT']
        fetches = [f for f in flows if f['kind']=='RECOVERY_INPUT']
        require(len(pflows)==bool(r['flow_id']) and len(fetches)<=1, 'duplicate INPUT network lifecycle')
        if r['flow_id']:
            f = pflows[0]
            require(str(r['flow_id']) not in transfers, 'proactive flow misclassified as business')
            require(sent==int(f['sent_bytes']) and int(f['bytes'])==r['input_bytes'], 'proactive actual bytes mismatch')
            require(int(f['source_node'])==r['source'] and int(f['destination_node'])==r['target'], 'proactive target mismatch')
            require(r['source']!=r['target'], 'LocalDelivery fabricated UDP')
        else: require(sent==0, 'LocalDelivery charged network bytes')
        if r['handed_off']: require(not fetches, 'same INPUT was handed off and refetched')
        if r['refetch_reason']:
            require(r['refetch_reason'] in ('WRONG_TARGET_REFETCH','FAILED_PREFETCH_REFETCH'), 'invalid refetch label')
        if r['used_ns']>=0:
            recovery = recoveries[key]
            require(r['handed_off'] and r['ready_ns']>=0 and
                r['used_ns']==int(recovery['recovery_compute_start_time_ns'])>=r['ready_ns'], 'PREFETCH_USED before actual compute')
            require(r['used_bytes']==sent and r['unused_bytes']==0, 'used bytes truncate at fault')
            require(int(recovery['input_received_time_ns'])==r['ready_ns'], 'ready timestamp is not real reception')
        used_events=[e for e in events if e['task_id']==key and e['event']=='PREFETCH_USED']
        require(len(used_events)==int(r['used_ns']>=0), 'USED event duplication/missing')
    require(summary['B_prefetch_total']==total==normal+post, 'prefetch total lifetime mismatch')
    require(summary['B_prefetch_used']==used and summary['B_prefetch_unused']==total-used, 'used/unused mismatch')
    require(total==sum(categories.values()), 'prefetch byte categories overlap/omit')
    require(summary['used_bytes_at_end']==summary['reserved_bytes_at_end']==0,'INPUT storage leak')
    for r in recoveries.values():
        if r['input_delivery_mode'].startswith('PREFETCH') and r['recovery_compute_start_time_ns']:
            require(int(r['recovery_compute_start_time_ns'])>=max(int(r['input_received_time_ns']),int(r['state_ready_time_ns'])),
                    'compute bypassed real INPUT/state join')
    return dict(summary, selector_candidates=len(decisions), selector_SEND=sum(r['decision']=='SEND' for r in decisions),
                profile_selected=dict(Counter(r['profile'] for r in decisions if r['decision']=='SEND')))


def analyze(root):
    root=Path(root)
    ordinary=ACCOUNTING['analyze'](root)
    ts=rows(root,'task-summary.csv'); tasks={r['task_id']:r for r in ts}
    require(len(ts)==len(tasks), 'duplicate logical task')
    rs=rows(root,'recovery-summary.csv'); recoveries={r['task_id']:r for r in rs}
    ps={r['task_id']:r for r in rows(root,'protection-task-summary.csv')}
    impacts=defaultdict(list)
    for r in rows(root,'fault-task-impact.csv'):
        if r['impact_type'].startswith('RUNNING_INTERRUPTED'): impacts[r['task_id']].append(r)
    executions=[]
    for key,t in tasks.items():
        p,r=ps.get(key,{}),recoveries.get(key,{})
        normal=float(p.get('normal_protection_eq_wu') or 0)
        idle=float(r.get('recovery_reserved_idle_eq_wu') or 0)
        executions.append(dict(task_id=key,normal_eq_wu=normal,idle_eq_wu=idle,
            **BASE['task_execution'](t,r,{},[],impacts[key],normal,idle)))
    all_transfers=rows(root,'transfer-summary.csv')
    require(all(r['terminal_state'] in ('COMPLETED','FAILED','CANCELLED') for r in all_transfers), 'active physical flow')
    capacity=json.loads((root/'capacity-aware-summary.json').read_text())
    require(all(capacity[k]==0 for k in ('active_path_count_at_end','reserved_directed_link_count_at_end',
        'total_reserved_rate_bps_at_end','pending_transfer_count_at_end')), 'network reservation leak')
    network=ordinary['network']
    physical=BASE['physical_network'](root,ts,extra_kinds=('PREFETCH_INPUT',))
    prefetch=audit_prefetch(root)
    link=rows(root,'link-summary.csv')
    summary=json.loads((root/'run-summary.json').read_text())
    require(physical['business_sent_bytes']==summary['sent_application_bytes'], 'business payload conservation')
    flow=rows(root,'network-flow-metrics.csv')[0]
    require(int(flow['tx_bytes'])-28*int(flow['tx_packets'])==physical['total_physical_application_sent_bytes'],
            'physical payload union differs from IPv4/UDP FlowMonitor')
    service=sum(r[k] for r in executions for k in ('primary_actual_service_ns','recovery_actual_service_ns','replica_actual_service_ns'))
    require(service==sum(int(r['busy_time_ns']) for r in rows(root,'compute-node-summary.csv')),
            'actual task execution differs from node compute-service ledger')
    active_wait=[(int(r['recovery_compute_start_time_ns'])-int(r['fault_time_ns']))/1e6
                 for r in rs if r['recovery_compute_start_time_ns']]
    catch=[int(r['actual_T_catch_ns'])/1e6 for r in rs if r['actual_T_catch_ns']]
    wait,missing_wait=critical_wait_distribution(rs)
    protection_sent=sum(v['sent_bytes'] for k,v in network.items() if k!='RECOVERY_RESULT_NETWORK')
    return dict(tasks=len(tasks), completed=sum(t['task_success']=='1' for t in ts),
        failed=sum(t['final_state']=='FAILED' for t in ts), deadline_failed=sum(t['failure_reason']=='COMPUTE_DEADLINE_EXCEEDED' for t in ts),
        failure_reasons=dict(Counter(t['failure_reason'] for t in ts if t['final_state']=='FAILED')),
        recovery_attempts=len(rs), recovery_completed=sum(r['terminal_state']=='COMPLETED' for r in rs),
        paths=dict(Counter(r['chosen_path'] for r in rs)), fault_identity=ordinary['fault_identity'],
        simulation_duration_ns=summary['simulation_duration_ns'], unique_terminals=True,
        fault_to_compute_start_ms=distribution(active_wait), fault_to_catch_ms=distribution(catch),
        missing_compute_start=len(rs)-len(active_wait), missing_catch=len(rs)-len(catch),
        actual_INPUT_critical_wait_ms=wait, missing_INPUT_critical_wait=missing_wait,
        normal_protection_eq_wu=sum(r['normal_eq_wu'] for r in executions),
        reserved_idle_eq_wu=sum(r['idle_eq_wu'] for r in executions),
        task_execution_waste_wu=sum(r['task_execution_waste_wu'] for r in executions),
        total_waste_eq_wu=sum(r['w_waste_actual'] for r in executions),
        total_executed_wu=sum(r['total_executed_wu'] for r in executions),
        useful_work_wu=sum(r['useful_work_wu'] for r in executions),
        normal_checkpoint_sent_bytes=sum(network.get(k,{}).get('sent_bytes',0) for k in ('INIT_BASE','INIT_STATE','L1','REMOTE_BATCH')),
        recovery_INPUT_sent_bytes=network['RECOVERY_INPUT']['sent_bytes'], protection_sent_bytes=protection_sent,
        all_network_sent_bytes=physical['total_physical_application_sent_bytes'], physical_network=physical,
        per_kind_network=network, prefetch=prefetch,
        mean_link_utilization_percent=sum(float(r['utilization_percent']) for r in link)/len(link),
        max_link_whole_run_utilization_percent=max(float(r['utilization_percent']) for r in link),
        available_exposure_utilization_percent=100*sum(float(r['available_tx_busy_time_s']) for r in link)/sum(float(r['available_time_s']) for r in link),
        storage=ordinary['storage'], all_lifetimes_terminal=True)


def paired_comparison(roots, results):
    """Same realized primary fault identity only; not an exact counterfactual claim."""
    answer={}
    columns=('protection_sent_bytes','all_network_sent_bytes','total_waste_eq_wu')
    for old,new in (('D','S'),('D','N'),('E','S'),('E','N'),('S','N')):
        # Retain historical four-group analysis; final closure executes only D/S.
        if old not in results or new not in results: continue
        a,b=results[old],results[new]
        change={k:dict(before=a[k],after=b[k],delta=b[k]-a[k],
                       percent=100*(b[k]-a[k])/a[k] if a[k] else None) for k in columns}
        for k in ('fault_to_compute_start_ms','fault_to_catch_ms'):
            av,bv=a[k]['mean'],b[k]['mean']
            change[k]=dict(before=av,after=bv,delta=bv-av if av is not None and bv is not None else None,
                percent=100*(bv-av)/av if av and bv is not None else None)
        ra={r['task_id']:r for r in rows(roots[old],'recovery-summary.csv')}
        rb={r['task_id']:r for r in rows(roots[new],'recovery-summary.csv')}
        fa={str(f['fault_id']):f for f in json.loads((roots[old]/'fault-trace.json').read_text())['faults']}
        fb={str(f['fault_id']):f for f in json.loads((roots[new]/'fault-trace.json').read_text())['faults']}
        common=[]; deltas=[]
        for task in ra.keys() & rb.keys():
            x,y=ra[task],rb[task]
            # Fault IDs can shift when another task changes; compare physical event semantics.
            identity=('fault_time_ns','primary_node','fault_type')
            if any(x.get(k)!=y.get(k) for k in identity): continue
            if any(fa[x['fault_id']].get(k)!=fb[y['fault_id']].get(k) for k in ('f1_occurred','f2_occurred')): continue
            common.append(task)
            if x['actual_T_catch_ns'] and y['actual_T_catch_ns']:
                deltas.append(dict(task_id=task,before_ms=int(x['actual_T_catch_ns'])/1e6,
                    after_ms=int(y['actual_T_catch_ns'])/1e6,
                    delta_ms=(int(y['actual_T_catch_ns'])-int(x['actual_T_catch_ns']))/1e6))
        answer[f'{old}_to_{new}']=dict(whole_cohort=change,common_primary_faults=len(common),
            common_valid_catch=len(deltas),paired_catch_delta_ms=distribution([r['delta_ms'] for r in deltas]),
            paired_tasks=sorted(deltas,key=lambda r:abs(r['delta_ms']),reverse=True),
            interpretation='Whole realized cohorts plus same-time/node primary faults; no forced replay or causal counterfactual claim.')
    return answer
