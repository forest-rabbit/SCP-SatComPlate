"""Audit causal passive START snapshots; labels never enter the feature builder."""
from collections import Counter
import json
import math
from pathlib import Path
import runpy

API = runpy.run_path(str(Path(__file__).with_name('selective_input_offline_audit.py')))
require, rows, unique = (API[k] for k in ('require', 'rows', 'unique'))
COHORT = 'DEFERRED_N5R_RUN11_DEVELOPMENT'
CANONICAL_COMMIT = 'c7889de89cf02363a2694a18fa2d5fa59a1b18e4'


def execution_identity(execution, result, run, canonical):
    """Complete source/parameter gate, independently of any P_F or outcome score."""
    require(execution.get('purpose') == 'DEVELOPMENT_CALIBRATION' and
            execution.get('final_performance_result') is False and execution.get('input_start_audit') is True and
            execution.get('selective_input_enabled') is False and execution.get('worktree_dirty') is False,
            'not a clean passive development execution')
    require(execution.get('canonical_reference_commit') == canonical['commit'] == CANONICAL_COMMIT and
            bool(execution.get('commit')), 'incorrect canonical source identity')
    require(result['returncode'] == 0 and run['simulation_duration_ns'] == 1300*10**9 and
            run['task_count'] == 800, 'incomplete run11 execution')
    before, after = API['command_controls'](canonical), API['command_controls'](execution)
    allowed = {'--outputDir', '--faultTrace', '--inputStartAudit'}
    require(after.get('--inputStartAudit') == '1' and
            {k: v for k, v in before.items() if k not in allowed} ==
            {k: v for k, v in after.items() if k not in allowed}, 'nonlogging controls changed')
    return dict(status='PASS', execution_commit=execution['commit'], canonical_commit=canonical['commit'],
                nonlogging_controls_identical=True, task_count=800, simulation_duration_s=1300)


def feature_snapshot(record):
    """Only pre-START_CHECKPOINT fields; no outcome/recovery/actual future event inputs."""
    r = record
    require(r['admitted'] is True and r['snapshot_point'] == 'PRE_START_CHECKPOINT' and
            r['checkpoint_ready'] is False, 'not an admitted initialization snapshot')
    now, remaining = r['start_time_ns'], r['remaining_compute_ns']
    require(now >= 0 and remaining > 0 and 0 <= r['progress_work'] < r['compute_work_units'],
            'invalid live task window')
    p = r['predictor']
    trajectory, pf = None, None
    if p is not None:
        require(p['source'] == 'QueryTaskPrediction' and p['finish_exclusive'] is True,
                'new passive queries must retain finish-exclusive semantics')
        require(p['prediction_time_ns'] == now and p['remaining_compute_ns'] == remaining,
                'predictor window mismatch')
        first, interval = p['first_sample_time_ns'], p['check_interval_ns']
        require(interval > 0 and first % interval == 0 and first >= now, 'noncanonical grid')
        pending = first == now
        require((pending and now > 0) or first == now + interval - now % interval,
                'missing pending/next first sample')
        require(p['first_sample_semantics'] == ('PENDING_CURRENT' if pending else 'NEXT_CANONICAL'),
                'first sample semantic label mismatch')
        require(r['start_trigger'] in ('TASK_RUNNING', 'CAPACITY_RELEASE', 'FAULT_EPOCH'),
                'unknown START trigger')
        require(r['start_trigger'] != 'FAULT_EPOCH' or not pending, 'survived epoch counted again')
        steps = p['future_steps']
        require(p['future_steps_count'] == len(steps) and
                [s['time_ns'] for s in steps] == list(range(first, now + remaining, interval)),
                'incomplete or finish-inclusive trajectory')
        trajectory, survival = [], 1.0
        for step in steps:
            q1, q2, q = (step[k] for k in ('q_f1', 'q_f2', 'q_comp'))
            require(all(math.isfinite(x) and 0 <= x <= 1 for x in (q1, q2, q)), 'invalid probability')
            require(math.isclose(q, 1-(1-q1)*(1-q2), abs_tol=1e-12), 'F1/F2 union mismatch')
            trajectory.append(dict(step, first_failure_mass=survival*q))
            survival *= 1-q
        pf = p['P_F']
        require(math.isfinite(pf) and 0 <= pf <= 1 and
                math.isclose(pf, 1-survival, abs_tol=1e-12), 'P_F survival product mismatch')
    path = r['input_path']
    local = r['source_node'] == r['remote_node']
    require(path['source_node'] == r['source_node'] and path['remote_node'] == r['remote_node'] and
            path['local_delivery'] == local, 'path uses wrong actual pair')
    serialization = None
    if local:
        require(not path['hops'] and path['admitted_rate_bps'] is None, 'LocalDelivery faked as UDP')
        serialization = 0.0
    elif path['admissible']:
        require(path['reachable'] and path['admitted_rate_bps'] > 0 and path['hops'], 'invalid admitted path')
        nodes = [path['hops'][0]['source']] + [h['destination'] for h in path['hops']]
        require(nodes[0] == r['source_node'] and nodes[-1] == r['remote_node'] and
                all(h['source'] == nodes[i] for i, h in enumerate(path['hops'])), 'disconnected path')
        serialization = r['input_bytes'] * 8 / path['admitted_rate_bps']
    else:
        require(path['admitted_rate_bps'] is None, 'unavailable path fabricated a rate')
    actual = r['actual_post_batch_validation']
    if actual is not None:
        require(actual['recovery_rate'] == r['recovery_rate'] and actual['primary_rate'] == r['primary_rate'],
                'actual validation rate mismatch')
    return dict(cohort=COHORT, task_id=str(r['task_id']), profile=r['profile'], input_bytes=r['input_bytes'],
        start_time_ns=now, local_node=r['local_node'], remote_node=r['remote_node'],
        remaining_compute_s=remaining/1e9, P_F=pf, future_first_failure_trajectory=trajectory,
        planned_network_input_bytes=0 if local else r['input_bytes'], input_serialization_s=serialization,
        path_observation='LOCAL' if local else 'ADMISSIBLE' if path['admissible'] else 'UNAVAILABLE',
        checkpoint_state_tail_forecast=None,
        G_I_s=0.0 if local else None, P_Iimpact=0.0 if local else None, P_Iddl=0.0 if local else None)


