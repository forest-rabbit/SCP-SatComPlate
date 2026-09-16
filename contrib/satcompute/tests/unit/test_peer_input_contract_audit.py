"""Committed policy symmetry and causal evidence gates for the peer INPUT audit."""
from pathlib import Path
import runpy
import unittest


AUDIT = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                          'support/protection/peer_input_contract_audit.py'))
Timeline = AUDIT['Timeline']
resolve = AUDIT['dependency_from_prefix']


def event(time, state, name, target=4, flow=7):
    return dict(time_ns=str(time), state=state, event=name,
                target=str(target), flow_id=str(flow))


def candidate(node, r=0, u=.1, m=.05, selected=False, hops=1):
    return dict(candidate_node=str(node), recovery_conflict=str(r),
                historical_utilization=str(u), storage_pressure=str(m),
                bottleneck=str(max(r,u,m)), propagation_ns=str(hops*1000000),
                feasible='1', selected=str(int(selected)))


class PeerInputContractAuditTests(unittest.TestCase):
    def dependency(self, events, now=20, target=4, source=1, available=lambda _: True):
        return resolve(Timeline(events, 'time_ns'), now, target, source, available)

    def test_future_ready_never_makes_current_input_ready(self):
        events = [event(10, 'IN_FLIGHT', 'PREFETCH_STARTED'),
                  event(30, 'READY', 'PREFETCH_READY')]
        result = self.dependency(events)
        self.assertEqual(result['mode'], 'IN_FLIGHT')
        self.assertIsNone(result['remaining_ns'])
        # Moving a future completion must not change a present estimate.
        events[-1]['time_ns'] = '999999'
        self.assertEqual(self.dependency(events), result)

    def test_ready_survives_source_unavailability(self):
        result = self.dependency([event(10, 'READY', 'PREFETCH_READY')],
                                 available=lambda node: node != 1)
        self.assertEqual((result['mode'], result['remaining_ns']), ('READY', 0))

    def test_registered_but_unadmitted_network_request_requires_fetch(self):
        result = self.dependency([event(10, 'REQUESTED', 'PREFETCH_REQUESTED')])
        self.assertEqual(result['mode'], 'FETCH')
        self.assertEqual(result['reason'], 'PREFETCH_NOT_ESTABLISHED')
        self.assertIsNone(result['remaining_ns'])

    def test_pending_local_delivery_uses_one_nanosecond(self):
        result = self.dependency([event(10, 'REQUESTED', 'PREFETCH_LOCAL_STARTED', flow=0)], source=4)
        self.assertEqual((result['mode'], result['remaining_ns']), ('IN_FLIGHT', 1))

    def test_failed_and_wrong_target_are_not_ready(self):
        failed = self.dependency([event(10, 'FAILED', 'PREFETCH_FAILED')])
        wrong = self.dependency([event(10, 'READY', 'PREFETCH_READY', target=9)])
        self.assertEqual((failed['mode'], failed['reason']), ('FETCH','FAILED_PREFETCH_REFETCH'))
        self.assertEqual((wrong['mode'], wrong['reason']), ('FETCH','WRONG_TARGET_REFETCH'))

    def test_same_ns_receiver_completion_is_not_ordered_by_guess(self):
        result = self.dependency([event(10, 'IN_FLIGHT', 'PREFETCH_STARTED'),
                                 event(20, 'READY', 'PREFETCH_READY')])
        self.assertEqual(result['mode'], 'UNKNOWN')

    def test_same_ns_fault_marker_does_not_read_postfault_state(self):
        result = self.dependency([event(10, 'READY', 'PREFETCH_READY'),
                                 event(20, 'READY', 'PREFETCH_FAULT_SNAPSHOT')])
        self.assertEqual(result['mode'], 'READY')

    def test_model_term_uses_policy_and_never_runtime_remaining(self):
        direction = AUDIT['catch_direction']
        self.assertEqual(direction('LEGACY', 1, 4, 100), 'UNCHANGED')
        self.assertEqual(direction('SEND_VALID', 1, 4, 100), 'DECREASE')
        self.assertEqual(direction('SEND_VALID', 4, 4, 100), 'UNCHANGED')
        self.assertEqual(direction('UNKNOWN', 1, 4, 100), 'UNKNOWN')

    def contract(self, events, final=None):
        committed = dict(start_committed='1', time_ns='5', remote='4',
                         selective_dryrun_decision='SEND', runtime_prefetch_admission_success='1')
        if final:
            committed.update(final)
        timeline = Timeline(events, 'time_ns')
        dependency = self.dependency(events)
        return AUDIT['policy_contract'](committed, timeline, 20, 4, dependency)[0]

    def test_same_contract_applies_to_ready_and_inflight(self):
        for state, name in [('READY','PREFETCH_READY'), ('IN_FLIGHT','PREFETCH_STARTED')]:
            self.assertEqual(self.contract([event(10,state,name)]), 'SEND_VALID')

    def test_network_requested_is_fetch_not_a_zero_model_term(self):
        self.assertEqual(self.contract([event(10,'REQUESTED','PREFETCH_REQUESTED')]), 'LEGACY')

    def test_failed_or_released_send_returns_to_legacy(self):
        for state, name in [('FAILED','PREFETCH_FAILED'), ('RELEASED','PREFETCH_RELEASED'),
                            ('ABSENT','PREFETCH_NOT_ADMITTED')]:
            self.assertEqual(self.contract([event(10,state,name)]), 'LEGACY')

    def test_no_send_or_failed_admission_cannot_invent_zero(self):
        ready = [event(10,'READY','PREFETCH_READY')]
        for override in [dict(selective_dryrun_decision='DEFER'),dict(start_committed='0'),
                         dict(runtime_prefetch_admission_success='0'),dict(remote='9')]:
            self.assertEqual(self.contract(ready,override), 'LEGACY')

    def test_future_failure_does_not_retroactively_invalidate_contract(self):
        events = [event(10,'IN_FLIGHT','PREFETCH_STARTED'),event(30,'FAILED','PREFETCH_FAILED')]
        self.assertEqual(self.contract(events), 'SEND_VALID')

    def test_missing_lifecycle_or_admission_remains_unknown(self):
        self.assertEqual(self.contract([]), 'UNKNOWN')
        self.assertEqual(self.contract([event(10,'READY','PREFETCH_READY')],
                                       dict(runtime_prefetch_admission_success='')), 'UNKNOWN')

    def test_um_floor_proves_winner_without_inventing_r(self):
        candidates = [candidate(1, selected=True), candidate(2,r=.8,u=.2)]
        self.assertEqual(AUDIT['selection_bound'](candidates,{2})['status'], 'UNCHANGED_PROVEN')

    def test_possible_flip_is_unknown_not_measured_change(self):
        candidates = [candidate(1,u=.3,selected=True),candidate(2,r=.8,u=.1)]
        result = AUDIT['selection_bound'](candidates,{2})
        self.assertEqual(result,dict(status='UNKNOWN',challenger_nodes=[2]))

    def test_selected_peer_can_lose_when_r_increases(self):
        candidates = [candidate(1,selected=True),candidate(2,u=.2)]
        self.assertEqual(AUDIT['selection_bound'](candidates,{1})['status'], 'UNKNOWN')

    def test_tie_break_uses_propagation_then_stable_id(self):
        candidates = [candidate(1,selected=True),candidate(2,r=.8)]
        self.assertEqual(AUDIT['selection_bound'](candidates,{2})['status'], 'UNCHANGED_PROVEN')
        candidates = [candidate(2,selected=True,hops=2),candidate(1,r=.8)]
        self.assertEqual(AUDIT['selection_bound'](candidates,{1})['status'], 'UNKNOWN')


if __name__ == '__main__':
    unittest.main()
