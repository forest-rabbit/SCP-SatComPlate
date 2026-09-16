"""Offline-only residual S-vs-historical-N audit; no production selector or optimizer."""
from collections import Counter
import csv
import json
import math
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
RESIDUAL = runpy.run_path(str(HERE/'residual_deadline_audit.py'))
require = RESIDUAL['require']
TASKS = RESIDUAL['TASKS']
HISTORICAL_N_COMMIT = 'a4315e2b8'


def transfer_times(input_bytes, path):
    """Exact native AdmissiblePathEstimate::TransferTimeNs integer definition."""
    if not path['admissible']:
        return None
    if not input_bytes:
        return 0, 0
    if path['local']:
        return 0, 0
    rate = path['rate_bps']
    propagation = path['propagation_ns']
    if not rate or propagation < 0:
        return None
    serialization = (input_bytes*8_000_000_000+rate-1)//rate
    return serialization, serialization+propagation


def evaluate(*, start_ns, remaining_ns, first_sample_ns, finish_exclusive,
             input_bytes, path, p_fail, steps):
    """Historical a4315e2b8 S/N definition, including common SER cost and strict >."""
    require(start_ns >= 0 and remaining_ns >= 0, 'invalid INPUT prediction horizon')
    times = transfer_times(input_bytes, path)
    if times is None or p_fail is None:
        return dict(known=False,sSend=None,nSend=None,reason='PATH_OR_PREDICTION_UNAVAILABLE')
    serialization,network = times
    if path['local'] or not input_bytes:
        return dict(known=True,sSend=True,nSend=True,reason='LOCAL_DELIVERY',
                    serializationNs=serialization,networkReadyNs=network,
                    serialGainNs=0,networkGainNs=0,costNs=0)
    require(math.isfinite(p_fail) and 0 <= p_fail <= 1, 'invalid canonical INPUT P_F')
    finish=start_ns+remaining_ns
    survival=1.0;mass=0.0;serial_gain=0.0;network_gain=0.0;previous=-1
    for step in steps:
        t,q=step['time_ns'],step['q_comp']
        require(math.isfinite(q) and 0 <= q <= 1 and t >= start_ns and
                t >= first_sample_ns and t > previous and t <= finish and
                not (finish_exclusive and t == finish), 'invalid canonical INPUT probability trajectory')
        previous=t
        weight=survival*q
        survival*=1-q
        mass+=weight
        lead=t-start_ns
        serial_gain+=weight*min(serialization,lead)
        network_gain+=weight*min(network,lead)
    require(abs(mass-p_fail) <= 1e-12, 'INPUT trajectory does not cover canonical P_F')
    cost=(1-p_fail)*serialization
    return dict(known=True,sSend=serial_gain > cost,nSend=network_gain > cost,
                reason='STRICT_HISTORICAL_BREAK_EVEN',serializationNs=serialization,
                networkReadyNs=network,serialGainNs=serial_gain,
                networkGainNs=network_gain,costNs=cost)


def residual_evaluation(record):
    path=record['input_path']
    result=evaluate(start_ns=record['time_ns'],remaining_ns=record['remaining_ns'],
        first_sample_ns=record['selective_first_sample_ns'],
        finish_exclusive=record['selective_finish_exclusive'],input_bytes=record['input_bytes'],
        path=dict(admissible=path['admissible'],local=path['local'],rate_bps=path['rate_bps'],
                  propagation_ns=path['propagation_ns']),p_fail=record['selective_p_fail'],
        steps=record['selective_steps'])
    require(result['known'],'Stage 2B non-veto candidate lacks S/N evidence')
    require(result['serializationNs']==record['serialization_ns'] and
            result['networkReadyNs']==record['network_ready_ns'],
            'native TransferTimeNs does not match captured candidate path')
    require(result['sSend']==record['selective_pure_send'],
            'offline S differs from frozen production SER')
    require(math.isclose(result['serialGainNs'],record['serial_gain_ns'],rel_tol=1e-14,abs_tol=1e-9) and
            math.isclose(result['costNs'],record['serial_cost_ns'],rel_tol=1e-14,abs_tol=1e-9),
            'offline S quantities differ from captured production SER')
    tr=record['recovery_no_full_input_min_seconds'];slack=record['slack_seconds']
    send=result['sSend'] or result['nSend']
    resource_model_known=tr is not None and record['resource_feasible_configs'] > 0
    originally_deadline_infeasible=(record['original_reason']=='DEADLINE_INFEASIBLE' and
        resource_model_known and record['fault_input_seconds']+tr > slack)
    zero_input_feasible=resource_model_known and tr <= slack
    result.update(taskId=record['task_id'],decisionTimeNs=record['time_ns'],
        decisionTrigger=record['trigger'],local=record['fixed_local'],remote=record['remote'],
        candidateIndex=record['candidate_index'],candidateCount=record['candidate_count'],
        P_F=record['selective_p_fail'],S_SEND=result['sSend'],N_SEND=result['nSend'],
        S_TO_N_DEFER_TO_SEND=(not result['sSend'] and result['nSend']),
        originalReason=record['original_reason'],deadlineSlackSeconds=slack,
        faultInputSeconds=record['fault_input_seconds'],recoveryNoFullInputMinSeconds=tr,
        zeroInputAdmissionChecked=send,originallyDeadlineInfeasible=originally_deadline_infeasible,
        zeroInputAdmissionFeasible=zero_input_feasible if send else None,
        deadlineBecameFeasibleAfterZeroInput=(originally_deadline_infeasible and zero_input_feasible) if send else None,
        resourceFeasibleConfigurations=record['resource_feasible_configs'],
        bestDeltaPermille=record['delta_permille'],bestBatchN=record['batch_n'],
        evidence='CAUSAL_NON_VETO_CANDIDATE_OFFLINE_ONLY')
    return result


