#!/usr/bin/env python3
"""Audit maintenance trajectories, real ledgers and the corrected five-run U comparison."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
RUN = runpy.run_path(str(HERE/'run-checkpoint-maintenance.py'))
COMMON = runpy.run_path(str(HERE/'analyze-recovery-u-revalidation.py'))
FIX, MULTI = COMMON['FIX'], COMMON['MULTI']
V4, ROOT, GROUPS = COMMON['V4'], RUN['ROOT'], RUN['GROUPS']
rows, require = MULTI['rows'], MULTI['require']
WRITE = runpy.run_path(str(HERE/'audit-checkpoint-maintenance.py'))['write_csv']


def audit_only_changes(paths):
    """Allow later evidence/docs commits, never reinterpret outputs with changed production."""
    return all(p == 'AGENTS.md' or p.startswith(('docs/', 'contrib/satcompute/tests/'))
               or Path(p).name == 'README.md' for p in paths)


def maintenance(directory):
    events = rows(directory, 'protection-events.csv')
    captures, cursor, by_task = {}, {}, defaultdict(list)
    open_blocks, blocks, transitions = {}, [], Counter()
    for e in events:
        task, time, name = e['task_id'], int(e['time_ns']), e['event']
        by_task[task].append(e)
        w, l, r, actual = (int(e[k]) for k in ('work_units', 'local_work_units', 'remote_work_units', 'actual_work_units'))
        require(r <= l <= actual, 'invalid maintenance progress')
        if name == 'START': cursor[task] = w
        if name == 'L1_CAPTURED':
            require(w > cursor[task] and w <= actual and (task, w) not in captures,
                    'duplicate/noncausal capture sequence')
            captures[task, w] = (cursor[task], int(e['bytes']), time)
            cursor[task] = w
        for stage in ('CAPTURE', 'REMOTE_BATCH'):
            key = task, stage
            change = name.startswith(stage+'_BLOCKED_') or name == stage+'_RESUMED'
            terminal = name in ('PROTECTION_STOP', 'QUIESCE_FOR_RECOVERY')
            if change or terminal:
                if key in open_blocks:
                    start, reason = open_blocks.pop(key)
                    require(time >= start, 'negative blocked interval')
                    blocks.append(dict(task_id=task, stage=stage, start_ns=start, end_ns=time,
                                       duration_ns=time-start, reason=reason))
                if name.startswith(stage+'_BLOCKED_'):
                    reason = name.removeprefix(stage+'_BLOCKED_')
                    open_blocks[key] = time, reason
                    transitions[stage+'_'+reason] += 1
    # QUIESCE's existing event name is retained in historical logs; close by exact recorded stop.
    summaries = {r['task_id']: r for r in rows(directory, 'protection-task-summary.csv')}
    for (task, stage), (start, reason) in open_blocks.items():
        end = int(summaries[task]['stop_time_ns'])
        require(end >= start, 'block after actual protection stop')
        blocks.append(dict(task_id=task, stage=stage, start_ns=start, end_ns=end, duration_ns=end-start, reason=reason))
    seen = set()
    for f in rows(directory, 'protection-transfers.csv'):
        if f['kind'] != 'L1': continue
        key = f['task_id'], int(f['work_units'])
        # Runtime outputs can repeat a physical flow in reports; identical IDs are one flow.
        require(key not in seen, 'duplicate L1 sequence/flow')
        require(key in captures and int(f['bytes']) == captures[key][1], 'L1 lost capture/H byte identity')
        seen.add(key)
    decisions = rows(directory, 'frequency-decisions.csv')
    for r in decisions:
        if r['phase_before'] == 'ON':
            require(r['resource_reason'] not in ('LOCAL_BUSY', 'REMOTE_BUSY'), 'ON maintenance still CPU-gated')
        if r.get('maintenance_resource_hold') == '1':
            require(r['decision_committed'] == '1' and r['phase_before'] == 'ON' and
                    r['current_delta_permille'] == r['committed_delta_permille'] and
                    r['current_n'] == r['committed_n'], 'resource hold changed committed cadence')
    gaps = [dict(task_id=r['task_id'], phase=r['phase_at_fault'], fault_time_ns=r['fault_time_ns'],
                 local_gap_wu=int(r['actual_work_units'])-int(r['local_work_units']),
                 remote_gap_wu=int(r['actual_work_units'])-int(r['remote_work_units']))
            for r in rows(directory, 'recovery-summary.csv', True)]
    return dict(captures=len(captures), block_transitions=dict(transitions),
        block_duration_ns={stage: sum(r['duration_ns'] for r in blocks if r['stage'] == stage)
                           for stage in ('CAPTURE', 'REMOTE_BATCH')},
        local_gap_wu=V4['distribution'](r['local_gap_wu'] for r in gaps if r['phase'] == 'ON'),
        remote_gap_wu=V4['distribution'](r['remote_gap_wu'] for r in gaps if r['phase'] == 'ON'),
        resource_hold_decisions=sum(r.get('maintenance_resource_hold') == '1' for r in decisions)), blocks, gaps


def prefix(before, after):
    candidates = []
    for directory in (before, after):
        candidates += [int(r['time_ns']) for r in rows(directory, 'protection-events.csv') if r['event'] == 'L1_CAPTURED']
        candidates += [int(r['fault_epoch_time_ns']) for r in rows(directory, 'frequency-decisions.csv') if r['phase_before'] == 'ON']
    cutoff = min(candidates)
    for name, column in [('task-events.csv', 'simulation_time_ns'), ('protection-events.csv', 'time_ns'),
                         ('placement-load-events.csv', 'time_ns')]:
        a, b = ([r for r in rows(d, name) if int(r[column]) < cutoff] for d in (before, after))
        require(a == b, 'pre-maintenance behavior changed: '+name)
    return dict(status='PASS', before_earliest_maintenance_ns=cutoff)


def audit(root):
    plan = json.loads((root/'execution-plan.json').read_text())
    current = RUN['identity']()
    require(audit_only_changes(RUN['git']('diff', '--name-only', plan['commit'], current).splitlines())
            and plan['entries'] == RUN['entries'](root), 'formal production/scope changed')
    results, accounts, recoveries, faults = ({str(r): {} for r in range(11, 16)} for _ in range(4))
    table, block_rows, gap_rows, histories, impact = [], [], [], {}, {}
    signature = None
    for entry in plan['entries']:
        run, group = str(entry['run']), entry['group']
        directory, before = ROOT/entry['directory'], ROOT/entry['source']
        print('AUDIT', run, group, flush=True)
        metric, recovery, value = FIX['measure'](directory, corrected=True)
        require(value['execution']['commit'] == plan['commit'] and not value['execution']['worktree_dirty'], 'dirty execution')
        require(value['execution']['simulation_duration_s'] == 1300 and value['summary']['tasks'] == 800, 'incomplete scene')
        observed = MULTI['task_signature'](rows(directory, 'task-summary.csv'))
        signature = observed if signature is None else signature
        require(observed == signature and observed == MULTI['task_signature'](rows(before, 'task-summary.csv')), 'workload changed')
        observed_maintenance, blocks, gaps = maintenance(directory)
        block_rows.extend(dict(run=run, group=group, **r) for r in blocks)
        gap_rows.extend(dict(run=run, group=group, **r) for r in gaps)
        metric.update(run=run, group=group)
        table.append(metric)
        network = MULTI['task_network'](rows(directory, 'protection-transfers.csv'))
        require(sum(network.values()) == value['network']['extra_sent_bytes'], 'actual network sum differs')
        accounts[run][group] = {f"{run}:{t['task_id']}": dict(t, extra_application_bytes=network.get(str(t['task_id']), 0))
                                for t in value.pop('task_rows')}
        recoveries[run][group] = [dict(r, task_id=f"{run}:{r['task_id']}") for r in recovery]
        faults[run][group] = MULTI['fault_signature'](directory)
        old_faults = MULTI['fault_signature'](before)
        impact[run+':'+group] = dict(prefix=prefix(before, directory), old_only_faults=sorted(old_faults-faults[run][group]),
            new_only_faults=sorted(faults[run][group]-old_faults), maintenance=observed_maintenance)
        if group == 'rational-U': histories[run] = MULTI['RAT']['audit_actual'](directory)
        results[run][group] = value
    pooled_a = {g: {k: v for a in accounts.values() for k, v in a[g].items()} for g in GROUPS}
    pooled_r = {g: [r for a in recoveries.values() for r in a[g]] for g in GROUPS}
    comparison, exclusions, aggregate, before_after = {}, {}, {}, {}
    for g in GROUPS:
        selected = [r for r in table if r['group'] == g]
        aggregate[g] = dict(MULTI['cohort'](pooled_a[g], pooled_r[g]),
            catch_seconds=V4['distribution'](int(r['actual_T_catch_ns'])/1e9 for r in pooled_r[g] if r['actual_T_catch_ns']),
            assignment_hhi_mean=sum(r['assignment_hhi'] for r in selected)/5,
            storage_hhi_mean=sum(r['storage_hhi'] for r in selected)/5,
            mean_link_utilization_percent=sum(r['mean_link_utilization_percent'] for r in selected)/5)
        old_recovery = [dict(r, task_id=f"{entry['run']}:{r['task_id']}") for entry in plan['entries']
                        if entry['group'] == g for r in rows(ROOT/entry['source'], 'recovery-summary.csv')]
        before_after[g] = dict(paired_catch=MULTI['OLD']['paired_catch'](old_recovery, pooled_r[g], ('before', 'after')))
        for label, data in [('before', old_recovery), ('after', pooled_r[g])]:
            before_after[g][label+'_on_gap_wu'] = {
                k: V4['distribution'](int(r['actual_work_units'])-int(r[k]) for r in data if r['phase_at_fault']=='ON')
                for k in ('local_work_units', 'remote_work_units')}
    for scope in [str(r) for r in range(11, 16)]+['pooled']:
        a, r = (pooled_a, pooled_r) if scope == 'pooled' else (accounts[scope], recoveries[scope])
        caught = COMMON['common_caught'](r)
        comparison[scope] = dict(three_way_common_catch=MULTI['three_way_catch'](r))
        exclusions[scope] = {}
        for pair in MULTI['PAIRS']:
            name = '__'.join(pair)
            comparison[scope][name] = COMMON['contrast'](a, r, pair, caught)
            exclusions[scope][name] = COMMON['exclusions'](a, r, pair, caught)
    for name, value in [('audit-results', results), ('aggregate', aggregate), ('paired-comparison', comparison),
                        ('leave-one-out', exclusions), ('maintenance-impact', impact), ('before-after', before_after)]:
        (root/(name+'.json')).write_text(json.dumps(value, indent=2)+'\n')
    WRITE(root/'summary.csv', [{k: v for k, v in r.items() if not isinstance(v, dict)} for r in table])
    WRITE(root/'maintenance-blocks.csv', block_rows)
    WRITE(root/'staleness-at-fault.csv', gap_rows)
    status = dict(status='CHECKPOINT_MAINTENANCE_U_PASS', groups=15, new_executions=15, reused_executions=0,
        commit=plan['commit'], independent_rational_history=histories, default_promotion=False,
        next_action='STOP_FOR_USER_REVIEW', ci_or_merge=False)
    (root/'audit-status.json').write_text(json.dumps(status, indent=2)+'\n')
    return status


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT/'output/checkpoint-maintenance-fixed')
    print(json.dumps(audit(parser.parse_args().root.resolve()), indent=2))
