"""Read-only model decomposition. No alternate policy, deadline relaxation or runtime oracle."""
from collections import Counter, defaultdict
import csv
from decimal import Decimal, ROUND_CEILING
import json
import math
from pathlib import Path
import runpy

COVERAGE = runpy.run_path(str(Path(__file__).with_name('candidate_coverage_audit.py')))
rows, require = COVERAGE['rows'], COVERAGE['require']
TASKS = (5,12,13,193,244,300,302,304,513,584,766)
LABELS = dict(A='INPUT_DOMINATED_PREFETCH_CANDIDATE', B='RECOVERY_DOMINATED_INFEASIBLE',
    C='JOINTLY_INFEASIBLE_PARTIAL_PREFETCH_CANDIDATE', D='BOTH_INDIVIDUALLY_INFEASIBLE',
    E='UNKNOWN_INSUFFICIENT_EVIDENCE', F='DEFERRED_MODEL_FEASIBLE',
    R='NO_RESOURCE_FEASIBLE_CONFIGURATION')
NS = 10**9


def classify(ti, tr, slack):
    if any(v is None or not math.isfinite(v) for v in (ti,tr,slack)):return 'E'
    require(ti >= 0 and tr >= 0, 'negative model cost')
    if ti > slack:return 'D' if tr > slack else 'A'
    if tr > slack:return 'B'
    return 'C' if ti+tr > slack else 'F'


def minimum_prefetch(ti, tr, slack):
    if classify(ti,tr,slack) not in ('A','C'):return None,None
    seconds = max(0,ti+tr-slack)
    return seconds,seconds/ti


def masses(steps, now, first, finish, exclusive, expected):
    """Only canonical future trajectory; never observed fault time/probability."""
    survival,total,previous = 1.0,0.0,-1
    result=[]
    for s in steps:
        t,q=s['time_ns'],s['q_comp']
        require(now <= t and first <= t and t > previous and
                (t < finish if exclusive else t <= finish), 'illegal causal sample window')
        require(math.isfinite(q) and 0 <= q <= 1,'invalid risk probability')
        w=survival*q;survival*=1-q;total+=w;previous=t
        result.append((t,w))
    require(abs(total-expected) <= 1e-12, 'incomplete canonical trajectory')
    return result


def timing(record, ti, tr, slack):
    required,fraction=minimum_prefetch(ti,tr,slack)
    now=record['time_ns']
    pred=record['selective_p_fail']
    psteps=[]
    if pred is not None:
        psteps=masses(record['selective_steps'],now,record['selective_first_sample_ns'],
            now+record['remaining_ns'],record['selective_finish_exclusive'],pred)
    relevant=[(t,w) for t,w in psteps if w > 0]
    first=relevant[0][0] if relevant else None
    lead=(first-now)/NS if first is not None else None
    init=record['initialization_seconds']
    init_ns=int((Decimal.from_float(init)*NS).to_integral_value(rounding=ROUND_CEILING))
    # This is the existing model ready boundary, not proof of actual checkpoint receipt.
    joint=[(t,w) for t,w in relevant if required is not None and
           (t-now)/NS >= required and t-now >= init_ns]
    reason=record['selective_reason']
    known=pred is not None and reason not in ('PREDICTION_UNAVAILABLE','PATH_UNAVAILABLE')
    veto=record['observed_fault_hit']
    return dict(analysisTimeNs=now,firstRelevantRiskSampleNs=first,leadTimeToRiskSeconds=lead,
        fullInputSerializationSeconds=record['serialization_ns']/NS if known else None,
        modeledInputNetworkReadySeconds=record['network_ready_ns']/NS if known else None,
        requiredPrefetchFractionLowerBound=fraction,requiredPrefetchSecondsLowerBound=required,
        initializationSeconds=init,initializationReadyEstimateNs=now+init_ns,
        firstRiskMeetsPrefetchLowerBound=lead >= required if lead is not None and required is not None else None,
        firstRiskMeetsInitializationEstimate=first-now >= init_ns if first is not None else None,
        laterRiskMeetsBothModelBounds=bool(joint) if pred is not None and required is not None else None,
        firstRiskMeetingBothModelBoundsNs=joint[0][0] if joint else None,
        probabilityMassMeetingBothModelBounds=sum(w for _,w in joint),
        selectiveWouldSendAtThisTime=('SEND' if record['selective_pure_send'] else 'DEFER') if known else 'UNKNOWN',
        selectiveEvidence='PURE_CAUSAL_CANDIDATE_NOT_POST_BATCH_ADMISSION' if known else 'UNKNOWN',
        selectiveReason=reason,selectivePFail=pred,selectiveFirstSampleNs=record['selective_first_sample_ns'],
        selectiveFinishExclusive=record['selective_finish_exclusive'],sameBatchFaultVeto=veto,
        actionableModelOpportunity=bool(joint) and not veto,
        actualInputProgressAtFault='UNKNOWN_NOT_SENT',actualPrefetchAdmission='NOT_ATTEMPTED',
        actualCheckpointReadyCounterfactual='UNKNOWN_NOT_CREATED')