def fixture_evaluation(row):
    path=row['input_path'];prediction=row['predictor']
    return evaluate(start_ns=row['start_time_ns'],remaining_ns=row['remaining_compute_ns'],
        first_sample_ns=prediction['first_sample_time_ns'],finish_exclusive=prediction['finish_exclusive'],
        input_bytes=row['input_bytes'],path=dict(admissible=path['admissible'],
            local=path['local_delivery'],rate_bps=path.get('admitted_rate_bps') or 0,
            propagation_ns=path.get('propagation_ns') or 0),p_fail=prediction['P_F'],
        steps=prediction['future_steps'])


def validate_historical_n(fixture_path, archived_s=None, archived_n=None):
    candidates=json.loads(Path(fixture_path).read_text())['candidates']
    evaluated={str(r['task_id']):(r,fixture_evaluation(r)) for r in candidates}
    require(len(evaluated)==len(candidates)==409,'historical S/N anchor population changed')
    for source,result in evaluated.values():
        require(result['sSend']==source['expected_send'],'historical S anchor changed')
        require(not result['sSend'] or result['nSend'],'historical N is not a superset of S')
    network=[(r,v) for r,v in evaluated.values() if not r['input_path']['local_delivery']]
    local=[(r,v) for r,v in evaluated.values() if r['input_path']['local_delivery']]
    require((len(network),sum(v['sSend'] for _,v in network),sum(v['nSend'] for _,v in network),len(local))==
            (405,68,115,4),'historical 68/115/4 N anchor changed')
    archive=dict(available=False,rows=0,matched=0)
    if archived_s is not None and archived_n is not None:
        s={r['task_id']:r for r in csv.DictReader(Path(archived_s).open())}
        n={r['task_id']:r for r in csv.DictReader(Path(archived_n).open())}
        require(s.keys()==n.keys()==evaluated.keys(),'archived S/N runtime population differs')
        for task,(source,result) in evaluated.items():
            require((s[task]['decision']=='SEND')==result['sSend'] and
                    (n[task]['decision']=='SEND')==result['nSend'],'archived runtime S/N decision differs')
            require(int(s[task]['T_ser_ns'])==result['serializationNs'] and
                    int(n[task]['T_net_ns'])==result['networkReadyNs'],'archived native transfer time differs')
        archive=dict(available=True,rows=len(s)+len(n),matched=len(s)+len(n))
    return dict(status='PASS',sourceCommit=HISTORICAL_N_COMMIT,candidates=409,
        networkCandidates=405,networkSSend=68,networkNSend=115,localDelivery=4,
        sOnly=0,definition='G_net=sum(w_k*min(T_net,lead_k)); SEND iff G_net>(1-P_F)*T_ser',
        archivedRuntime=archive)


def summarize(rows):
    tasks=[]
    for task in TASKS:
        group=[r for r in rows if r['taskId']==task]
        require(group,'missing residual task')
        s_send=any(r['S_SEND'] for r in group);n_send=any(r['N_SEND'] for r in group)
        new=[r for r in group if r['S_TO_N_DEFER_TO_SEND']]
        union=[r for r in group if r['S_SEND'] or r['N_SEND']]
        best_s=max(group,key=lambda r:r['serialGainNs']-r['costNs'])
        best_n=max(group,key=lambda r:r['networkGainNs']-r['costNs'])
        tasks.append(dict(taskId=task,candidateSnapshots=len(group),
            decisionEvents=len({(r['decisionTimeNs'],r['decisionTrigger']) for r in group}),
            S_SEND=s_send,N_SEND=n_send,S_TO_N_NEW_TASK=(not s_send and n_send),
            sSendCandidates=sum(r['S_SEND'] for r in group),nSendCandidates=sum(r['N_SEND'] for r in group),
            sToNNewCandidates=len(new),sendCandidatesZeroInputFeasible=sum(r['zeroInputAdmissionFeasible'] is True for r in union),
            newCandidatesZeroInputFeasible=sum(r['zeroInputAdmissionFeasible'] is True for r in new),
            bestSMarginNs=best_s['serialGainNs']-best_s['costNs'],
            bestNMarginNs=best_n['networkGainNs']-best_n['costNs'],
            maxNIncrementalGainNs=max(r['networkGainNs']-r['serialGainNs'] for r in group),
            allSendCandidatesBecameFeasible=bool(union) and all(r['deadlineBecameFeasibleAfterZeroInput'] for r in union),
            evidence='EXISTENTIAL_SEND_OVER_NON_VETO_CANDIDATE_SNAPSHOTS'))
    return tasks


