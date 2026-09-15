#!/usr/bin/env python3
"""Independently audit causal Multi-tree mapping; never tune or launch a simulation."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
BASELINE = ROOT / 'contrib/satcompute/protection/baseline/multitree'


def rank(knots, raw):
    if raw <= knots[0]['raw']:
        return knots[0]['percentile']
    for left, right in zip(knots, knots[1:]):
        if raw <= right['raw']:
            fraction = (raw - left['raw']) / (right['raw'] - left['raw'])
            return left['percentile'] + fraction * (right['percentile'] - left['percentile'])
    return knots[-1]['percentile']


def audit(directory):
    scale = json.loads((BASELINE / 'calibration/published-ft-scale.json').read_text())
    tasks = {t['task_id']: t for t in json.loads((ROOT / scale['source_task_trace']).read_text())['tasks']}
    rows = list(csv.DictReader((directory / 'multitree-decisions.csv').open()))
    summary = json.loads((directory / 'multitree-mapping-summary.json').read_text())
    decisions, branches, profiles, features = Counter(), Counter(), defaultdict(Counter), defaultdict(list)
    seen = set()
    causal_queues, queue_at_start = defaultdict(dict), {}
    for event in csv.DictReader((directory / 'task-events.csv').open()):
        task_id, node = int(event['task_id']), int(event['node_id'])
        if event['from_state'] == 'QUEUED':
            causal_queues[tasks[task_id]['compute_node_id']].pop(task_id)
        if event['to_state'] == 'QUEUED':
            causal_queues[node][task_id] = int(event['simulation_time_ns'])
        if event['to_state'] == 'RUNNING' and task_id not in queue_at_start:
            queue_at_start[task_id] = sorted(causal_queues[node],
                key=lambda task: (causal_queues[node][task], task))
    for row in rows:
        task = tasks[int(row['task_id'])]
        task_id = task['task_id']
        assert task_id not in seen
        seen.add(task_id)
        ts, iddl, cl, fr = (float(row[k]) for k in ('ts_mt', 'iddl_mt', 'cl_mt', 'fr_mt'))
        assert not any(math.isnan(x) for x in (ts, iddl, cl, fr))
        assert 1 <= ts <= 3 and 5 <= iddl <= 13 and cl >= 0 and fr >= 0
        assert int(row['input_bytes']) == task['input_bytes']
        assert math.isclose(ts, 1 + 2 * rank(scale['input_knots'], task['input_bytes']), abs_tol=1e-12)
        assert math.isclose(iddl, 5 + 8 * rank(scale['deadline_knots'], int(row['compute_deadline_budget_ns'])), abs_tol=1e-12)
        queued = list(map(int, row['queued_ids'].split(';'))) if row['queued_ids'] else []
        assert queued == queue_at_start[task_id], ('causal queue mismatch', task_id, queued, queue_at_start[task_id])
        assert len(queued) == int(row['queued_count']) and task_id not in queued and len(set(queued)) == len(queued)
        assert all(tasks[q]['compute_node_id'] == task['compute_node_id'] for q in queued)
        expected_cl = sum(1 + 2 * rank(scale['input_knots'], tasks[q]['input_bytes']) for q in queued)
        assert math.isclose(cl, expected_cl, abs_tol=1e-12)
        q1, q2, q = (float(row[k]) for k in ('q_f1', 'q_f2', 'q_comp'))
        assert math.isclose(q, 1 - (1-q1)*(1-q2), abs_tol=1e-14)
        expected_fr = -math.log1p(-q) / (int(row['check_interval_ns']) / 1e9) if q < 1 else math.inf
        assert math.isclose(fr, expected_fr, abs_tol=1e-14)
        # Independent Boolean transcription of Fig.14's RP leaves.
        rp = ((iddl > 9 and (cl >= 8.2 or (cl >= 5 and ts >= 2.2))) or
              (iddl <= 9 and (fr > 0.3 or cl >= 3.5)))
        assert row['decision'] == ('RP' if rp else 'RS')
        decisions[row['decision']] += 1
        branches[row['branch']] += 1
        profiles[row['profile']][row['decision']] += 1
        for key in ('ts_mt', 'iddl_mt', 'cl_mt', 'fr_mt', 'queued_count'):
            features[key].append(float(row[key]))
    assert summary['task_count'] == len(tasks) == scale['task_count']
    assert set(summary['undecided_task_ids']) == set(tasks) - seen
    assert summary['RS'] == decisions['RS'] and summary['RP'] == decisions['RP']
    assert dict(branches) == summary['branches']
    issues = []
    if not decisions['RS'] or not decisions['RP']:
        issues.append('degenerate RS/RP split')
    if any(len(set(features[key])) <= 1 for key in ('ts_mt', 'iddl_mt', 'cl_mt', 'fr_mt')):
        issues.append('constant feature needs mapping review')
    distributions = {}
    for key, values in features.items():
        values.sort()
        distributions[key] = {label: values[round(f * (len(values)-1))]
                              for label, f in [('min', 0), ('p10', .1), ('p50', .5), ('p90', .9), ('max', 1)]}
    # Samples deliberately cover distinct queue depths and leaves, not favourable outcomes.
    samples, keys = [], set()
    for row in rows:
        key = (row['queued_count'], row['branch'])
        if key not in keys:
            keys.add(key)
            samples.append({k: row[k] for k in ('task_id', 'primary_node', 'queued_count', 'queued_ids', 'cl_mt', 'branch', 'decision')})
    return {'status': 'MULTITREE_MAPPING_NEEDS_REVIEW' if issues else 'MULTITREE_MAPPING_READY',
            'issues': issues, 'decided': len(rows), 'undecided': len(tasks)-len(rows),
            'decisions': dict(decisions), 'branches': dict(branches),
            'profiles': {k: dict(v) for k, v in profiles.items()}, 'distributions': distributions,
            'queue_samples': samples}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = audit(args.directory)
    text = json.dumps(report, indent=2, allow_nan=False) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text)