def decompose(record):
    ti,tr,slack=record['fault_input_seconds'],record['recovery_no_full_input_min_seconds'],record['slack_seconds']
    category=classify(ti,tr,slack)
    if tr is None and record['resource_feasible_configs'] == 0:category='R'
    out=dict(taskId=record['task_id'],decisionTimeNs=record['time_ns'],decisionTrigger=record['trigger'],
        taskProfile=record['profile'],fixedLocal=record['fixed_local'],remoteCandidate=record['remote'],
        candidateIndex=record['candidate_index'],candidateCount=record['candidate_count'],
        deadlineSlackSeconds=slack,faultInputSeconds=ti,recoveryNoFullInputSeconds=tr,
        totalDeferredRecoverySeconds=ti+tr if tr is not None else None,
        faultInputExceedsSlack=ti > slack,recoveryNoFullInputExceedsSlack=tr > slack if tr is not None else None,
        totalDeferredExceedsSlack=ti+tr > slack if tr is not None else None,
        bestDeltaPermille=record['delta_permille'],bestBatchN=record['batch_n'],
        classification=category,classificationName=LABELS[category],
        evidenceLevel='PASSIVE_SAME_CANDIDATE_MODEL' if category != 'E' else 'UNKNOWN',
        resourceFeasibleConfigurations=record['resource_feasible_configs'],
        storageRejectedConfigurations=record['storage_rejected_configs'],
        originalProposalReason=record['original_reason'],originalResolution=record['observed_resolution'],
        localInput=record['input_path']['local'],frequencyFirstSampleNs=record['frequency_first_sample_ns'],
        frequencyFinishExclusive=record['frequency_finish_exclusive'],frequencyPFail=record['frequency_p_fail'])
    out.update(timing(record,ti,tr,slack))
    return out


def validate(records, coverage):
    def key(r):return r['task_id'],r['time_ns'],r['trigger']
    groups=defaultdict(list)
    for r in records:groups[key(r)].append(r)
    expected={(int(c['task_id']),int(c['time_ns']),c['decision_trigger']):c for c in coverage}
    require(groups.keys() == expected.keys(), 'candidate decision population changed or incomplete')
    for k,group in groups.items():
        c=expected[k];checked=int(c['remote_candidates_checked'])
        require(len(group)==checked and sorted(r['candidate_index'] for r in group)==list(range(1,checked+1)),
                'missing or duplicate candidate snapshot')
        require(checked==int(c['remote_candidates_total']) and c['all_candidates_infeasible']=='1',
                'residual cohort is not exhaustive OFF rejection')
        require(len({r['remote'] for r in group})==checked, 'duplicate remote')
        for r in group:
            require(r['fixed_local']==int(c['reference_local']) and r['candidate_count']==checked,
                    'fixed-local contract or candidate count changed')
            require(not r['observed_committed'] and r['observed_fault_hit']==(c['actual_fault_hit']=='1'),
                    'actual commit/fault label differs')
            require(r['original_reason']=='DEADLINE_INFEASIBLE', 'unexpected residual hard-rejection cause')
            path=r['input_path']
            require(path['local']==(r['source']==r['remote']), 'LocalDelivery target mismatch')
            require(path['admissible'] and r['node_available'] and r['path_available'], 'missing causal path/node')
            expected_ti=0 if path['local'] else r['input_bytes']/r['input_bandwidth_bytes_per_s']
            require(r['fault_input_seconds']==expected_ti,'TI is not original target-aware Deferred model')
            if not path['local']:
                require(r['input_bandwidth_bytes_per_s']==path['rate_bps']/8,'candidate path capacity mismatch')
            tr=r['recovery_no_full_input_min_seconds']
            if tr is not None:
                d,n=r['delta_permille'],r['batch_n'];delta=d/1000
                require(10 <= d <= 100 and 1 <= n <= 100 and n*d <= 1000,'search space changed')
                model=r['variable_bytes']*(n-1)*delta/(2*r['backup_bandwidth_bytes_per_s']) + \
                    r['cR_ns']/NS*(n-1)/n + r['work']*delta/(2*r['recovery_rate'])
                require(math.isclose(tr,model,rel_tol=1e-14,abs_tol=1e-15),'recovery expression differs')
                require(r['best_local_additional_bytes']<=r['local_free_bytes'] and
                        r['best_remote_additional_bytes']<=r['remote_free_bytes'],'minimum violates storage')
            expected_slack=(r['deadline_ns']-r['time_ns'])/NS-r['work']*(1-r['progress'])/r['recovery_rate']
            require(math.isclose(r['slack_seconds'],expected_slack,rel_tol=1e-14,abs_tol=1e-14), 'slack redefined')
            require(r['frequency_finish_exclusive']==(r['trigger']!='FAULT_EPOCH'), 'historical window conflated')
            masses(r['frequency_steps'],r['time_ns'],r['frequency_first_sample_ns'],
                   r['time_ns']+r['remaining_ns'],r['frequency_finish_exclusive'],r['frequency_p_fail'])
    return dict(status='PASS',events=len(groups),candidate_snapshots=len(records),tasks=len({k[0] for k in groups}))


