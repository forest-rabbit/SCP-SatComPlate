#!/usr/bin/env python3
"""Read-only historical maintenance audit; UNKNOWN is not evidence of equivalence."""
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]


def rows(directory, name):
    with (directory / name).open(newline='') as stream:
        return list(csv.DictReader(stream))


def classify(pauses, has_captures):
    # Reserving L1 before capture rather than after cL also changes the resource trajectory.
    if pauses or has_captures:
        return 'RERUN_FULL_SCENARIO'
    return 'UNKNOWN_REQUIRES_VALIDATION'


def inspect(directory, label):
    decisions = rows(directory, 'frequency-decisions.csv')
    events = defaultdict(list)
    for r in rows(directory, 'protection-events.csv'):
        events[r['task_id']].append(r)
    indexes = {k: [int(r['time_ns']) for r in v] for k, v in events.items()}
    recoveries = rows(directory, 'recovery-summary.csv')
    faulted = {r['task_id'] for r in recoveries}
    intervals = rows(directory, 'frequency-pause-intervals.csv')
    pause_rows = []
    for r in decisions:
        if not (r['phase_before'] == 'ON' and r['proposed_action'] == 'PAUSE'
                and r['decision_committed'] == '1' and r['actual_fault_hit'] == '0'):
            continue
        task, at = r['task_id'], int(r['fault_epoch_time_ns'])
        i = bisect_right(indexes.get(task, []), at) - 1
        state = events[task][i] if i >= 0 else {}
        interval = next((p for p in intervals if p['task_id'] == task
                         and int(p['start_time_ns']) <= at < int(p['end_time_ns'])), {})
        pause_rows.append(dict(run=label, task_id=task, time_ns=at,
            reason=r['resource_reason'] or r['proposal_reason'],
            local_node=r['local_node'], remote_node=r['remote_node'],
            actual_work=r['progress_work'],
            local_work=state.get('local_work_units', 'UNKNOWN'),
            remote_work=state.get('remote_work_units', 'UNKNOWN'),
            # No exact pending target or per-edge/idle snapshots in historical decision rows.
            next_target='UNKNOWN', primary_to_local_path='UNKNOWN',
            local_to_remote_path='UNKNOWN', local_compute_idle='UNKNOWN', remote_compute_idle='UNKNOWN',
            local_storage_free=r['local_free_bytes'], remote_storage_free=r['remote_free_bytes'],
            pause_duration=interval.get('duration_ns', 'UNKNOWN'),
            fault_after_pause=any(x['task_id'] == task and int(x['fault_time_ns']) > at for x in recoveries),
            task_never_faulted=task not in faulted))
    gaps = [dict(run=label, task_id=r['task_id'], fault_time_ns=r['fault_time_ns'],
                 phase=r['phase_at_fault'],
                 local_gap_wu=int(r['actual_work_units'])-int(r['local_work_units']),
                 remote_gap_wu=int(r['actual_work_units'])-int(r['remote_work_units'])) for r in recoveries]
    reasons = Counter(p['reason'] for p in pause_rows)
    captures = sum(r['event'] == 'L1_CAPTURED' for group in events.values() for r in group)
    return pause_rows, gaps, dict(run=label, directory=str(directory.relative_to(ROOT)),
        decision_pause_rows=len(pause_rows), pause_intervals=len(intervals), local_captures=captures,
        busy_pause_rows=sum(reasons[k] for k in ('LOCAL_BUSY', 'REMOTE_BUSY')),
        busy_pause_tasks_without_fault=len({p['task_id'] for p in pause_rows
            if p['reason'] in ('LOCAL_BUSY', 'REMOTE_BUSY') and p['task_never_faulted']}),
        classification=classify(pause_rows, captures), reasons=dict(reasons))


def write_csv(path, data):
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(data[0]) if data else [])
        writer.writeheader()
        writer.writerows(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'output/audits/checkpoint-maintenance')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    plan = json.loads((ROOT / 'output/recovery-u-revalidation/execution-plan.json').read_text())
    sources = [(ROOT / r['directory'], f"U/{r['run']}/{r['group']}") for r in plan['entries']]
    sources += [(ROOT / 'output/n5c-v4/formal' / name, name)
                for name in ('R5-n5c', 'R7-n5c', 'R7-n5c-noR', 'R7-n5c-noU', 'R7-n5c-noM')]
    pauses, gaps, runs = [], [], []
    for path, label in sources:
        p, g, r = inspect(path, label)
        pauses.extend(p)
        gaps.extend(g)
        runs.append(r)
        print(label, r['classification'], r['busy_pause_rows'], flush=True)
    write_csv(args.output / 'pause-events.csv', pauses)
    write_csv(args.output / 'staleness-at-fault.csv', gaps)
    write_csv(args.output / 'affected-runs.csv', [{k: v for k, v in r.items() if k != 'reasons'} for r in runs])
    (args.output / 'summary.json').write_text(json.dumps(dict(
        runs=runs, keep_rule='maintenance trajectory AND resource ledger equivalence',
        unknown_fields='Historical per-path, exact target and idle snapshots are absent; never imputed.',
        formal_execution_scope='U only; other entries are read-only historical impact, not rerun authorization.'), indent=2)+'\n')


if __name__ == '__main__':
    main()
