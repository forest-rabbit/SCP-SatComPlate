#!/usr/bin/env python3
"""Five-run U comparison under corrected recovery, including both signed tail exclusions."""
import argparse
from collections import Counter
import json
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
RUN = runpy.run_path(str(HERE / 'run-recovery-u-revalidation.py'))
FIX = runpy.run_path(str(HERE / 'analyze-recovery-deadline-reruns.py'))
MULTI = runpy.run_path(str(HERE / 'analyze-n5c-rational-multirun.py'))
ROOT, GROUPS = RUN['ROOT'], RUN['GROUPS']
V4, OLD, RAT = MULTI['V4'], MULTI['OLD'], MULTI['RAT']
rows, require = MULTI['rows'], MULTI['require']


def signed_largest(values, positive):
    eligible = {k: v for k, v in values.items() if (v > 0 if positive else v < 0)}
    return min(eligible, key=lambda k: (-abs(eligible[k]), MULTI['instance_key'](k))) if eligible else None


def common_caught(recoveries):
    indexed = {g: MULTI['recovery_index'](recoveries[g]) for g in GROUPS}
    common = set.intersection(*(set(r) for r in indexed.values()))
    return {k for k in common if all(indexed[g][k]['actual_T_catch_ns'] for g in GROUPS)}


def contrast(accounts, recoveries, labels, caught, excluded=None):
    a, b = labels
    value = MULTI['contrast'](accounts[a], accounts[b], recoveries[a], recoveries[b], labels, excluded)
    # Whole-cohort counts/resources stay whole. Headline latency and its LOO use the
    # SAME three-way observed cohort, not a different pairwise complete-case subset.
    filtered = {g: [r for r in recoveries[g] if r['task_id'] != excluded and
        (r['task_id'], r['fault_time_ns'], r['fault_type']) in caught] for g in labels}
    value['paired_catch'] = OLD['paired_catch'](filtered[a], filtered[b], labels)
    value['paired_catch_scope'] = 'three-way common valid catch; missing is never zero'
    return value


def exclusions(accounts, recoveries, labels, caught):
    a, b = labels
    indexed = {g: MULTI['recovery_index'](recoveries[g]) for g in labels}
    catch = {k[0]: (int(indexed[a][k]['actual_T_catch_ns']) -
                    int(indexed[b][k]['actual_T_catch_ns'])) / 1e9 for k in caught}
    waste = {k: accounts[a][k]['w_waste_actual'] - accounts[b][k]['w_waste_actual'] for k in accounts[a]}
    output = []
    for metric, values in [('paired_catch_seconds', catch), ('total_eq_waste', waste)]:
        for positive in (True, False):
            chosen = signed_largest(values, positive)
            output.append(dict(metric=metric, selector='largest_positive' if positive else 'largest_negative',
                selected_instance=chosen, benefit_reference_minus_candidate=values.get(chosen),
                status='EXCLUDED_ONE_INSTANCE' if chosen else 'NO_CONTRIBUTION_OF_THIS_SIGN',
                result=contrast(accounts, recoveries, labels, caught, chosen)))
    return output


