#!/usr/bin/env python3
"""Offline evidence audit of frozen 1Gbps START rejects. Never launch a simulator."""
import argparse
from collections import Counter
import csv
from fractions import Fraction
import json
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[5]
SUPPORT = ROOT/'contrib/satcompute/tests/support/protection'
BASE = runpy.run_path(str(SUPPORT/'multitree_comparison_audit.py'))
FREQUENCY = runpy.run_path(str(SUPPORT/'frequency_audit.py'))
rows, require = BASE['rows'], BASE['require']
NS = 10**9
# Evidence identity, not a hash-integrity mechanism. This execution's compiled
# fault-para.cc fixes checkIntervalSeconds=1.0; the platform has no cadence CLI.
FROZEN_EXECUTION_COMMIT = 'ebd8a4748893d538c85f0602ab207b74971c93ac'
UNKNOWN = 'UNKNOWN_INSUFFICIENT_EVIDENCE'
HARD_REASONS = {'DEADLINE_INFEASIBLE', 'INITIALIZATION_TOO_LATE', 'STORAGE_INFEASIBLE',
                'NO_CAPACITY_NOW', 'NO_ROUTE', 'NO_FEASIBLE_NODE_PAIR',
                'PLACEMENT_UNAVAILABLE', 'PATH_ADMISSION_UNAVAILABLE'}


def key(row):
    return row['task_id'], int(row['fault_epoch_time_ns']), row['decision_trigger']


def indexed(records):
    out = {}
    for line, record in enumerate(records, 2):
        k = key(record)
        require(k not in out, 'duplicate decision event identity')
        out[k] = (line, record)
    return out


