"""Read-only peer Selective policy-contract symmetry audit.

Counts refer to (decision, candidate, peer) observations, not distinct tasks.
Only strictly earlier events may establish a peer's state. The original peer_count
constrains membership; missing identities or risk windows remain unknown.
Runtime dependency times NEVER enter the hypothetical P recovery model.
"""
import argparse
from bisect import bisect_left
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path


NS = 10**9
TESTS = Path(__file__).resolve().parents[2]
REPO = TESTS.parents[2]
STATE_EVENTS = {
    'PREFETCH_REQUESTED', 'PREFETCH_STARTED', 'PREFETCH_LOCAL_STARTED',
    'PREFETCH_READY', 'PREFETCH_FAILED', 'PREFETCH_RELEASED', 'PREFETCH_NOT_ADMITTED',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def rows(root, name):
    with (root / name).open(newline='') as stream:
        return list(csv.DictReader(stream))


class Timeline:
    def __init__(self, events, time_key):
        self.events = sorted(events, key=lambda e: int(e[time_key]))
        self.times = [int(e[time_key]) for e in self.events]

    def before(self, now):
        index = bisect_left(self.times, now)
        return self.events[index - 1] if index else None

    def at(self, now):
        index = bisect_left(self.times, now)
        result = []
        while index < len(self.times) and self.times[index] == now:
            result.append(self.events[index])
            index += 1
        return result


def timelines(events, key, time_key):
    grouped = defaultdict(list)
    for event in events:
        grouped[int(event[key])].append(event)
    return {key: Timeline(value, time_key) for key, value in grouped.items()}


def dependency_from_prefix(timeline, now, target, source, satellite_available):
    """Resolve's state branches only; no interpolation of future receiver completion."""
    past = timeline.before(now)
    if any(e['event'] in STATE_EVENTS for e in timeline.at(now)):
        return dict(state='UNKNOWN', mode='UNKNOWN', reason='SAME_NS_ORDER_NOT_LOGGED')
    state = past['state'] if past else 'ABSENT'
    reason = ''
    mode = 'FETCH'
    remaining = None
    if past and int(past['target']) == target and satellite_available(target):
        if state == 'READY':
            mode, remaining = 'READY', 0
        elif satellite_available(source) and (
                state == 'IN_FLIGHT' or (state == 'REQUESTED' and source == target)):
            mode = 'IN_FLIGHT'
            remaining = 1 if state == 'REQUESTED' and source == target else None
    if mode == 'FETCH':
        if past and state == 'FAILED':
            reason = 'FAILED_PREFETCH_REFETCH'
        elif past and int(past['target']) != target and int(past['flow_id']):
            reason = 'WRONG_TARGET_REFETCH'
        elif state == 'REQUESTED':
            reason = 'PREFETCH_NOT_ESTABLISHED'
        elif state == 'RELEASED':
            reason = 'RELEASED_OBJECT_REQUIRES_FETCH'
    return dict(state=state, mode=mode, reason=reason, remaining_ns=remaining)


def policy_contract(final, timeline, now, remote, dependency):
    """Use committed actual admission and causal validity, never rerun the selector.

    admission_success is the recorded Request acceptance, not receiver readiness.
    Only Resolve's READY/IN_FLIGHT categories establish the model's zero term.
    Network REQUESTED still requires FETCH; same-ns ordering gaps stay unknown.
    """
    if final is None or final['start_committed'] != '1' or int(final['time_ns']) >= now:
        return 'LEGACY', 'NO_PRIOR_COMMITTED_SEND_CONTRACT'
    if int(final['remote']) != remote:
        return 'LEGACY', 'COMMITTED_TARGET_MISMATCH'
    if final['selective_dryrun_decision'] != 'SEND':
        return 'LEGACY', 'COMMITTED_DEFER'
    admission = final['runtime_prefetch_admission_success']
    if admission == '0':
        return 'LEGACY', 'PREFETCH_ADMISSION_FAILED'
    if admission != '1':
        return 'UNKNOWN', 'PREFETCH_ADMISSION_NOT_RECORDED'
    if dependency['state'] == 'UNKNOWN':
        return 'UNKNOWN', dependency['reason']
    past = timeline.before(now)
    if past is None:
        return 'UNKNOWN', 'ACCEPTED_SEND_LIFECYCLE_MISSING'
    if int(past['target']) != remote:
        return 'LEGACY', 'LIFECYCLE_TARGET_MISMATCH'
    if past['state'] in ('ABSENT', 'FAILED', 'RELEASED'):
        return 'LEGACY', 'CONTRACT_INVALIDATED'
    require(past['state'] in ('REQUESTED', 'IN_FLIGHT', 'READY'), 'unrecognized lifecycle state')
    if dependency['mode'] == 'FETCH':
        return 'LEGACY', 'NO_EFFECTIVE_PROACTIVE_DEPENDENCY'
    return 'SEND_VALID', 'COMMITTED_SAME_TARGET_SEND_NOT_INVALIDATED'


def catch_direction(contract, source, remote, input_bytes):
    if source == remote or not input_bytes or contract == 'LEGACY':
        return 'UNCHANGED'
    return 'DECREASE' if contract == 'SEND_VALID' else 'UNKNOWN'


def rank_key(row, score):
    return (score, int(row['propagation_ns']), int(row['candidate_node']))


def selection_bound(candidates, mutable_nodes):
    """Prove a winner using 0 <= R <= 1 and the frozen U/M/tie-break.

    Unresolved is not an action flip. Bounds are independent per candidate and
    may be loose; they never substitute invented probabilities for missing data.
    """
    legal = [r for r in candidates if r['feasible'] == '1']
    winners = [r for r in candidates if r['selected'] == '1']
    require(len(winners) == int(bool(legal)), 'invalid recorded selection')
    if not legal:
        return dict(status='UNCHANGED_PROVEN', challenger_nodes=[])
    winner = winners[0]
    expected = min(legal, key=lambda r: rank_key(r, float(r['bottleneck'])))
    require(winner == expected, 'recorded selection violates deterministic ranking')

    def bound(row, upper):
        r = float(row['recovery_conflict'])
        if int(row['candidate_node']) in mutable_nodes:
            r = 1.0 if upper else 0.0
        return rank_key(row, max(r, float(row['historical_utilization']),
                                float(row['storage_pressure'])))

    challenger_nodes = [int(r['candidate_node']) for r in legal if r is not winner
                        and bound(r, False) < bound(winner, True)]
    return dict(status='UNKNOWN' if challenger_nodes else 'UNCHANGED_PROVEN',
                challenger_nodes=challenger_nodes)


def analyze(root):
    placement = rows(root, 'n5c-placement-decisions.csv')
    protection = rows(root, 'protection-events.csv')
    start_rows = [r for r in protection if r['event'] == 'START']
    starts = {int(r['task_id']): r for r in start_rows}
    require(len(starts) == len(start_rows), 'multiple protection STARTs per task')
    task_history = timelines(rows(root, 'task-events.csv'), 'task_id', 'simulation_time_ns')
    input_history = timelines(rows(root, 'input-prefetch-events.csv'), 'task_id', 'time_ns')
    fault_history = timelines(rows(root, 'fault-events.csv'), 'node_id', 'simulation_time_ns')
    empty = Timeline([], 'time_ns')
    scene = REPO / 'contrib/satcompute/input/experiments/leo-66'
    # task120's arrival normalization does not change any definition used here.
    tasks = {t['task_id']: t for t in json.loads((scene/'workload/task-trace.json').read_text())['tasks']}
    rates = {r['node_id']: r['compute_rate_work_units_per_second']
             for r in json.loads((scene/'compute/compute-profile.json').read_text())['compute_nodes']}
    execution = json.loads((root/'execution.json').read_text())
    require(execution['input_policy'] == 'selective' and execution['pressure_model'] == 'cumulative',
            'audit scope is frozen Selective/CUMULATIVE Run A')
    require(execution['random_seed'] == 1 and execution['random_run'] == 11,
            'unexpected seed/run')
    require(json.loads((root/'execution-result.json').read_text())['returncode'] == 0,
            'incomplete execution')
    final_rows = [r for r in rows(root, 'compfrr-policy-aware-admission.csv')
                  if r['stage'] == 'FINAL_REVALIDATION' and r['start_committed'] == '1']
    final_contracts = {int(r['task_id']):r for r in final_rows}
    require(len(final_contracts) == len(final_rows) and set(final_contracts) == set(starts),
            'START/committed actual contract coverage mismatch')
    for task, final in final_contracts.items():
        require(final['remote'] == starts[task]['remote_node'] and
                final['time_ns'] == starts[task]['time_ns'], 'contract is not the actual committed pair')

    groups = defaultdict(list)
    observations, candidate_rows, decision_rows = [], [], []
    state_counts, mode_counts, direction_counts, contract_counts = (Counter() for _ in range(4))
    identity_unknown_slots = 0
    window_changes_proven = 0
    mutable_by_decision = defaultdict(set)
    for row in placement:
        require(row['variant'] == 'full', 'unexpected scoring variant')
        decision, now, remote, own = (int(row[k]) for k in ('decision_id','time_ns','candidate_node','task_id'))
        groups[decision].append(row)
        count = int(row['peer_count'])
        require(0 <= float(row['recovery_conflict']) <= 1, 'R outside probability bounds')
        if row['feasible'] == '1':
            require(float(row['bottleneck']) == max(float(row[k]) for k in
                    ('recovery_conflict','historical_utilization','storage_pressure')), 'score mismatch')
        if not count:
            continue

        def available(node):
            event = fault_history.get(node, empty).before(now)
            return event is None or event['satellite_available_after'] == 'true'

        pool = []
        for peer, start in starts.items():
            if peer == own or int(start['remote_node']) != remote or int(start['time_ns']) > now:
                continue
            if int(start['time_ns']) == now and row['trigger'] == 'FAULT_EPOCH':
                continue  # All fault-batch proposals precede START commits.
            history = task_history[peer]
            running = history.before(now)
            if not running or running['cause'] != 'COMPUTE_DISPATCH':
                continue
            definition = tasks[peer]
            rate = rates[definition['compute_node_id']]
            service_ns = (definition['compute_work_units'] * NS + rate - 1) // rate
            # Causal planned primary service end, not observed future completion.
            # QueryTaskPrediction rejects remainingNs <= 0 even on a same-ns boundary.
            if now >= int(running['simulation_time_ns']) + service_ns:
                continue
            dependency = dependency_from_prefix(input_history.get(peer, empty), now, remote,
                                                definition['source_node_id'], available)
            contract, contract_reason = policy_contract(final_contracts.get(peer),
                input_history.get(peer, empty), now, remote, dependency)
            direction = catch_direction(contract, definition['source_node_id'], remote,
                                        definition['input_bytes'])
            pool.append(dict(peer_task_id=peer, source=definition['source_node_id'],
                             input_bytes=definition['input_bytes'], **dependency,
                             policy_contract=contract, contract_reason=contract_reason,
                             hypothetical_input_term='0' if contract == 'SEND_VALID' else
                                 'LEGACY_S_OVER_B' if contract == 'LEGACY' else 'UNKNOWN',
                             catch_direction=direction))
        require(len(pool) >= count, f'cannot cover recorded peers: {decision}/{remote}')
        unique_membership = len(pool) == count
        if unique_membership:
            for item in pool:
                state_counts[item['state']] += 1
                mode_counts[item['mode']] += 1
                direction_counts[item['catch_direction']] += 1
                contract_counts[item['policy_contract']] += 1
        else:
            identity_unknown_slots += count
            # Same state across ALL possible members proves a count without inventing
            # which member passed BuildResources. Never take the first count entries.
            for key, total in (('state', state_counts), ('mode', mode_counts),
                               ('catch_direction', direction_counts), ('policy_contract', contract_counts)):
                values = {item[key] for item in pool}
                total[next(iter(values)) if len(values) == 1 else 'UNKNOWN'] += count
        all_unchanged = all(item['catch_direction'] == 'UNCHANGED' for item in pool)
        r_invariant = all_unchanged or float(row['first_failure_demand_probability']) == 0
        if not r_invariant:
            mutable_by_decision[decision].add(remote)
        # Positive old R with exactly one peer proves a nonempty weighted peer
        # window. Reducing catch leaves that window feasible but shortens its end.
        # A >1ns minimum reduction avoids a false claim from ceil-to-ns equality.
        window_change = (unique_membership and count == 1 and
            pool[0]['catch_direction'] == 'DECREASE' and float(row['recovery_conflict']) > 0 and
            pool[0]['input_bytes'] * 8 * NS > execution['isl_bandwidth_bps'])
        window_changes_proven += int(window_change)
        candidates = [p['peer_task_id'] for p in pool]
        for item in pool:
            observations.append(dict(decision_id=decision, task_id=own, time_ns=now,
                trigger=row['trigger'], remote=remote, recorded_peer_count=count,
                membership='EXACT' if unique_membership else 'POSSIBLE_NOT_COUNTED_INDIVIDUALLY',
                possible_peer_ids=candidates, **item))
        candidate_rows.append(dict(decision_id=decision, task_id=own, time_ns=now,
            remote=remote, peer_count=count, possible_peer_ids=candidates,
            membership_exact=unique_membership, legacy_R=float(row['recovery_conflict']),
            symmetric_R=float(row['recovery_conflict']) if r_invariant else None,
            peer_window_changed_proven=window_change,
            R_status='UNCHANGED_PROVEN' if r_invariant else 'UNKNOWN_MISSING_FORECAST_WINDOWS',
            hard_feasible_set_status='UNCHANGED_BY_PEER_ONLY_SUBSTITUTION'))

    for decision, candidates in sorted(groups.items()):
        first = candidates[0]
        winner = next((int(r['candidate_node']) for r in candidates if r['selected'] == '1'), None)
        bound = selection_bound(candidates, mutable_by_decision[decision])
        decision_rows.append(dict(decision_id=decision, task_id=int(first['task_id']),
            time_ns=int(first['time_ns']), legacy_selected_remote=winner,
            symmetric_selected_remote=winner if bound['status'] == 'UNCHANGED_PROVEN' else None,
            **bound))
    total_peers = sum(int(r['peer_count']) for r in placement)
    require(sum(mode_counts.values()) == total_peers, 'peer denominator mismatch')
    unknown_R = sum(len(v) for v in mutable_by_decision.values())
    unknown_selection = [r for r in decision_rows if r['status'] == 'UNKNOWN']
    known_unchanged_peers = direction_counts['UNCHANGED']
    summary = dict(
        status='POLICY_CONTRACT_SYMMETRY_PARTIAL_EVIDENCE', source_directory=str(root),
        execution_base_commit=execution['commit'], execution_dirty=execution['worktree_dirty'],
        decisions=len(groups), candidate_snapshots=len(placement),
        candidates_with_peers=len(candidate_rows), peer_observations=total_peers,
        state_counts=dict(state_counts), dependency_mode_counts=dict(mode_counts),
        policy_contract_counts=dict(contract_counts),
        catch_direction_counts=dict(direction_counts),
        membership_unknown_peer_slots=identity_unknown_slots,
        peer_windows_changed_exact=None,
        peer_windows_changed_bounds=[window_changes_proven, total_peers-known_unchanged_peers],
        R_changed_exact=None, R_changed_bounds=[0, unknown_R],
        R_unchanged_proven=len(placement)-unknown_R,
        selected_remote_changed_exact=None if unknown_selection else 0,
        selected_remote_changed_bounds=[0, len(unknown_selection)],
        selected_remote_unchanged_proven=len(groups)-len(unknown_selection),
        selection_unresolved_task_ids=[r['task_id'] for r in unknown_selection],
        hard_feasible_candidate_set_changed=0,
        hard_feasible_scope='Peer INPUT term only; own forecast, peer eligibility, constraints and solver frozen.',
        missing=['per-peer futureSteps and original occupancy windows',
                 'per-peer same-decision INPUT/backup path and cadence snapshots'],
        warning='Policy-contract comparison only: valid committed SEND=0, otherwise legacy S/B. '
                'Runtime remainingNs is NEVER used in P. Bounds are proof intervals, not measured flips. '
                'No claim is made about the downstream changed-run trajectory.')
    return summary, observations, candidate_rows, decision_rows


def file_inventory(root):
    return {str(p.relative_to(root)): (p.stat().st_size, p.stat().st_mtime_ns)
            for p in root.rglob('*') if p.is_file()}


def write_csv(path, data):
    if not data:
        return
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(data[0]))
        writer.writeheader()
        writer.writerows({k: json.dumps(v) if isinstance(v, (list, dict)) else v
                          for k,v in row.items()} for row in data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source_root.resolve(), args.output_dir.resolve()
    require(not output.is_relative_to(source) and not source.is_relative_to(output),
            'audit output must be separate from original evidence')
    require(not output.exists(), 'refuse to overwrite prior audit output')
    before = file_inventory(source)
    results = {name: analyze(source/name) for name in ('5Gbps-run11', '10Gbps-run11')}
    require(file_inventory(source) == before, 'source evidence inventory changed during audit')
    output.mkdir(parents=True)
    for name, (summary, observations, candidates, decisions) in results.items():
        write_csv(output/f'{name}-peer-observations.csv', observations)
        write_csv(output/f'{name}-candidate-bounds.csv', candidates)
        write_csv(output/f'{name}-selection-bounds.csv', decisions)
    receipt = dict(status='POLICY_CONTRACT_SYMMETRY_PARTIAL_EVIDENCE',
                   source_preservation='all file sizes and mtimes unchanged; no source writes',
                   source_file_count=len(before),
                   results={name: values[0] for name, values in results.items()})
    (output/'summary.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