def summarize_task(task, records):
    live=[r for r in records if not r['sameBatchFaultVeto']]
    witnesses=[r for r in live if r['classification'] in ('A','C')]
    timely=[r for r in witnesses if r['actionableModelOpportunity']]
    first_timely=[r for r in witnesses if r['firstRiskMeetsPrefetchLowerBound'] and
                  r['firstRiskMeetsInitializationEstimate']]
    first_best=min(first_timely,key=lambda r:(r['decisionTimeNs'],r['candidateIndex'])) if first_timely else None
    pool=timely or witnesses or live or records
    best=min(pool,key=lambda r:(r['decisionTimeNs'],r['candidateIndex']))
    # Existence can be demonstrated by one witness. A global negative requires full coverage.
    classes=sorted({r['classification'] for r in live})
    category=best['classification'] if witnesses else classes[0] if len(classes)==1 else 'MIXED'
    zero_input_impossible=bool(live) and all(r['recoveryNoFullInputExceedsSlack'] is True for r in live)
    return dict(taskId=task,classification=category,observedNonVetoClasses='|'.join(classes),
        decisions=len({(r['decisionTimeNs'],r['decisionTrigger']) for r in records}),
        candidateSnapshots=len(records),nonVetoCandidateSnapshots=len(live),
        sameBatchVetoDecisions=len({(r['decisionTimeNs'],r['decisionTrigger']) for r in records if r['sameBatchFaultVeto']}),
        bestDecisionTimeNs=best['decisionTimeNs'],decisionTrigger=best['decisionTrigger'],fixedLocal=best['fixedLocal'],
        candidateRemotesConsidered=best['candidateCount'],representativeRemote=best['remoteCandidate'],
        D_slack=best['deadlineSlackSeconds'],T_input=best['faultInputSeconds'],
        T_recovery_min=best['recoveryNoFullInputSeconds'],taskProfile=best['taskProfile'],
        requiredPrefetchFractionLowerBound=best['requiredPrefetchFractionLowerBound'],
        requiredPrefetchSecondsLowerBound=best['requiredPrefetchSecondsLowerBound'],
        initializationSeconds=best['initializationSeconds'],riskLeadSeconds=best['leadTimeToRiskSeconds'],
        selectiveWouldSend=best['selectiveWouldSendAtThisTime'],
        hasFirstRiskTimelyWitness=bool(first_timely),
        firstRiskTimelyWitnessDecisionNs=first_best['decisionTimeNs'] if first_best else None,
        firstRiskTimelyWitnessTrigger=first_best['decisionTrigger'] if first_best else None,
        firstRiskTimelyWitnessLeadSeconds=first_best['leadTimeToRiskSeconds'] if first_best else None,
        firstRiskTimelyWitnessInitializationSeconds=first_best['initializationSeconds'] if first_best else None,
        hasPureSendFirstRiskTimelyWitness=any(r['selectiveWouldSendAtThisTime']=='SEND' for r in first_timely),
        hasAnyRiskTimelyWitness=bool(timely),
        hasPureSendTimelyWitness=any(r['selectiveWouldSendAtThisTime']=='SEND' for r in timely),
        zeroInputStillInfeasibleAcrossAllNonVetoCandidates=zero_input_impossible,
        canProceedToStartSelectiveStudy=bool(witnesses),
        evidenceLevel='EXISTENTIAL_MODEL_WITNESS_NOT_GUARANTEED_RESCUE' if witnesses else
            'EXHAUSTIVE_OBSERVED_CANDIDATE_MODEL',actualCounterfactualPrefetchProgress='UNKNOWN_NOT_SENT')


