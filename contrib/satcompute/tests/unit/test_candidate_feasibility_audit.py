"""Offline evidence bounds: no simulator, fake flow or alternate-pair predictor."""
from fractions import Fraction as F
from pathlib import Path
import itertools
import json
import runpy
import unittest

TESTS = Path(__file__).resolve().parents[1]
AUDIT = runpy.run_path(str(TESTS/'integration/regression/audit-1g-candidate-feasibility.py'))
NS = 10**9


class CandidateFeasibilityAuditTests(unittest.TestCase):
    def row(self, **changes):
        return dict(dict(task_id='53', fault_epoch_time_ns='0', decision_trigger='TASK_RUNNING',
            progress_work='0', first_sample_time_ns=str(NS), remote_node='1',
            input_bandwidth_bytes_per_s='125000000', p_fail_before_finish='0.9'), **changes)

    def task(self, **changes):
        return dict(dict(compute_start_time_ns='0', source_node_id='43', input_bytes='125000000',
            compute_work_units='200000', compute_rate_work_units_per_second='100000'), **changes)

    def test_ser_bounds_cover_send_defer_and_unknown(self):
        bound = AUDIT['selector_bounds']
        self.assertEqual(bound(F(9, 10), 100, 100, 200)[0], 'SEND')
        self.assertEqual(bound(F(1, 10), 100, 0, 200)[0], 'DEFER')
        self.assertEqual(bound(F(9, 10), 100, 0, 200)[0], 'UNKNOWN')
        self.assertEqual(bound(F(0), 100, 0, 200)[0], 'DEFER')
        self.assertEqual(bound(F(1), 100, 0, 0)[0], 'DEFER')

    def test_representation_allowance_never_becomes_decision_epsilon(self):
        bound = AUDIT['selector_bounds']
        # Equality cannot be certified from rounded P_F without the full mass.
        self.assertEqual(bound(F(1, 2), 100, 100, 200)[0], 'UNKNOWN')
        self.assertEqual(bound(F(1, 2)+F(1, 10**13), 100, 100, 200)[0], 'UNKNOWN')
        self.assertEqual(bound(F(1, 2)+F(1, 10**9), 100, 100, 200)[0], 'SEND')

    def test_certified_decisions_match_every_synthetic_trajectory(self):
        seen = set()
        for qs in itertools.product((F(0), F(1, 10), F(1, 2), F(9, 10), F(1)), repeat=3):
            survival = F(1)
            weights = []
            for q in qs:
                weights.append(survival*q)
                survival *= 1-q
            p = sum(weights)
            for times, serial in itertools.product(((0, 5, 10), (5, 10, 15), (10, 20, 30)), (3, 10, 40)):
                decision, low, high, cost = AUDIT['selector_bounds'](p, serial, times[0], times[-1])
                gain = sum(w*min(serial, t) for w, t in zip(weights, times))
                self.assertLessEqual(low, gain)
                self.assertGreaterEqual(high, gain)
                seen.add(decision)
                if decision != 'UNKNOWN':
                    self.assertEqual(decision, 'SEND' if gain > cost else 'DEFER')
        self.assertEqual(seen, {'SEND', 'DEFER', 'UNKNOWN'})

    def test_initial_running_excludes_finish_sample(self):
        result = AUDIT['reference_selector'](self.row(input_bandwidth_bytes_per_s='12500000'), self.task(), NS)
        self.assertTrue(result['finish_exclusive'])
        self.assertEqual(result['selector_decision'], 'DEFER')
        self.assertAlmostEqual(result['gain_upper_ns']/NS, .9, places=8)
        self.assertEqual(result['hypothetical_actual_pair'], 'UNKNOWN')

    def test_certified_bounds_match_retained_causal_selector_anchor(self):
        candidates = json.loads((TESTS/'fixtures/protection/selective-input-ser-anchor.json').read_text())['candidates']
        certified = 0
        for row in candidates:
            path, predictor = row['input_path'], row['predictor']
            steps = predictor['future_steps']
            if path['local_delivery'] or not steps:
                continue
            serial = AUDIT['ceiling'](F(row['input_bytes']*8*NS, path['admitted_rate_bps']))
            start = row['start_time_ns']
            decision = AUDIT['selector_bounds'](AUDIT['number'](predictor['P_F']), serial,
                steps[0]['time_ns']-start, steps[-1]['time_ns']-start)[0]
            if decision != 'UNKNOWN':
                certified += 1
                self.assertEqual(decision == 'SEND', row['expected_send'], row['task_id'])
        self.assertGreater(certified, 0)

    def test_fault_epoch_and_noninitial_queries_stay_unknown(self):
        for changes in (dict(decision_trigger='FAULT_EPOCH'), dict(progress_work='1'),
                        dict(decision_trigger='CAPACITY_RELEASE'), dict(fault_epoch_time_ns='1')):
            result = AUDIT['reference_selector'](self.row(**changes), self.task(), NS)
            self.assertEqual(result['selector_decision'], 'UNKNOWN')
            self.assertEqual(result['finish_exclusive'], 'UNKNOWN')

    def test_local_delivery_is_target_specific_not_a_network_flow(self):
        task = self.task(source_node_id='1')
        local = AUDIT['reference_selector'](self.row(p_fail_before_finish='0'), task, NS)
        other = AUDIT['reference_selector'](self.row(p_fail_before_finish='0', remote_node='2'), task, NS)
        self.assertEqual(local['selector_decision'], 'SEND')
        self.assertEqual(local['serialization_estimate_ns'], 0)
        self.assertEqual(other['selector_decision'], 'DEFER')

    def test_missing_path_or_empty_query_not_invented(self):
        for changes in (dict(remote_node=''), dict(input_bandwidth_bytes_per_s=''),
                        dict(first_sample_time_ns=str(2*NS))):
            self.assertEqual(AUDIT['reference_selector'](self.row(**changes), self.task(), NS)
                             ['selector_decision'], 'UNKNOWN')

    def test_event_identity_includes_time_and_trigger(self):
        records = [self.row(), self.row(fault_epoch_time_ns='1'), self.row(decision_trigger='FAULT_EPOCH')]
        self.assertEqual(len(AUDIT['indexed'](records)), 3)
        self.assertEqual(len({r['task_id'] for r in records}), 1)
        with self.assertRaises((ValueError, RuntimeError, AssertionError)):
            AUDIT['indexed']([self.row(), self.row()])

    def test_skipped_candidates_and_other_run_success_are_not_proof(self):
        row = dict(pair_path_feasible='40', pair_hard_checked='1', proposal_reason='DEADLINE_INFEASIBLE')
        self.assertEqual(AUDIT['candidate_conclusion'](row), AUDIT['UNKNOWN'])
        row.update(fa_start_at_matching_event=True, fa_completed=True)
        self.assertEqual(AUDIT['candidate_conclusion'](row), AUDIT['UNKNOWN'])
        row.update(pair_hard_checked='40')
        self.assertEqual(AUDIT['candidate_conclusion'](row), 'PROVEN_EXHAUSTED_AT_THIS_EVENT')
        row.update(pair_hard_checked='0', pair_path_feasible='0')
        self.assertEqual(AUDIT['candidate_conclusion'](row), 'PROVEN_NO_ADMISSIBLE_PAIR_AT_THIS_EVENT')

    def test_unobserved_inflight_and_recovery_counts_remain_missing(self):
        result = AUDIT['summarize_selector']([dict(selector_decision='SEND', classification=AUDIT['UNKNOWN'])])
        self.assertIsNone(result['N_selective_in_flight'])
        self.assertIsNone(result['N_selective_still_infeasible'])
        self.assertEqual(result['N_provable_selective_false_rejection'], 0)
        self.assertEqual(result['N_unknown'], 1)

    def test_invalid_domains_fail_closed(self):
        for args in ((F(-1), 100, 0, 1), (F(2), 100, 0, 1), (F(1), 0, 0, 1), (F(1), 1, 2, 1)):
            with self.assertRaises((ValueError, RuntimeError, AssertionError)):
                AUDIT['selector_bounds'](*args)
        with self.assertRaises((ValueError, RuntimeError, AssertionError)):
            AUDIT['reference_selector'](self.row(), self.task(), 0)


if __name__ == '__main__':
    unittest.main()
