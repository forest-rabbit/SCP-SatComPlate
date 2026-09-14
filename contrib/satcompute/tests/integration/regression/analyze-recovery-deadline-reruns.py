#!/usr/bin/env python3
"""Audit corrected formal trajectories, preserving historical evidence and paired cohorts."""
import argparse
from collections import Counter
import csv
import json
import itertools
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
V4 = runpy.run_path(str(HERE / 'analyze-n5c-placement.py'))
OLD = runpy.run_path(str(HERE / 'audit-direct-recovery-deadline.py'))
ROOT = OLD['ROOT']


def measure(directory, corrected=False):
    result = V4['analyze'](directory, include_tasks=True)
    if result['execution_result']['returncode'] != 0:
        raise ValueError('incomplete/failed formal execution')
    recovery = OLD['rows'](directory / 'recovery-summary.csv')
    tasks = {r['task_id']:r for r in OLD['rows'](directory / 'task-summary.csv')}
    historical_infeasible = 0
    if corrected and {int(t['compute_rate_work_units_per_second']) for t in tasks.values()} != {100000}:
        raise ValueError('this audit requires the frozen uniform formal compute profile')
    if not corrected:
        for r in recovery:
            if r['chosen_path'] in ('TAIL','REMOTE_REDO'):
                e = OLD['direct_estimate'](r, int(tasks[r['task_id']]['compute_work_units']))
                historical_infeasible += not e['redo_fits'] and not e['tail_fits']
    if corrected:
        for row in recovery:
            if row['direct_post_catchup_ns']:
                # The frozen formal profile uses the same 100,000 WU/s on all nodes.
                # Rebuild from fault-time WU/deadline, never from actual completion time.
                post = OLD['duration'](int(tasks[row['task_id']]['compute_work_units']) -
                                       int(row['actual_work_units']), 100000)
                budget = int(row['original_deadline_ns']) - int(row['fault_time_ns']) - 1 - post
                if post != int(row['direct_post_catchup_ns']) or budget != int(row['direct_deadline_budget_ns']):
                    raise ValueError('direct post/budget differs from causal WU/deadline')
                for estimate, flag in [('estimated_remote_redo_ns','direct_redo_fits'),
                                       ('estimated_tail_ns','direct_tail_fits')]:
                    fits = bool(row[estimate]) and int(row[estimate]) <= budget
                    if row[flag] != str(int(fits)):
                        raise ValueError('direct feasibility flag differs from recorded decision estimate')
            if row['direct_redo_fits'] == '1' or row['direct_tail_fits'] == '1':
                if row['chosen_path'] not in ('TAIL','REMOTE_REDO'):
                    raise ValueError('feasible remote was not preferred')
            if row['chosen_path'] in ('TAIL', 'REMOTE_REDO'):
                if row['direct_redo_fits'] != '1' and row['direct_tail_fits'] != '1':
                    raise ValueError('deadline-infeasible direct accepted')
            if row['direct_fallback_reason'] == 'DIRECT_DEADLINE_INFEASIBLE':
                if row['direct_redo_fits'] != '0' or row['direct_tail_fits'] != '0':
                    raise ValueError('feasible direct unnecessarily rejected')
            if row['checkpoint_relocation_trigger'] == 'DIRECT_DEADLINE_INFEASIBLE':
                if row['remote_busy_at_fault'] != '0' or row['checkpoint_relocation_attempted'] != '1':
                    raise ValueError('deadline-triggered migration has contradictory evidence')
    summary = result['summary']
    placement = result['placement_recovery']
    concentration = result['resource_concentration']
    row = dict(completed=summary['completed'], failed=summary['failed'],
        deadline_miss=summary['deadline_miss'],
        catch_mean_s=placement['catch_seconds']['mean'], catch_p50_s=placement['catch_seconds']['p50'],
        catch_p95_s=placement['catch_seconds']['p95'], recovery_attempted=len(recovery),
        direct=placement['direct_count'], relocate=placement['relocate_count'],
        recompute=placement['recompute_count'], busy_at_fault=placement['backup_busy_at_fault'],
        relocate_attempted=sum(r['checkpoint_relocation_attempted'] == '1' for r in recovery),
        relocate_success=sum(r['chosen_path'].startswith('MIGRATE_') and r['terminal_state']=='COMPLETED'
                             for r in recovery),
        direct_deadline_infeasible=(sum(r.get('direct_fallback_reason')=='DIRECT_DEADLINE_INFEASIBLE'
                                      for r in recovery) if corrected else historical_infeasible),
        relocation_triggers=dict(Counter(r['checkpoint_relocation_trigger'] for r in recovery
                                        if r['checkpoint_relocation_trigger'])),
        actual_execution_waste_wu=summary['task_execution_waste_wu'],
        normal_protection_eq_wu=summary['normal_protection_eq_wu'],
        reserved_idle_eq_wu=summary['reserved_idle_eq_wu'],
        total_equivalent_waste_eq_wu=summary['w_waste_actual'],
        extra_application_sent_bytes=result['network']['extra_sent_bytes'],
        migration_actual_sent_bytes=placement['migration_total_sent_bytes'],
        mean_link_utilization_percent=result['links']['mean_utilization_percent'],
        assignment_hhi=concentration['backup_assignment_count']['hhi'],
        storage_hhi=concentration['backup_storage_time_integral_byte_ns']['hhi'],
        fault_counts=result['fault_counts'])
    return row, recovery, result


