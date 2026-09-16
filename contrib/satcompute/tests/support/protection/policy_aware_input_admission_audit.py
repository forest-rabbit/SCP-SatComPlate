"""Policy-aware START admission development audit; no selector fitting or tuning."""
from collections import Counter
import json
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
BASE = runpy.run_path(str(HERE/'multitree_comparison_audit.py'))
ROWS, REQUIRE = BASE['rows'], BASE['require']


def _boolean(value):
    REQUIRE(value in ('0', '1'), 'invalid policy-aware boolean')
    return value == '1'


def _policy(rows):
    by_stage = Counter()
    snapshots = {}
    final = []
    for row in rows:
        send = row['selective_dryrun_decision'] == 'SEND'
        REQUIRE(send or row['selective_dryrun_decision'] == 'DEFER', 'invalid Selective decision')
        legacy = _boolean(row['legacy_deadline_feasible'])
        aware = _boolean(row['policy_aware_deadline_feasible'])
        rescued = _boolean(row['rescued_by_policy_aware_input'])
        start = _boolean(row['start_committed'])
        REQUIRE(float(row['policy_aware_input_admission_s']) >= 0 and
                float(row['legacy_fault_input_s']) >= float(row['policy_aware_input_admission_s']),
                'invalid policy-aware INPUT admission term')
        if send:
            REQUIRE(float(row['policy_aware_input_admission_s']) == 0,
                    'SEND retained the full Deferred INPUT term')
        else:
            REQUIRE(float(row['policy_aware_input_admission_s']) == float(row['legacy_fault_input_s']),
                    'DEFER changed the legacy INPUT term')
        if rescued:
            REQUIRE(send and not legacy and aware, 'invalid policy-aware rescue classification')
        key = (row['task_id'], row['time_ns'], row['local'], row['remote'])
        value = (row['selective_dryrun_decision'], row['selective_pf'],
                 row['selective_u_ser_pot'], row['selective_t_ser_s'])
        REQUIRE(key not in snapshots or snapshots[key] == value,
                'same causal candidate received different Selective decisions')
        snapshots[key] = value
        by_stage[(row['stage'], row['selective_dryrun_decision'])] += 1
        if row['stage'] == 'FINAL_REVALIDATION':
            REQUIRE(_boolean(row['is_final_pair']) and _boolean(row['final_pair_revalidated']),
                    'final actual pair was not revalidated')
            final.append(row)
        if start:
            REQUIRE(row['runtime_prefetch_admission_success'] in ('', '0', '1'),
                    'invalid runtime prefetch admission result')
    final_tasks = {row['task_id'] for row in final}
    REQUIRE(len(final_tasks) == len(final), 'multiple final revalidations for one START task')
    unique_send = sum(value[0] == 'SEND' for value in snapshots.values())
    runtime_fail = [row for row in final if _boolean(row['start_committed']) and
                    row['selective_dryrun_decision'] == 'SEND' and
                    row['runtime_prefetch_admission_success'] == '0']
    rescued_start = {row['task_id'] for row in final if _boolean(row['start_committed']) and
                     _boolean(row['rescued_by_policy_aware_input'])}
    return {
        'rows': len(rows),
        'unique_candidate_snapshots': len(snapshots),
        'unique_SEND_candidate_snapshots': unique_send,
        'unique_DEFER_candidate_snapshots': len(snapshots)-unique_send,
        'by_stage_and_decision': {f'{stage}:{decision}': count
                                  for (stage, decision), count in sorted(by_stage.items())},
        'rescued_candidate_rows': sum(_boolean(row['rescued_by_policy_aware_input']) for row in rows),
        'rescued_candidate_snapshots': len({(row['task_id'], row['time_ns'], row['local'], row['remote'])
                                            for row in rows
                                            if _boolean(row['rescued_by_policy_aware_input'])}),
        'final_actual_pairs': len(final),
        'final_SEND': sum(row['selective_dryrun_decision'] == 'SEND' for row in final),
        'final_DEFER': sum(row['selective_dryrun_decision'] == 'DEFER' for row in final),
        'rescued_START_tasks': len(rescued_start),
        'rescued_START_task_ids': sorted(map(int, rescued_start)),
        'runtime_prefetch_admission_failures': len(runtime_fail),
        'runtime_prefetch_admission_failure_task_ids': sorted(int(row['task_id']) for row in runtime_fail),
    }


def analyze(root):
    root = Path(root)
    result = BASE['audit'](root, allow_development=True)
    policy = _policy(ROWS(root, 'compfrr-policy-aware-admission.csv'))
    decisions = ROWS(root, 'frequency-decisions.csv')
    impacts = ROWS(root, 'fault-task-impact.csv')
    starts = {row['task_id'] for row in decisions
              if row['proposed_action'] == 'START' and row['decision_committed'] == '1'}
    faulted = {row['task_id'] for row in impacts
               if row['impact_type'].startswith('RUNNING_INTERRUPTED')}
    prefetch = result['compfrr']['prefetch']
    summary = result['summary']
    return {
        'status': 'PASS',
        'execution': result['execution'],
        'completion': {'completed': summary['completed'], 'tasks': summary['tasks'],
                       'failed_task_ids': summary['failed_task_ids'],
                       'failure_reasons': summary['failure_reasons']},
        'START_count': len(starts),
        'faulted_primary_task_count': len(faulted),
        'faulted_primary_task_ids': sorted(map(int, faulted)),
        'catch_ms': summary['fault_to_catch_ms'],
        'FT_traffic_bytes': summary['extra_sent_bytes'],
        'prefetch_INPUT_bytes': prefetch['B_prefetch_total'] if prefetch else 0,
        'recovery_INPUT_bytes': result['compfrr']['recovery_INPUT_sent_bytes'],
        'total_waste_eq_wu': summary['w_waste_actual'],
        'busiest_link_average_utilization_percent': summary['max_link_whole_run_utilization_percent'],
        'mean_link_utilization_percent': summary['mean_link_utilization_percent'],
        'policy_aware_admission': policy,
    }


def compare(root):
    root = Path(root)
    results = {name: analyze(root/name) for name in ('5Gbps-run11', '10Gbps-run11')}
    bandwidths = {name: value['execution']['isl_bandwidth_bps'] for name, value in results.items()}
    REQUIRE(bandwidths == {'5Gbps-run11': 5_000_000_000,
                           '10Gbps-run11': 10_000_000_000},
            'development comparison bandwidth identity changed')
    output = {'status': 'PASS', 'development_only': True,
              'scope': 'CompFRR-P/CUMULATIVE/Selective/Relocate, seed1/run11; no baseline matrix.',
              'results': results}
    (root/'development-comparison.json').write_text(json.dumps(output, indent=2)+'\n')
    return output