def audit(root):
    plan = json.loads((root / 'execution-plan.json').read_text())
    RUN['validate_plan'](plan, root)
    RUN['frozen_scope']()
    results, accounts, recoveries, faults = ({str(r): {} for r in RUN['RUNS']} for _ in range(4))
    table, impact, independent, signature = [], {}, {}, None
    for entry in plan['entries']:
        run, group = str(entry['run']), entry['group']
        directory, before = ROOT / entry['directory'], ROOT / entry['source']
        RUN['verify'](directory, int(run), group, plan['commit'] if entry['new'] else entry['source_commit'])
        print('AUDIT', run, group, 'NEW' if entry['new'] else 'REUSED', flush=True)
        metric, recovery, result = FIX['measure'](directory, corrected=entry['corrected'])
        tasks = rows(directory, 'task-summary.csv')
        observed = MULTI['task_signature'](tasks)
        if signature is None:
            signature = observed
        require(observed == signature, 'task inputs/rates/deadlines differ')
        require(result['summary']['tasks'] == 800 and json.loads((directory / 'run-summary.json').read_text())[
            'total_compute_work_units'] == 352513119, 'frozen workload differs')
        network = MULTI['task_network'](rows(directory, 'protection-transfers.csv'))
        require(sum(network.values()) == result['network']['extra_sent_bytes'], 'task network mismatch')
        accounts[run][group] = {f"{run}:{t['task_id']}": dict(t,
            extra_application_bytes=network.get(str(t['task_id']), 0)) for t in result.pop('task_rows')}
        for field in MULTI['FIELDS'][:-1]:
            V4['near'](sum(t[field] for t in accounts[run][group].values()), result['summary'][field], field)
        recoveries[run][group] = [dict(r, task_id=f"{run}:{r['task_id']}") for r in recovery]
        faults[run][group] = MULTI['fault_signature'](directory)
        row = OLD['summary_row'](result, int(run), group, directory)
        row.update(reused=not entry['new'], corrected_execution=entry['corrected'],
            direct_deadline_infeasible=metric['direct_deadline_infeasible'],
            relocation_attempted=metric['relocate_attempted'], relocation_success=metric['relocate_success'],
            tail=sum(r['chosen_path'] == 'TAIL' for r in recovery),
            remote_redo=sum(r['chosen_path'] == 'REMOTE_REDO' for r in recovery),
            migration_total_sent_bytes=metric['migration_actual_sent_bytes'],
            total_application_bytes=result['network']['total_physical_application_sent_bytes'],
            max_link_full_mean_percent=result['links']['max_single_link_full_mean_utilization_percent'])
        triggers = Counter(r['checkpoint_relocation_trigger'] for r in recovery if r['checkpoint_relocation_trigger'])
        for reason in ('REMOTE_BUSY', 'DIRECT_DEADLINE_INFEASIBLE', 'INPUT_PATH_UNAVAILABLE', 'OTHER'):
            row['relocate_trigger_' + reason] = (sum(v for k, v in triggers.items() if k not in
                ('REMOTE_BUSY', 'DIRECT_DEADLINE_INFEASIBLE', 'INPUT_PATH_UNAVAILABLE')) if reason == 'OTHER' else triggers[reason])
        table.append(row)
        if entry['new']:
            affected = set(entry['impact']['affected_tasks'] + entry['impact']['input_path_fallback_tasks'])
            old_recovery = rows(before, 'recovery-summary.csv')
            old_faults = MULTI['fault_signature'](before)
            impact[f'{run}:{group}'] = dict(old_execution_commit=entry['source_commit'],
                new_execution_commit=plan['commit'], reason=entry['impact'],
                prefix=FIX['prefix'](before, directory, affected),
                fault_trace_identical=json.loads((before/'fault-trace.json').read_text()) ==
                                      json.loads((directory/'fault-trace.json').read_text()),
                old_only_faults=sorted(old_faults-faults[run][group]),
                new_only_faults=sorted(faults[run][group]-old_faults),
                old_recovery_events=len(old_recovery), new_recovery_events=len(recovery),
                affected_task_details={label: [r for r in records if int(r['task_id']) in affected]
                    for label, records in [('before', old_recovery), ('after', recovery)]})
        else:
            impact[f'{run}:{group}'] = dict(reused=True, execution_commit=entry['source_commit'],
                basis='Corrected run11 already verified' if run == '11' else
                'Complete historical trace activates neither corrected branch; unchanged direct/migration '
                'arithmetic, scheduling, ownership and RNG; prior formal/fixture equivalence retained.',
                historical_branch_audit=entry['impact'])
        if group == 'rational-U':
            independent[run] = RAT['audit_actual'](directory)
        results[run][group] = result
        print('AUDITED', run, group, flush=True)
    pooled_a = {g: {k: v for run in accounts.values() for k, v in run[g].items()} for g in GROUPS}
    pooled_r = {g: [r for run in recoveries.values() for r in run[g]] for g in GROUPS}
    aggregate = {}
    average_fields = ('assignment_top1', 'assignment_top5', 'assignment_hhi', 'assignment_gini',
                      'storage_hhi', 'storage_top1', 'mean_link_utilization_percent')
    sum_fields = ('deadline_success', 'recovery_attempted', 'without_catch', 'tail', 'remote_redo',
        'migration_total_sent_bytes', 'total_application_bytes', 'relocation_attempted', 'relocation_success',
        'direct_deadline_infeasible', 'relocate_trigger_REMOTE_BUSY', 'relocate_trigger_DIRECT_DEADLINE_INFEASIBLE',
        'relocate_trigger_INPUT_PATH_UNAVAILABLE', 'relocate_trigger_OTHER', 'F1', 'F2', 'F3')
    for group in GROUPS:
        chosen = [r for r in table if r['group'] == group]
        c = MULTI['cohort'](pooled_a[group], pooled_r[group])
        c.update({f: sum(r[f] for r in chosen) for f in sum_fields})
        c.update({f + '_mean': sum(r[f] for r in chosen)/5 for f in average_fields})
        c.update({f: max(r[f] for r in chosen) for f in
            ('peak_active_backups', 'peak_backup_storage_bytes', 'max_link_full_mean_percent')})
        c.update(busy_rate=c['busy_at_fault']/c['designated_at_fault'] if c['designated_at_fault'] else None,
            active_eq_cost=c['task_execution_waste_wu'] + c['normal_protection_eq_wu'],
            catch_seconds=V4['distribution'](int(r['actual_T_catch_ns'])/1e9 for r in pooled_r[group] if r['actual_T_catch_ns']))
        aggregate[group] = c
    comparisons, loo, fault_diff, task_deltas, catch_deltas = {}, {}, {}, [], []
    for scope in [str(r) for r in RUN['RUNS']] + ['pooled']:
        a, r = (pooled_a, pooled_r) if scope == 'pooled' else (accounts[scope], recoveries[scope])
        caught = common_caught(r)
        comparisons[scope] = dict(three_way_common_catch=MULTI['three_way_catch'](r))
        loo[scope] = {}
        for labels in MULTI['PAIRS']:
            left, right = labels
            name = left + '__' + right
            comparisons[scope][name] = contrast(a, r, labels, caught)
            loo[scope][name] = exclusions(a, r, labels, caught)
            if scope == 'pooled':
                continue
            fault_diff.setdefault(scope, {})[name] = dict(reference_only=sorted(faults[scope][left]-faults[scope][right]),
                candidate_only=sorted(faults[scope][right]-faults[scope][left]))
            for key in sorted(a[left], key=MULTI['instance_key']):
                task_deltas.append(dict(run=int(scope), task_id=MULTI['instance_key'](key)[1], reference=left,
                    candidate=right, reference_completed=a[left][key]['completed'], candidate_completed=a[right][key]['completed'],
                    **{f'delta_{f}': a[right][key][f]-a[left][key][f] for f in MULTI['FIELDS']}))
            indexed = {g: MULTI['recovery_index'](r[g]) for g in labels}
            for key in sorted(caught):
                va, vb = (int(indexed[g][key]['actual_T_catch_ns']) for g in labels)
                catch_deltas.append(dict(run=int(scope), task_id=MULTI['instance_key'](key[0])[1],
                    fault_time_ns=key[1], fault_type=key[2], reference=left, candidate=right,
                    reference_catch_ns=va, candidate_catch_ns=vb, delta_candidate_minus_reference_ns=vb-va))
    for name, value in [('audit-results', results), ('aggregate', aggregate), ('paired-comparison', comparisons),
        ('leave-one-out', loo), ('fault-differences', fault_diff), ('recovery-fix-impact', impact)]:
        (root / (name + '.json')).write_text(json.dumps(value, indent=2) + '\n')
    for name, value in [('summary', table), ('per-task-comparison', task_deltas), ('paired-catch-differences', catch_deltas)]:
        OLD['write_csv'](root / (name + '.csv'), value)
    status = dict(status='RECOVERY_U_REVALIDATION_PASS', groups=15, new_executions=8, reused_executions=7,
        execution_commit=plan['commit'], independent_rational_history=independent, exact_workload_signature=True,
        notes=['Both deadline and INPUT early-return impacts included.',
               'Online generate; no forced outcome replay. Unrerun historical groups outside U remain historical.',
               'HHI/utilization are equal-exposure per-run means; peaks are per-node maxima over runs.',
               'Signed exclusions remove one instance symmetrically; no resimulation or HHI/link recomputation.',
               'Latency uses three-way common valid catch; whole completion/resources always also reported.'],
        next_action='STOP_FOR_USER_REVIEW', default_promotion=False, ci_or_merge=False)
    (root / 'audit-status.json').write_text(json.dumps(status, indent=2) + '\n')
    return status


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT / 'output/recovery-u-revalidation')
    print(json.dumps(audit(parser.parse_args().root.resolve()), indent=2))
