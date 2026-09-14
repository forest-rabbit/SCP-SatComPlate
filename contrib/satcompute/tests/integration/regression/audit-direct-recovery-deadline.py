#!/usr/bin/env python3
"""Read immutable formal ledgers; never infer counterfactual paths from future outcomes."""
import argparse
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def duration(work, rate):
    if work < 0 or rate <= 0:
        raise ValueError("invalid causal work/rate")
    return (work * 1_000_000_000 + rate - 1) // rate


def direct_estimate(record, total):
    """Recorded decision estimates, not actual arrival/catch/terminal times."""
    rate = int(record['recovery_rate_wu_per_s'])
    post = duration(total - int(record['actual_work_units']), rate)
    decision = int(record['recovery_accept_time_ns'])
    remaining = int(record['original_deadline_ns']) - decision
    redo = int(record['estimated_remote_redo_ns']) if record['estimated_remote_redo_ns'] else None
    tail = int(record['estimated_tail_ns']) if record['estimated_tail_ns'] else None
    budget = remaining - post
    return dict(redo_estimate_ns=redo, tail_estimate_ns=tail, post_catchup_ns=post,
                decision_time_ns=decision, deadline_remaining_ns=remaining,
                direct_deadline_budget_ns=budget,
                redo_fits=redo is not None and redo <= budget,
                tail_fits=tail is not None and tail <= budget)


def sources():
    paths = sorted((ROOT / 'output/n5c-v4/formal').glob('*/recovery-summary.csv'))
    paths += sorted((ROOT / 'output/n5c-u-audit').glob('run-*/*/recovery-summary.csv'))
    paths += sorted((ROOT / 'output/n5c-rational-u').glob('run-11/*/recovery-summary.csv'))
    paths += sorted((ROOT / 'output/n5c-rational-multirun').glob('run-*/*/recovery-summary.csv'))
    return [p.parent for p in paths]


def audit(directories):
    events, runs = [], []
    for directory in directories:
        metadata = json.loads((directory / 'execution.json').read_text())
        result = json.loads((directory / 'execution-result.json').read_text())
        if result['returncode'] != 0 or metadata['worktree_dirty']:
            raise ValueError(f'incomplete/dirty historical execution: {directory}')
        tasks = {r['task_id']: r for r in rows(directory / 'task-summary.csv')}
        records = rows(directory / 'recovery-summary.csv')
        selected = []
        path_changes = []
        for r in records:
            if r['checkpoint_fallback_reason'] == 'INPUT_PATH_UNAVAILABLE':
                path_changes.append(int(r['task_id']))
            if r['chosen_path'] not in ('TAIL', 'REMOTE_REDO'):
                continue
            if r['phase_at_fault'] != 'ON' or r['remote_busy_at_fault'] != '0':
                raise ValueError('unexpected direct recovery contract')
            e = direct_estimate(r, int(tasks[r['task_id']]['compute_work_units']))
            affected = not e['redo_fits'] and not e['tail_fits']
            e.update(directory=str(directory.relative_to(ROOT)), run=metadata['run'],
                task_id=int(r['task_id']), fault_time_ns=int(r['fault_time_ns']),
                remote_node=int(r['remote_node']), direct_path=r['chosen_path'],
                direct_deadline_infeasible=affected,
                relocation_opportunity='UNKNOWN_HISTORICAL_CANDIDATE_STATE' if affected else 'NOT_NEEDED',
                relocate_candidate_count=None, first_feasible_candidate=None)
            selected.append(e)
            events.append(e)
        affected_tasks = [e['task_id'] for e in selected if e['direct_deadline_infeasible']]
        runs.append(dict(directory=str(directory.relative_to(ROOT)), execution_commit=metadata['commit'],
            run=metadata['run'], direct_recovery_events=len(selected),
            direct_deadline_infeasible=len(affected_tasks), affected_tasks=affected_tasks,
            input_path_fallback_tasks=path_changes,
            rerun_required=bool(affected_tasks or path_changes)))
    affected = [e for e in events if e['direct_deadline_infeasible']]
    return dict(direct_recovery_events=len(events), direct_deadline_infeasible=len(affected),
        with_feasible_relocation=0, without_feasible_relocation=0,
        unknown_relocation=len(affected),
        candidate_evidence='Historical ledgers do not certify complete same-event candidate/path/storage state. '
            'Unknown is not zero candidates. Corrected executions record actual relocation decisions.',
        affected_runs=[r for r in runs if r['rerun_required']], affected_tasks=affected, runs=runs), events


def cb_audit():
    """Separate augmented CB evidence. Restore cost is frozen per-task model input."""
    groups = []
    base = ROOT / 'output/cb-sat-v2/20260913T070155626344Z-formal'
    for directory in sorted(base.glob('CB-*-relocate')):
        tasks = {r['task_id']: r for r in rows(directory / 'cb-sat-tasks.csv')}
        events = []
        for r in rows(directory / 'cb-sat-recovery.csv'):
            if r['chosen_path'] != 'DIRECT':
                continue
            task = tasks[r['task_id']]
            restore = int(task['remote_cost_ns']) if r['log_objects'] else 0
            compute = duration(int(task['total_work_units']) - int(r['recoverable_work_units']),
                               int(r['recovery_rate_wu_per_s']))
            remaining = int(r['original_deadline_ns']) - int(r['recovery_accept_time_ns'])
            events.append(dict(task_id=int(r['task_id']), estimated_complete_ns=restore + compute,
                remaining_ns=remaining, infeasible=restore + compute > remaining))
        groups.append(dict(directory=str(directory.relative_to(ROOT)), direct_events=len(events),
                           affected=[e for e in events if e['infeasible']], events=events))
    if len(groups) != 4:
        raise ValueError('corrected CB relocate evidence missing')
    return dict(main_cb_unchanged=True, groups=groups,
                direct_deadline_infeasible=sum(len(g['affected']) for g in groups))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
        default=ROOT / 'output/audits/recovery-direct-deadline-impact.json')
    args = parser.parse_args()
    if args.output.exists() or args.output.with_suffix('.csv').exists():
        raise SystemExit('refuse to overwrite existing audit evidence')
    report, events = audit(sources())
    report['cb_sat_augmented'] = cb_audit()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    with args.output.with_suffix('.csv').open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(events[0]))
        writer.writeheader()
        writer.writerows(events)
    print(json.dumps({k: report[k] for k in ('direct_recovery_events', 'direct_deadline_infeasible',
                                           'unknown_relocation')}, indent=2))
    for run in report['affected_runs']:
        print(run['directory'], 'deadline', run['affected_tasks'], 'INPUT path', run['input_path_fallback_tasks'])


if __name__ == '__main__':
    main()