def write_csv(path,data):
    require(data,'empty audit output')
    with Path(path).open('w',newline='') as handle:
        writer=csv.DictWriter(handle,fieldnames=list(data[0]));writer.writeheader();writer.writerows(data)


def analyze(snapshot_path,output,fixture_path,archived_s=None,archived_n=None):
    source=json.loads(Path(snapshot_path).read_text())
    require(source['diagnostic_only'] is True and sorted(source['task_filter'])==list(TASKS),
            'unexpected Stage 2B residual population')
    raw=[r for r in source['records'] if not r['observed_fault_hit']]
    require(len(raw)==1238 and {r['task_id'] for r in raw}==set(TASKS),'non-veto population changed')
    records=[residual_evaluation(r) for r in raw]
    tasks=summarize(records)
    historical=validate_historical_n(fixture_path,archived_s,archived_n)
    new_tasks=[r['taskId'] for r in tasks if r['S_TO_N_NEW_TASK']]
    s_tasks=[r['taskId'] for r in tasks if r['S_SEND']]
    n_tasks=[r['taskId'] for r in tasks if r['N_SEND']]
    new_candidates=[r for r in records if r['S_TO_N_DEFER_TO_SEND']]
    union=[r for r in records if r['S_SEND'] or r['N_SEND']]
    still_deferred=[r for r in records if not r['S_SEND']]
    result=dict(status='PASS',diagnosticOnly=True,noSimulation=True,
        scope=dict(tasks=list(TASKS),nonVetoCandidateSnapshots=len(records),decisionEvents=len({
            (r['taskId'],r['decisionTimeNs'],r['decisionTrigger']) for r in records})),
        historicalN=historical,
        candidateCounts=dict(sSend=sum(r['S_SEND'] for r in records),nSend=sum(r['N_SEND'] for r in records),
            sToNNew=len(new_candidates),sendUnion=len(union),
            maxNIncrementalGainNs=max(r['networkGainNs']-r['serialGainNs'] for r in records),
            maxNIncrementalGainAmongSDeferNs=max(r['networkGainNs']-r['serialGainNs'] for r in still_deferred),
            bestNMarginAmongSDeferNs=max(r['networkGainNs']-r['costNs'] for r in still_deferred),
            sendUnionBecameFeasibleAfterZeroInput=sum(r['deadlineBecameFeasibleAfterZeroInput'] is True for r in union),
            newSendBecameFeasibleAfterZeroInput=sum(r['deadlineBecameFeasibleAfterZeroInput'] is True for r in new_candidates)),
        answers=dict(q1=dict(sSendTasks=len(s_tasks),nSendTasks=len(n_tasks),sTaskIds=s_tasks,nTaskIds=n_tasks),
            q2=dict(newTaskCount=len(new_tasks),newTaskIds=new_tasks),
            q3=dict(newSendTaskCount=len({r['taskId'] for r in new_candidates}),
                    newSendCandidateCount=len(new_candidates),
                    newlyFeasibleTaskCount=len({r['taskId'] for r in new_candidates if r['deadlineBecameFeasibleAfterZeroInput']}),
                    newlyFeasibleCandidateCount=sum(r['deadlineBecameFeasibleAfterZeroInput'] is True for r in new_candidates),
                    allExistingSendCandidatesBecameFeasible=len(union) and
                        all(r['deadlineBecameFeasibleAfterZeroInput'] for r in union)),
            q4='KEEP_S_FOR_THIS_RESIDUAL_COHORT; N_ADDS_NO_TASK_OR_CANDIDATE'),
        semantics='Task SEND is existential over its causal non-veto candidate snapshots. No task/remote is selected.',
        admission='For S/N SEND candidates only, TI is set to zero in the already captured exact search; TRmin<=D proves one unchanged resource/storage-feasible (delta,n).',
        limitation='Offline feasibility is not START commitment, receiver completion, placement ranking, or successful recovery.',
        tasks=tasks)
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    write_csv(output/'residual-selective-s-vs-n-candidates.csv',records)
    write_csv(output/'residual-selective-s-vs-n-tasks.csv',tasks)
    (output/'residual-selective-s-vs-n-summary.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