def audit_trace(root):
    """New Deferred run labels itself; never borrow an old JIT run's future outcome."""
    document = json.loads((root / 'input-start-snapshots.json').read_text())
    require(document['purpose'] == 'DEVELOPMENT_CALIBRATION' and
            document['selective_input_enabled'] is False, 'incorrect trace purpose')
    snapshots = unique([dict(r, task_id=str(r['task_id'])) for r in document['candidates']], 'task_id')
    starts, excluded = API['candidate_starts'](rows(root, 'frequency-decisions.csv'),
        rows(root, 'protection-events.csv'), rows(root, 'placement-selections.csv'))
    require(snapshots.keys() == {r['task_id'] for r in starts}, 'admitted population mismatch')
    tasks = unique(rows(root, 'task-summary.csv'), 'task_id')
    impacts = API['primary_impacts'](rows(root, 'fault-task-impact.csv'))
    recovery = unique(rows(root, 'recovery-summary.csv'), 'task_id')
    features, labels = [], []
    for start in starts:
        tid = start['task_id']
        snapshot, task = snapshots[tid], tasks[tid]
        for a, b in (('start_time_ns', 'fault_epoch_time_ns'), ('local_node', 'local_node'),
                     ('remote_node', 'remote_node'), ('delta_permille', 'committed_delta_permille'),
                     ('batch_n', 'committed_n'), ('progress_work', 'progress_work')):
            require(snapshot[a] == int(start[b]), 'snapshot committed START mismatch: '+a)
        require(snapshot['start_trigger'] == start['decision_trigger'], 'trigger mismatch')
        for a, b in (('input_bytes', 'input_bytes'), ('compute_work_units', 'compute_work_units'),
                     ('source_node', 'source_node_id'), ('primary_node', 'compute_node_id'),
                     ('deadline_ns', 'compute_deadline_time_ns')):
            require(snapshot[a] == int(task[b]), 'static/dispatch contract mismatch: '+a)
        feature = feature_snapshot(snapshot)
        features.append(feature)
        impact, rec = impacts.get(tid), recovery.get(tid)
        label = API['label_candidate'](task, task, impact, impact, rec, rec)
        labels.append(dict(cohort=COHORT, task_id=tid, **label,
            historical_fault_source=impact['fault_type'] if impact else None,
            planned_network_input_bytes=feature['planned_network_input_bytes']))
    reduced = API['reduced_pf_analysis'](features, labels, COHORT)
    counts = dict(Counter(r['label'] for r in labels))
    summary = dict(status='PASS', purpose='DEVELOPMENT_CALIBRATION', final_performance_result=False,
        task_count=len(tasks), task_completed=sum(t['task_success'] == '1' for t in tasks.values()),
        candidates=len(starts), labels=counts, excluded_commits=excluded,
        unadmitted_snapshot_count=document['unadmitted_snapshot_count'],
        triggers=dict(Counter(r['decision_trigger'] for r in starts)),
        complete_trajectories=sum(f['future_first_failure_trajectory'] is not None for f in features),
        future_steps=sum(len(f['future_first_failure_trajectory'] or []) for f in features),
        actual_pair_differs_from_reference=sum(r['frequency_reference_pair'] is not None and
            (r['local_node'], r['remote_node']) != (r['frequency_reference_pair']['local'],
                                                  r['frequency_reference_pair']['remote']) for r in snapshots.values()),
        path_observations=dict(Counter(f['path_observation'] for f in features)),
        A0='PASS', A1='INCOMPLETE', advanced_ranking='SKIPPED_A1_INCOMPLETE',
        advanced_reason='Current state/cost/quota and proposal estimates do not identify future legal checkpoint receipt/tail at every risk step. No second predictor or future-outcome substitution.',
        selected_production_threshold=None, selective_input_enabled=False)
    return dict(features=features, labels=labels, reduced=reduced, summary=summary)


