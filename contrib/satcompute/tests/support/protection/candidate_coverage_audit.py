"""Fixed-local anchor coverage accounting, not a Frequency solver or new selector."""
from collections import Counter
from pathlib import Path
import runpy

FREQUENCY = runpy.run_path(str(Path(__file__).with_name('frequency_audit.py')))
rows, require, stats = FREQUENCY['rows'], FREQUENCY['require'], FREQUENCY['stats']
HARD = {'STORAGE_INFEASIBLE', 'DEADLINE_INFEASIBLE', 'INITIALIZATION_TOO_LATE'}


def validate(records, decisions):
    def key(r): return r['task_id'], int(r.get('time_ns', r.get('fault_epoch_time_ns'))), r['decision_trigger']
    index = {key(r): r for r in decisions if r['phase_before']=='OFF'}
    require(len({key(r) for r in records})==len(records), 'duplicate coverage event')
    # Malformed early input may not enter pair search; only node/path-count rows have this audit.
    require(set(map(key, records)) == {k for k,r in index.items() if r['pair_candidates_total']},
            'P OFF candidate audit population differs')
    for r in records:
        f=index[key(r)]
        total, checked = int(r['remote_candidates_total']), int(r['remote_candidates_checked'])
        anchor=int(r['first_feasible_anchor_index']) if r['first_feasible_anchor_index'] else None
        exhausted=r['all_candidates_infeasible']=='1'
        require(0<=checked<=total<=int(f['pair_path_feasible']), 'fixed-local candidate count mismatch')
        require(checked==int(f['pair_hard_checked']), 'solver call count differs')
        require(exhausted==(anchor is None) and (not exhausted or checked==total), 'incomplete candidate exhaustion')
        require(checked<=1 or r['reference_reject_reason'] in HARD, 'fallback without reference hard rejection')
        if anchor:
            require(anchor==checked and int(f['pair_hard_feasible'])==1 and bool(r['feasible_anchor_remote']),
                    'search continued after first feasible anchor')
            require((anchor==1)==(not r['reference_reject_reason']), 'reference success classification differs')
        else:
            require(not r['feasible_anchor_remote'] and not r['final_selected_remote'] and
                    f['proposed_action']=='NONE', 'exhausted set selected a remote')
        if r['final_selected_remote']:
            require(anchor and r['reference_local']==f['local_node'] and
                    r['final_selected_remote']==f['remote_node'] and f['proposed_action']=='START',
                    'P ranking changed fixed local or mismatched final remote')
        for a,b in (('proposed_action','proposed_action'),('proposal_reason','proposal_reason'),
                    ('actual_fault_hit','actual_fault_hit'),('decision_committed','decision_committed'),
                    ('anchor_delta_permille','proposed_delta_permille'),('anchor_n','proposed_n'),
                    ('resolution_reason','reason')):
            require(r[a]==f[b], 'coverage/Frequency resolution mismatch')
    fallback=[r for r in records if int(r['remote_candidates_checked'])>1]
    anchors=[r for r in records if r['first_feasible_anchor_index'] and int(r['first_feasible_anchor_index'])>1]
    admitted=[r for r in anchors if r['decision_committed']=='1' and r['proposed_action']=='START']
    return dict(events=len(records), tasks=len({r['task_id'] for r in records}),
        first_reference_success_count=sum(r['first_feasible_anchor_index']=='1' for r in records),
        fallback_used_count=len(fallback), fallback_used_tasks=len({r['task_id'] for r in fallback}),
        fallback_depth=stats([int(r['remote_candidates_checked'])-1 for r in fallback]),
        successful_anchor_fallback_depth=stats([int(r['first_feasible_anchor_index'])-1 for r in anchors]),
        fallback_definition='additional remotes checked after original reference; all fallback events, including exhaustion',
        all_remotes_infeasible_count=sum(r['all_candidates_infeasible']=='1' for r in records),
        empty_path_feasible_set_count=sum(int(r['remote_candidates_total'])==0 for r in records),
        reference_failed_alternate_feasible_events=len(anchors),
        reference_failed_alternate_feasible_tasks=len({r['task_id'] for r in anchors}),
        fallback_committed_start_tasks=len({r['task_id'] for r in admitted}),
        fallback_anchor_fault_hit_count=sum(r['actual_fault_hit']=='1' for r in anchors),
        reference_reject_reasons=dict(Counter(r['reference_reject_reason'] for r in records if r['reference_reject_reason'])))


def analyze(root):
    decisions=rows(root,'frequency-decisions.csv')
    FREQUENCY['verify_pair_retries'](decisions,rows(root,'frequency-capacity-waits.csv'))
    result=validate(rows(root,'compfrr-candidate-coverage.csv'),decisions)
    tasks=rows(root,'task-summary.csv'); recoveries=rows(root,'recovery-summary.csv')
    starts={r['task_id'] for r in decisions if r['proposed_action']=='START' and r['decision_committed']=='1'}
    result.update(start_tasks=len(starts), never_started_tasks=len(tasks)-len(starts),
        completed_tasks=sum(r['task_success']=='1' for r in tasks),
        off_proposal_reasons=dict(Counter(r['proposal_reason'] for r in decisions if r['phase_before']=='OFF')),
        remaining_off_faults=[{k:r[k] for k in ('task_id','fault_time_ns','phase_at_fault','chosen_path','terminal_state','terminal_reason')}
                             for r in recoveries if r['phase_at_fault']=='OFF'])
    result['never_started_first_reason']={}
    for r in decisions:
        if r['task_id'] not in starts:
            result['never_started_first_reason'].setdefault(r['task_id'],r['proposal_reason'])
    result['never_started_first_reason_counts']=dict(Counter(result['never_started_first_reason'].values()))
    last={r['task_id']:r['proposal_reason'] for r in decisions if r['task_id'] not in starts}
    result['never_started_last_proposal_reason_counts']=dict(Counter(last.values()))
    result['never_evaluated_task_ids']=sorted(
        (r['task_id'] for r in tasks if r['task_id'] not in starts and r['task_id'] not in last),key=int)
    result['status']='PASS'
    return result