def key(row, run):
    return (run, row['task_id'], row['fault_time_ns'], row['fault_type'])


def projected_equivalence(before, after):
    """Every historical CSV field, streaming rows; only added diagnostics are excluded."""
    counts = {}
    for path in sorted(before.glob('*.csv')):
        count = 0
        with path.open() as left_file, (after/path.name).open() as right_file:
            left, right = csv.DictReader(left_file), csv.DictReader(right_file)
            if not set(left.fieldnames) <= set(right.fieldnames):
                raise ValueError('historical columns missing')
            for a,b in itertools.zip_longest(left,right):
                count += 1
                if a is None or b is None or a != {k:b[k] for k in left.fieldnames}:
                    raise ValueError(f'non-affected execution changed: {path.name}:{count}')
        counts[path.name] = count
    for name in ('fault-trace.json','protection-finalization.json','capacity-aware-summary.json'):
        if json.loads((before/name).read_text()) != json.loads((after/name).read_text()):
            raise ValueError(f'non-affected JSON changed: {name}')
    return dict(status='PASS', csv_files=len(counts), rows_by_file=counts,
                fault_finalization_capacity_identical=True)


def prefix(before, after, affected_ids):
    records = OLD['rows'](before / 'recovery-summary.csv')
    cutoff = min((int(r['fault_time_ns']) for r in records if int(r['task_id']) in affected_ids),
                 default=1300_000_000_001)
    checks = {}
    for filename, column in [('task-events.csv', 'simulation_time_ns'), ('protection-events.csv', 'time_ns'),
                             ('placement-load-events.csv', 'time_ns')]:
        left = [r for r in OLD['rows'](before/filename) if int(r[column]) < cutoff]
        right = [r for r in OLD['rows'](after/filename) if int(r[column]) < cutoff]
        checks[filename] = left == right
    if not all(checks.values()):
        raise ValueError(f'behavior changed before first affected decision: {before}: {checks}')
    return dict(before_fault_time_ns=cutoff, checks=checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT/'output/recovery-deadline-reruns')
    args = parser.parse_args()
    plan = json.loads((args.root/'execution-plan.json').read_text())
    entries = {e['source']: e for e in plan['entries']}
    historical = json.loads(Path(plan['audit']).read_text())
    comparison, table, groups, task_comparison = {}, [], {}, []
    for item in historical['runs']:
        source = item['directory']
        if source not in entries:
            continue
        old = ROOT/source
        entry = entries.get(source)
        new = ROOT/entry['destination'] if entry else old
        print('AUDIT', source, 'NEW' if entry else 'REUSED', flush=True)
        before, old_recovery, old_result = measure(old)
        after, new_recovery, result = measure(new, corrected=bool(entry)) if entry else (before, old_recovery, old_result)
        if entry:
            if result['execution']['commit'] != plan['commit'] or result['execution']['worktree_dirty']:
                raise ValueError('wrong corrected execution identity')
            proof = prefix(old, new, set(entry['deadline_tasks'] + entry['input_path_tasks']))
            if not entry['deadline_tasks'] and not entry['input_path_tasks']:
                proof['full_old_field_equivalence'] = projected_equivalence(old,new)
        else:
            proof = dict(reused=True, basis='No new branch activated in complete historical execution; '
                         'same direct/migration arithmetic and unchanged existing small fixtures.')
            after = dict(after, direct_deadline_infeasible=item['direct_deadline_infeasible'])
        run = result['execution']['run']
        old_map, new_map = ({key(r,run):r for r in rs} for rs in (old_recovery,new_recovery))
        common = old_map.keys() & new_map.keys()
        caught = [k for k in common if old_map[k]['actual_T_catch_ns'] and new_map[k]['actual_T_catch_ns']]
        paired = dict(common_faults=len(common), paired_catch=len(caught),
            before_catch_mean_s=sum(int(old_map[k]['actual_T_catch_ns']) for k in caught)/len(caught)/1e9 if caught else None,
            after_catch_mean_s=sum(int(new_map[k]['actual_T_catch_ns']) for k in caught)/len(caught)/1e9 if caught else None,
            old_only_faults=[list(k) for k in sorted(old_map.keys()-new_map.keys())],
            new_only_faults=[list(k) for k in sorted(new_map.keys()-old_map.keys())])
        old_faults = json.loads((old/'fault-trace.json').read_text())['faults']
        new_faults = json.loads((new/'fault-trace.json').read_text())['faults']
        comparison[source] = dict(before=before, after=after, paired=paired, prefix=proof,
            fault_trace_identical=old_faults==new_faults,
            new_directory=str(new.relative_to(ROOT)), reused=entry is None,
            failures=result['failed_task_execution'])
        if entry:
            fields = ('task_id','fault_time_ns','chosen_path','remote_node','recovery_node',
                      'actual_work_units','local_work_units','remote_work_units','actual_T_catch_ns',
                      'terminal_state','terminal_reason','checkpoint_relocation_trigger',
                      'direct_fallback_reason','direct_deadline_budget_ns','estimated_remote_redo_ns',
                      'estimated_tail_ns')
            affected = set(entry['deadline_tasks'] + entry['input_path_tasks'])
            comparison[source]['affected_task_details'] = {
                label:[{k:r.get(k) for k in fields} for r in records if int(r['task_id']) in affected]
                for label,records in [('before',old_recovery),('after',new_recovery)]}
        for version, value in [('before',before),('after',after)]:
            table.append(dict(source=source, version=version,
                              **{k:v for k,v in value.items() if not isinstance(v,dict)}))
        old_tasks={r['task_id']:r for r in old_result['task_rows']}
        for task in result['task_rows']:
            prev=old_tasks[task['task_id']]
            task_comparison.append(dict(source=source, task_id=task['task_id'],
                before_completed=prev['completed'], after_completed=task['completed'],
                before_waste_eq_wu=prev['w_waste_actual'], after_waste_eq_wu=task['w_waste_actual']))
        meta=result['execution']
        if meta.get('n5c_variant') == 'rational-U':
            rational = runpy.run_path(str(HERE/'analyze-n5c-rational-u.py'))
            comparison[source]['independent_rational_history'] = rational['audit_actual'](new)
        if meta['placement_mode']=='n5c' and meta['input_staging_policy']=='deferred' and meta['n5c_variant'] in ('full','noU','rational-U'):
            groups.setdefault(meta['n5c_variant'],[]).append((run,after,new_map))
    three_way = {}
    maps = {g:{k:r for _,_,m in values for k,r in m.items()} for g,values in groups.items()}
    common = set.intersection(*(set(m) for m in maps.values()))
    caught = [k for k in common if all(m[k]['actual_T_catch_ns'] for m in maps.values())]
    for group, values in groups.items():
        if [r for r,_,_ in values]!=[11]:
            raise ValueError('only one run11 per U variant is authorized')
        three_way[group] = dict(common_faults=len(common), paired_catch=len(caught),
            paired_catch_mean_s=sum(int(maps[group][k]['actual_T_catch_ns']) for k in caught)/len(caught)/1e9,
            **{field:sum(s[field] for _,s,_ in values) for field in ('completed','failed','busy_at_fault',
                'direct','relocate','recompute','actual_execution_waste_wu','normal_protection_eq_wu',
                'reserved_idle_eq_wu','total_equivalent_waste_eq_wu','extra_application_sent_bytes')})
    output=dict(status='PASS', execution_commit=plan['commit'], corrected_runs=len(entries),
                comparison=comparison, run11_three_way=three_way,
                historical_other_runs_not_corrected=plan['deferred_historical_runs'])
    (args.root/'comparison.json').write_text(json.dumps(output,indent=2)+'\n')
    for filename, data in [('comparison.csv',table),('per-task-comparison.csv',task_comparison)]:
        with (args.root/filename).open('w') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(data[0])); writer.writeheader(); writer.writerows(data)
    print(json.dumps(three_way,indent=2))


if __name__=='__main__':
    main()