def write_audit(root, output):
    API['HISTORY']['output_guard'](output, [root])
    execution = json.loads((root / 'execution.json').read_text())
    canonical = Path(execution['canonical_reference'])
    API['HISTORY']['output_guard'](output, [root, canonical])
    before = API['evidence_metadata']([root, canonical])
    identity = execution_identity(execution, json.loads((root / 'execution-result.json').read_text()),
        json.loads((root / 'run-summary.json').read_text()), json.loads((canonical / 'execution.json').read_text()))
    result = audit_trace(root)
    # Reuse maintained ledger/placement audits, not another waste/routing model.
    placement = runpy.run_path(str(Path(__file__).with_name('placement_audit.py')))
    runtime = placement['analyze'](root)
    equivalence = placement['BASE']['strict_equivalence'](canonical, root)
    require(equivalence['passed'], 'current trace differs from corrected canonical semantics; stop for audit')
    require({p.name for p in canonical.glob('*.csv')} == {p.name for p in root.glob('*.csv')},
            'CSV output schema set changed')
    metadata = {'execution.json', 'execution-result.json'}
    semantic_json = {p.name for p in canonical.glob('*.json')} - metadata
    require(semantic_json == {p.name for p in root.glob('*.json')} - metadata - {'input-start-snapshots.json'},
            'unexpected additional/missing runtime JSON')
    for name in semantic_json - equivalence['checks'].keys():
        equivalent = json.loads((canonical / name).read_text()) == json.loads((root / name).read_text())
        require(equivalent, 'runtime JSON changed: '+name)
        equivalence['checks'][name] = True
        equivalence['json_equivalent_count'] += 1
    result['summary']['runtime_equivalence'] = dict(passed=True,
        csv_byte_identical_count=equivalence['csv_byte_identical_count'],
        json_equivalent_count=equivalence['json_equivalent_count'])
    result['summary']['independent_runtime_accounting'] = 'PASS'
    require(before == API['evidence_metadata']([root, canonical]), 'raw evidence modified')
    output.mkdir(parents=True, exist_ok=False)
    for name, data in (('candidate-features', result['features']), ('candidate-labels', result['labels']),
                       ('pf-score-sweeps', result['reduced']['sweeps']), ('pf-reference-points', result['reduced']['references'])):
        # Nested trajectories have a canonical JSON representation, not Python repr.
        records = [{k: json.dumps(v, separators=(',', ':')) if isinstance(v, (list, dict)) else v
                    for k, v in r.items()} for r in data]
        API['HISTORY']['write_csv'](output / (name+'.csv'), records)
    for name, data in (('summary', result['summary']), ('pf-summary', result['reduced']['summary']),
                       ('source-identity', identity), ('runtime-equivalence', equivalence),
                       ('runtime-accounting', runtime)):
        (output / (name+'.json')).write_text(json.dumps(data, indent=2, allow_nan=False)+'\n')
    return result['summary']