def ceiling(value):
    return -(-value.numerator // value.denominator)


def number(text):
    """Recover the recorded C++ double, not a decimal reinterpretation of it."""
    return Fraction.from_float(float(text))


def selector_bounds(p, serialization_ns, first_lead_ns, last_lead_ns):
    """Bound existing SER sum(w*min(Tser,lead)); do not generate a risk trajectory.

    1e-12 is the existing production trajectory/P_F representation validation
    allowance, NOT a tunable threshold or decision epsilon. Bounds widen by this
    allowance; production's strict greater-than decision stays unchanged.
    """
    require(0 <= p <= 1 and 0 <= first_lead_ns <= last_lead_ns and serialization_ns > 0,
            'invalid selector bound domain')
    allowance = Fraction(1, 10**12)
    mass_low, mass_high = max(0, p-allowance), min(1, p+allowance)
    low = mass_low*min(serialization_ns, first_lead_ns)
    high = mass_high*min(serialization_ns, last_lead_ns)
    cost = (1-p)*serialization_ns
    decision = 'SEND' if low > cost else 'DEFER' if high <= cost else 'UNKNOWN'
    return decision, low, high, cost


def reference_selector(row, task, interval_ns):
    """Conditional audit of THIS recorded reference at THIS event, not all targets.

    FAULT_EPOCH's pre-batch inclusive trajectory is not Selective's post-batch
    QueryTaskPrediction trajectory. We intentionally do not translate it here.
    Initial TASK_RUNNING at progress zero has the identical exclusive query window.
    """
    require(interval_ns > 0, 'invalid sampling interval')
    result = dict(selector_decision='UNKNOWN', selector_evidence='MISSING_CAUSAL_QUERY_SNAPSHOT',
        target_scope='RECORDED_REFERENCE_ONLY', first_sample_ns=row['first_sample_time_ns'],
        finish_exclusive='UNKNOWN', serialization_estimate_ns=None, gain_lower_ns=None,
        gain_upper_ns=None, cost_ns=None, full_future_steps_available=False,
        hypothetical_actual_pair='UNKNOWN')
    if (row['decision_trigger'] != 'TASK_RUNNING' or int(row['progress_work']) != 0 or
            int(row['fault_epoch_time_ns']) != int(task['compute_start_time_ns'])):
        return result
    result['finish_exclusive'] = True
    if not row['remote_node'] or not row['input_bandwidth_bytes_per_s']:
        result['selector_evidence'] = 'REFERENCE_PATH_ESTIMATE_UNAVAILABLE'
        return result
    if row['remote_node'] == task['source_node_id']:
        result.update(selector_decision='SEND', selector_evidence='CONDITIONAL_LOCAL_DELIVERY',
                      serialization_estimate_ns=0)
        return result
    bandwidth = number(row['input_bandwidth_bytes_per_s'])
    require(bandwidth > 0, 'invalid logged INPUT bandwidth')
    serialization = ceiling(Fraction(int(task['input_bytes'])*NS)/bandwidth)
    start = int(row['fault_epoch_time_ns'])
    first = int(row['first_sample_time_ns'])
    duration = ceiling(Fraction(int(task['compute_work_units'])*NS,
                                int(task['compute_rate_work_units_per_second'])))
    finish = start + duration
    if first < start or first >= finish:
        result['selector_evidence'] = 'EMPTY_OR_UNSUPPORTED_QUERY_WINDOW'
        return result
    last = first + ((finish-1-first)//interval_ns)*interval_ns
    decision, low, high, cost = selector_bounds(number(row['p_fail_before_finish']),
                                                serialization, first-start, last-start)
    result.update(selector_decision=decision,
        selector_evidence='PRODUCTION_SER_MASS_AND_TIME_BOUNDS' if decision != 'UNKNOWN'
                          else 'BOUNDS_STRADDLE_STRICT_SER_COMPARISON',
        serialization_estimate_ns=serialization, gain_lower_ns=float(low),
        gain_upper_ns=float(high), cost_ns=float(cost))
    return result


def candidate_conclusion(row):
    """No candidate identities/resource snapshots can be invented from counts."""
    paths = int(row['pair_path_feasible'] or 0)
    checked = int(row['pair_hard_checked'] or 0)
    require(0 <= checked <= paths, 'invalid checked candidate count')
    if paths == 0:
        return 'PROVEN_NO_ADMISSIBLE_PAIR_AT_THIS_EVENT'
    if checked == paths and row['proposal_reason'] in HARD_REASONS:
        return 'PROVEN_EXHAUSTED_AT_THIS_EVENT'
    return UNKNOWN


def event_context(row, task, line):
    return dict(task_id=int(row['task_id']), time_ns=int(row['fault_epoch_time_ns']),
        trigger=row['decision_trigger'], source_csv_row=line, profile=task['task_profile'],
        input_bytes=int(task['input_bytes']), source_node=int(task['source_node_id']),
        primary_node=int(task['compute_node_id']))


def summarize_selector(records):
    decisions = Counter(r['selector_decision'] for r in records)
    classes = Counter(r['classification'] for r in records)
    return dict(tasks_or_events=len(records), conditional_reference_selector=dict(decisions),
        conditional_classification=dict(classes),
        N_provable_selective_false_rejection=classes['PROVABLE_FALSE_REJECTION'],
        N_selective_in_flight=None, N_selective_in_flight_proven=0,
        N_selective_still_infeasible=None, N_selective_still_infeasible_proven=0,
        N_selective_would_defer=classes['SELECTIVE_WOULD_DEFER'], N_unknown=classes[UNKNOWN],
        hypothetical_actual_pair_known=0,
        warning='DEFER concerns this reference/event only; not all candidates or later retries. '
                'IN_FLIGHT is a state flag, not a mutually exclusive outcome. No hypothetical flow exists.')


def analyze(base):
    # Existing actual-ledger and execution-identity gates; this does NOT simulate.
    audited = BASE['comparison'](base, ('compfrr-p', 'compfrr-fa-ffp'))
    common = audited['common_runtime_inputs']
    require((common['islBandwidthBps'], common['randomSeed'], common['randomRun']) ==
            ('1000000000', '1', '11'), 'requires frozen 1Gbps seed1/run11')
    require(audited['commit'] == FROZEN_EXECUTION_COMMIT, 'unreviewed execution contract')
    interval_ns = NS
    pdir, fdir = base/'compfrr-p', base/'compfrr-fa-ffp'
    decisions, fdecisions = rows(pdir, 'frequency-decisions.csv'), rows(fdir, 'frequency-decisions.csv')
    for directory, records in ((pdir, decisions), (fdir, fdecisions)):
        FREQUENCY['verify_pair_retries'](records, rows(directory, 'frequency-capacity-waits.csv'))
        require(all(int(r['fault_epoch_time_ns']) % interval_ns == 0 for r in records
                    if r['decision_trigger']=='FAULT_EPOCH'), 'recorded epoch grid changed')
    tasks = {r['task_id']: r for r in rows(pdir, 'task-summary.csv')}
    ftasks = {r['task_id']: r for r in rows(fdir, 'task-summary.csv')}
    protected = {r['task_id']: r for r in rows(pdir, 'protection-task-summary.csv')}
    fprotected = {r['task_id']: r for r in rows(fdir, 'protection-task-summary.csv')}
    recovery = {r['task_id']: r for r in rows(pdir, 'recovery-summary.csv')}
    fidx, pidx = indexed(fdecisions), indexed(decisions)
    off = [r for r in decisions if r['phase_before'] == 'OFF']
    rejected = [r for r in off if r['proposal_reason'] in HARD_REASONS]
    reject_ids = {r['task_id'] for r in rejected}
    starts = {r['task_id']: r for r in fdecisions if r['proposed_action'] == 'START' and r['decision_committed'] == '1'}
    cohort = {k for k, r in recovery.items() if r['phase_at_fault'] == 'OFF' and r['terminal_state'] == 'FAILED'}
    require(len(cohort) == 69 and all(k not in protected for k in cohort), 'frozen 69-task cohort changed')
    require(not (cohort & {r['task_id'] for r in rows(pdir, 'input-admission-decisions.csv')}),
            'unprotected fault cohort unexpectedly has actual Selective snapshots')
    candidates, comparisons, selective = [], [], []
    first_reject = {}
    for row in rejected:
        k = row['task_id']; task = tasks[k]; line = pidx[key(row)][0]
        context = event_context(row, task, line)
        matched_line, matched = fidx.get(key(row), (None, {}))
        first_reject.setdefault(k, key(row))
        paths, checked = int(row['pair_path_feasible'] or 0), int(row['pair_hard_checked'] or 0)
        require(checked <= 1, 'P is no longer the frozen single-reference contract')
        conclusion = candidate_conclusion(row)
        fa_start = starts.get(k, {})
        same_start = bool(matched and matched['proposed_action'] == 'START' and matched['decision_committed'] == '1')
        serialization = (float(Fraction(int(task['input_bytes']) * NS)/number(row['input_bandwidth_bytes_per_s']))
                         if row['input_bandwidth_bytes_per_s'] else None)
        slack = float(row['rmax_s'])*NS if row['rmax_s'] else None
        comparisons.append(dict(context, reference_local=row['local_node'], reference_remote=row['remote_node'],
            reference_reject_reason=row['proposal_reason'], raw_pair_count=int(row['pair_candidates_total'] or 0),
            node_feasible_pair_count=int(row['pair_node_feasible'] or 0), path_feasible_pair_count=paths,
            frequency_checked_pair_count=checked, unchecked_path_feasible_pairs=paths-checked,
            full_alternative_identity_snapshot_available=False, candidate_conclusion=conclusion,
            has_alternative_feasible_candidate='UNKNOWN' if conclusion == UNKNOWN else 'FALSE',
            same_fixed_config_alternative='UNKNOWN', exists_other_config_alternative='UNKNOWN',
            reference_selected_delta=row['proposed_delta_permille'], reference_selected_n=row['proposed_n'],
            input_serialization_estimate_ns=serialization, deadline_slack_ns=slack,
            input_alone_exceeds_slack=serialization > slack if serialization is not None and slack is not None else None,
            actual_fault_hit=row['actual_fault_hit'], in_original_69=k in cohort,
            p_eventually_started=k in protected, p_completed=task['task_success']=='1',
            fa_completed=ftasks[k]['task_success']=='1', fa_matched_csv_row=matched_line,
            fa_matched_phase=matched.get('phase_before', ''), fa_start_at_matching_event=same_start,
            fa_checked_at_matching_event=matched.get('pair_hard_checked', ''),
            fa_first_start_ns=fa_start.get('fault_epoch_time_ns', ''),
            fa_first_start_local=fa_start.get('local_node', ''), fa_first_start_remote=fa_start.get('remote_node', ''),
            fa_first_start_same_as_source=fa_start.get('remote_node') == task['source_node_id'] if fa_start else None,
            cross_run_full_causal_snapshot_equivalent='UNKNOWN'))
        def entry(kind, count, local='', remote='', scope='P_EVENT'):
            return dict(context, candidate_kind=kind, count=count, local_node=local, remote_node=remote,
                same_as_input_source=remote==task['source_node_id'] if remote else None,
                evidence_scope=scope, candidate_path='UNKNOWN_UNRECORDED_HOPS', compute_feasible='UNKNOWN',
                path_feasible='UNKNOWN', storage_feasible='UNKNOWN', deadline_feasible='UNKNOWN',
                risk_feasible='NOT_A_SEPARATE_HARD_THRESHOLD', selected_delta='', selected_n='',
                input_transfer_estimate_ns=None, other_hard_constraints='UNKNOWN',
                candidate_feasible='UNKNOWN', reason='MISSING_CANDIDATE_SNAPSHOT', source_fa_csv_row=None)
        if checked:
            e = entry('RECORDED_REFERENCE', 1, row['local_node'], row['remote_node'])
            e.update(compute_feasible='TRUE', path_feasible='TRUE', candidate_feasible='FALSE',
                     reason=row['proposal_reason'], selected_delta=row['proposed_delta_permille'], selected_n=row['proposed_n'],
                     input_transfer_estimate_ns=serialization)
            if row['proposal_reason']=='DEADLINE_INFEASIBLE': e['deadline_feasible']='FALSE'
            if row['proposal_reason']=='STORAGE_INFEASIBLE': e['storage_feasible']='FALSE'
            if row['proposal_reason']=='INITIALIZATION_TOO_LATE': e['other_hard_constraints']='INITIALIZATION_TOO_LATE'
            candidates.append(e)
        if paths > checked:
            e = entry('UNRECORDED_ALTERNATIVES_AGGREGATE_NOT_INDIVIDUAL_NODES', paths-checked)
            e.update(compute_feasible='TRUE_AGGREGATE_ONLY', path_feasible='TRUE_AGGREGATE_ONLY')
            candidates.append(e)
        if not paths:
            e = entry('EMPTY_PATH_FEASIBLE_SET', 0)
            e.update(candidate_feasible='FALSE', reason=row['proposal_reason'])
            candidates.append(e)
        if same_start:
            e = entry('FA_ACTUAL_ADMITTED_PAIR', 1, matched['local_node'], matched['remote_node'], 'FA_EVENT_ONLY')
            e.update(compute_feasible='TRUE', path_feasible='TRUE', storage_feasible='TRUE', deadline_feasible='TRUE',
                candidate_feasible='TRUE_IN_FA_NOT_PROVEN_IN_P', reason='ACTUAL_START',
                selected_delta=matched['committed_delta_permille'], selected_n=matched['committed_n'],
                source_fa_csv_row=matched_line)
            candidates.append(e)
        bound = reference_selector(row, task, interval_ns)
        fault = recovery.get(k)
        fault_time = int(fault['fault_time_ns']) if fault else None
        minimum_serial = ceiling(Fraction(int(task['input_bytes'])*8*NS, 10**9))
        cannot_ready = (fault_time is not None and row['remote_node'] and row['remote_node'] != task['source_node_id']
                        and 0 <= fault_time-context['time_ns'] < minimum_serial)
        selective.append(dict(context, reference_local=row['local_node'], reference_remote=row['remote_node'],
            **bound, in_original_69=k in cohort, actual_fault_time_ns=fault_time,
            fault_outcome_used_in_selector=False, proposed_start_survives_fault_batch=row['actual_fault_hit']!='1',
            physical_serialization_lower_bound_ns=minimum_serial if row['remote_node'] != task['source_node_id'] else 0,
            input_cannot_be_ready_by_observed_fault_if_sent_now=bool(cannot_ready),
            hypothetical_input_status='NOT_READY_BY_PHYSICAL_LOWER_BOUND' if cannot_ready else 'UNKNOWN',
            hypothetical_checkpoint_status='UNKNOWN', hypothetical_receiver_completion_ns=None,
            hypothetical_full_recovery_feasible='UNKNOWN',
            classification='SELECTIVE_WOULD_DEFER' if bound['selector_decision']=='DEFER' else UNKNOWN,
            classification_scope='UNCHANGED_REFERENCE_AT_THIS_EVENT_ONLY'))
    by_event = {(str(r['task_id']), r['time_ns'], r['trigger']): r for r in selective}
    first_rows = [by_event[k] for k in first_reject.values()]
    cohort_rows = [r for r in first_rows if r['in_original_69']]
    require(len(cohort_rows)==69 and all(r['trigger']=='TASK_RUNNING' for r in cohort_rows), 'first rejected cohort event changed')
    # Fault state is observed separately from the impossible-to-observe hypothetical flow.
    fault_states = []
    for k, r in recovery.items():
        fault_states.append(dict(task_id=int(k), fault_time_ns=int(r['fault_time_ns']),
            original_69=k in cohort, task_state_at_fault='RUNNING', phase_at_fault=r['phase_at_fault'],
            observed_checkpoint_exists=r['checkpoint_state_exists'],
            observed_local_work_units=r['local_work_units'], observed_remote_work_units=r['remote_work_units'],
            observed_proactive_input='ABSENT_NO_ADMISSION' if k in cohort else 'SEE_ACTUAL_PREFETCH_LEDGER',
            hypothetical_input_status='UNKNOWN', hypothetical_checkpoint_status='UNKNOWN',
            hypothetical_remaining_wait_ns=None, actual_recovery_path=r['chosen_path'], terminal_reason=r['terminal_reason']))
    for k, t in tasks.items():
        if t['failure_reason']=='COMPUTE_SATELLITE_FAILURE' and int(t['compute_start_time_ns']) < 0:
            fault_states.append(dict(task_id=int(k), fault_time_ns=int(t['failure_time_ns']),
                original_69=False, task_state_at_fault='INPUT_TRANSFERRING', phase_at_fault='NOT_RUNNING',
                observed_checkpoint_exists='0', observed_local_work_units='0', observed_remote_work_units='0',
                observed_proactive_input='ABSENT_NO_START', hypothetical_input_status='NOT_APPLICABLE',
                hypothetical_checkpoint_status='NOT_APPLICABLE', hypothetical_remaining_wait_ns=None,
                actual_recovery_path='NOT_APPLICABLE', terminal_reason=t['failure_reason']))
    pfailed, ffailed = set(map(str, audited['groups']['compfrr-p']['summary']['failed_task_ids'])), set(map(str, audited['groups']['compfrr-fa-ffp']['summary']['failed_task_ids']))
    rescued, reverse = pfailed-ffailed, ffailed-pfailed
    pc = {r['task_id']:r for r in audited['groups']['compfrr-p']['catches']}
    fc = {r['task_id']:r for r in audited['groups']['compfrr-fa-ffp']['catches']}
    same = {k for k in pc.keys() & fc.keys() if BASE['same_primary_fault'](pc[k], fc[k])}
    summary = dict(status='COUNTERFACTUAL_EVIDENCE_INCOMPLETE', stage='OFFLINE_STAGE_1_STOP_FOR_REVIEW',
        production_changes=False, new_simulations=0, audited_execution_commit=audited['commit'],
        source_roots=dict(p=str(pdir), fa=str(fdir)), execution_ledger_audit='PASS',
        event_counts=dict(off_decisions=len(off), all_off_non_start=sum(r['proposed_action']!='START' for r in off),
            N_total_start_rejected=len(rejected), N_reference_candidate_infeasible=sum(int(r['pair_hard_checked']or 0)>0 for r in rejected),
            N_has_alternative_feasible_candidate=None, N_no_feasible_candidate_found=None,
            N_has_alternative_feasible_candidate_proven=0,
            N_no_feasible_candidate_proven=sum(r['candidate_conclusion']!=UNKNOWN for r in comparisons),
            N_candidate_unknown=sum(r['candidate_conclusion']==UNKNOWN for r in comparisons),
            unchecked_path_feasible_pairs_across_events=sum(r['unchecked_path_feasible_pairs'] for r in comparisons),
            fa_actual_start_at_matched_event=sum(r['fa_start_at_matching_event'] for r in comparisons),
            pre_batch_reject_events_hit_by_actual_fault=sum(r['actual_fault_hit']=='1' for r in rejected),
            triggers=dict(Counter(r['decision_trigger'] for r in rejected)),
            rejection_reasons=dict(Counter(r['proposal_reason'] for r in rejected))),
        task_counts=dict(total=len(tasks), ever_hard_rejected=len(reject_ids),
            rejected_then_later_started=len(reject_ids & protected.keys()), never_started_among_rejected=len(reject_ids-protected.keys()),
            original_failed_off_cohort=len(cohort), fa_started_among_p_rejected=len(reject_ids & fprotected.keys()),
            observed_p_completed=audited['groups']['compfrr-p']['summary']['completed'],
            observed_fa_completed=audited['groups']['compfrr-fa-ffp']['summary']['completed'],
            fa_only_completed=sorted(map(int,rescued)), p_only_completed=sorted(map(int,reverse)),
            fa_only_completed_with_same_primary_fault=len(rescued & same),
            fa_only_completed_source_equals_remote=sum(fprotected.get(k,{}).get('remote_node')==tasks[k]['source_node_id'] for k in rescued)),
        reference_coverage_loss=dict(point_estimate=None, proven_lower_bound=0, unresolved_upper_bound=1,
            reason='0 proven alternatives is not 0 true alternatives; skipped identities and resources were not logged.'),
        selective_all_events=summarize_selector(selective), selective_first_rejection_per_task=summarize_selector(first_rows),
        selective_original_69_first_running=summarize_selector(cohort_rows),
        original_69_cannot_input_ready_before_fault_by_serialization_lower_bound=sum(r['input_cannot_be_ready_by_observed_fault_if_sent_now'] for r in cohort_rows),
        saveable_tasks=dict(point_estimate=None, reason='No hypothetical checkpoint/flow/DAG evidence; cannot infer rescued count from net completion gap.'),
        missing_evidence=['Rejected START actual pair does not exist; skipped pair identities and complete per-pair resources are absent.',
            'Full canonical future steps and post-batch Selective snapshots were not logged for rejected STARTs.',
            'Counterfactual flow admission, queueing, receiver completion and checkpoint establishment never occurred.',
            'FA counterpart is an independent online execution, not the P decision snapshot.'],
        selector_bound_scope='Existing SER on unchanged recorded reference at initial TASK_RUNNING only. '
            'Uses probability/time bounds, not an invented trajectory. 1e-12 is existing representation validation only.',
        sampling_interval_ns=interval_ns,
        sampling_interval_source='Compiled fault/fault-para.cc at audited execution commit; no cadence CLI override.',
        fixed_vs_other_config='No selected configuration for a rejected Frequency search; neither a fixed-config alternative '
            'nor existence of another feasible configuration is certified by the skipped-pair counts.')
    return summary, dict(candidate_audit=candidates, reference_vs_alternatives=comparisons,
                         selective_evidence=selective, fault_state_evidence=fault_states)


def write_csv(path, records):
    require(bool(records), 'empty required evidence table')
    with path.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(records[0]))
        writer.writeheader(); writer.writerows(records)


def representative_markdown(summary, tables):
    lines = ['# 代表案例（只读历史证据）', '',
        '候选列表中 aggregate 行是未记录身份的数量，不是假造的逐节点快照。',
        'FA 的成功只在 FA 的运行中成立；同一任务/时刻/故障不代表整个资源快照相同。', '',
        '| task | P 首次硬拒绝 ns | reference local/remote | FA 首次 START ns | FA local/remote | reference SER 界 | 分类 |',
        '| --- | ---: | --- | ---: | --- | --- | --- |']
    for task in (5, 53, 158, 574):
        r = next(x for x in tables['reference_vs_alternatives'] if x['task_id']==task)
        s = next(x for x in tables['selective_evidence'] if x['task_id']==task)
        lines.append(f"| {task} | {r['time_ns']} | {r['reference_local']}/{r['reference_remote']} | "
            f"{r['fa_first_start_ns']} | {r['fa_first_start_local']}/{r['fa_first_start_remote']} | "
            f"{s['selector_decision']} | {s['classification']} |")
    lines += ['', 'task 120 在 INPUT 阶段遭 F3，不属于 69 个计算中失败；task 302 是 FA 失败、P 完成的反向案例。',
        'task 158 的两个 NO_CAPACITY_NOW 事件已穷尽当时路径准入，但不证明其他时刻也无候选。',
        'UNKNOWN 合法保留；没有启动新仿真，也没有宣布无需修复或已修复。', '']
    return '\n'.join(lines)


def inventory(directories):
    return {str(p): (p.stat().st_size, p.stat().st_mtime_ns) for d in directories for p in d.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, default=ROOT/'output/multitree/bandwidth/1Gbps-run11')
    parser.add_argument('--output', type=Path, default=ROOT/'output/compfrr/1g-candidate-feasibility-audit')
    args = parser.parse_args()
    base, output = args.base.resolve(), args.output.resolve()
    require(not output.exists() and not output.is_relative_to(base) and not base.is_relative_to(output),
            'refuse to overwrite audit/raw execution evidence')
    sources = [base/'compfrr-p', base/'compfrr-fa-ffp']
    before = inventory(sources)
    summary, tables = analyze(base)
    require(before == inventory(sources), 'raw evidence changed during offline audit')
    summary['raw_files_unchanged'] = len(before)
    output.mkdir(parents=True, exist_ok=False)
    for name, records in tables.items():
        write_csv(output/(name.replace('_','-')+'.csv'), records)
    (output/'classification-summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False)+'\n')
    (output/'representative-cases.md').write_text(representative_markdown(summary, tables))
    print(json.dumps(summary, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