def write_csv(path, data):
    require(bool(data),'empty required report')
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(data[0]));writer.writeheader();writer.writerows(data)


def analyze(root, output):
    root,output=Path(root),Path(output)
    data=json.loads((root/'residual-deadline-candidates.json').read_text())
    require(data['diagnostic_only'] is True and sorted(data['task_filter'])==list(TASKS),'unexpected diagnostic filter')
    coverage=[c for c in rows(root,'compfrr-candidate-coverage.csv') if int(c['task_id']) in TASKS]
    check=validate(data['records'],coverage)
    require({r['task_id'] for r in data['records']}==set(TASKS),'residual tasks missing')
    all_rows=[decompose(r) for r in data['records']]
    tasks=[summarize_task(t,[r for r in all_rows if r['taskId']==t]) for t in TASKS]
    summary=dict(diagnostic_only=True,status='PASS',coverage=check,
        candidate_classes=dict(Counter(r['classification'] for r in all_rows)),
        non_veto_candidate_classes=dict(Counter(r['classification'] for r in all_rows if not r['sameBatchFaultVeto'])),
        task_classes=dict(Counter(t['classification'] for t in tasks)),
        first_risk_timely_tasks=sum(t['hasFirstRiskTimelyWitness'] for t in tasks),
        any_risk_timely_tasks=sum(t['hasAnyRiskTimelyWitness'] for t in tasks),
        pure_send_and_timely_tasks=sum(t['hasPureSendTimelyWitness'] for t in tasks),
        pure_send_and_first_risk_timely_tasks=sum(t['hasPureSendFirstRiskTimelyWitness'] for t in tasks),
        zero_input_still_infeasible_tasks=sum(t['zeroInputStillInfeasibleAcrossAllNonVetoCandidates'] for t in tasks),
        model_scope='Original Frequency mean-recovery model minimum; NOT a runtime worst-case bound.',
        representative_rule='Earliest non-veto model-timely A/C witness if any, otherwise earliest A/C or observed candidate.',
        limitation='No SEND/checkpoint was executed for these candidates. Post-batch admission, actual receiver progress and rescue remain UNKNOWN.',
        tasks=tasks)
    output.mkdir(parents=True,exist_ok=True)
    write_csv(output/'residual-deadline-decomposition.csv',all_rows)
    timing_fields=['taskId','decisionTimeNs','decisionTrigger','fixedLocal','remoteCandidate','candidateIndex',
        'classification',*timing(data['records'][0],0,0,0).keys()]
    write_csv(output/'residual-input-analysis-time.csv',[{k:r[k] for k in timing_fields} for r in all_rows])
    write_csv(output/'residual-task-summary.csv',tasks)
    (output/'classification-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    text=['# Stage 2B：11 个任务的因果模型分解','','单位：秒。代表行不等于生产准入；所有 candidate/event 保留在 CSV。','',
        '|任务|类|分析时刻|local/remote|D|TI|TRmin|提前比例下界|首个风险 lead|纯 SER|',
        '|---|---|---:|---|---:|---:|---:|---:|---:|---|']
    def fmt(v):return 'UNKNOWN' if v is None else f'{v:.6f}'
    for t in tasks:
        text.append(f"|{t['taskId']}|{t['classification']}|{t['bestDecisionTimeNs']/NS:.9f}|"
            f"{t['fixedLocal']}/{t['representativeRemote']}|{fmt(t['D_slack'])}|{fmt(t['T_input'])}|"
            f"{fmt(t['T_recovery_min'])}|{fmt(t['requiredPrefetchFractionLowerBound'])}|"
            f"{fmt(t['riskLeadSeconds'])}|{t['selectiveWouldSend']}|")
    text+=['','A/C 仅表明可研究提前 INPUT；不是可恢复承诺。初始化使用原有 model estimate，不伪造 checkpoint 已到达。',
        '同批次命中只保留分解，不作为可行动机会；首风险不足与所有后续风险均不足分别统计。',
        '表中代表行是最早任意未来风险模型及时的候选；任务级首风险见证可能发生于更晚的合法决策，见 task-summary 的独立 witness 字段。',
        '纯 SER 由当前候选的 QueryTaskPrediction 因果快照计算，非最终 post-batch committed pair，未改变实际策略。',
        'FAULT_EPOCH 原 Frequency 窗口包含本轮且 finishExclusive=false；纯假设 Selective query 使用下一合法样本和 finishExclusive=true。',
        'S/B 与提前比例均为理想模型下界，真实排队、流准入、receiver completion 以及故障后恢复结果仍为 UNKNOWN。']
    (output/'representative-cases.md').write_text('\n'.join(text)+'\n')
    return summary
