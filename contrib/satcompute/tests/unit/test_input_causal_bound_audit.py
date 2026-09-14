"""Causal-upper proof obligations and conservative benefit; no network simulation."""
from fractions import Fraction
from pathlib import Path
import runpy
import subprocess
import unittest

TESTS = Path(__file__).resolve().parents[1]
API = runpy.run_path(str(TESTS/'support/protection/input_causal_bound_audit.py'))
BASE = runpy.run_path(str(Path(__file__).with_name('test_input_partial_predictability_audit.py')))


def row(tid='1', **updates):
    return dict(BASE['row'](tid, local_node=1, delta_permille=100, batch_n=2,
                          Kvar_bytes=100., backup_bandwidth_bytes_per_s=10**9,
                          cR_ns=1, G_cp_s=2e-9), **updates)


class BenefitBoundTests(unittest.TestCase):
    def bounds(self, uppers, steps=None, input_ns=10, start=0):
        return API['aggregate_bounds'](input_ns, start,
            steps or [dict(time_ns=10, first_failure_mass=.5)], uppers)

    def test_gain_monotone_and_nonnegative(self):
        for td in range(10):
            for ts in range(td+1):
                values = [API['gain_ns'](td, ts, a) for a in range(15)]
                self.assertTrue(all(a >= b >= 0 for a, b in zip(values, values[1:])))
                self.assertEqual(values[0], td-ts)

    def test_valid_upper_produces_lower_bound(self):
        b = self.bounds([6])
        for actual in range(7):
            actual_g = Fraction(1, 2)*API['gain_ns'](10, 0, actual)
            self.assertLessEqual(b['lower'], actual_g)
            self.assertGreaterEqual(b['upper'], actual_g)

    def test_unknown_does_not_become_zero_barrier(self):
        b = self.bounds([None])
        self.assertEqual((b['lower'], b['upper'], b['unknown_mass']), (0, 5, Fraction(1, 2)))
        self.assertEqual((b['sign'], b['action']), ('SIGN_UNCERTAIN', 'DEFER'))

    def test_known_and_unknown_unconditional_mass(self):
        b = self.bounds([2, None], [dict(time_ns=10, first_failure_mass=.2),
                                    dict(time_ns=20, first_failure_mass=.3)])
        self.assertEqual(b['lower'], Fraction(8, 5))
        self.assertEqual(b['upper'], 5)
        self.assertEqual(b['mass'], Fraction(1, 2))
        self.assertEqual(b['action'], 'SEND')  # Unknown additive terms do not erase a proved positive term.

    def test_tiny_positive_expected_ns_never_rounded_to_zero(self):
        b = self.bounds([9], [dict(time_ns=10, first_failure_mass=1e-300)])
        self.assertGreater(b['lower'], 0)
        self.assertEqual(b['action'], 'SEND')
        self.assertEqual(float(API['seconds'](b['lower'])), 1e-309)

    def test_below_float_range_keeps_sign_and_text(self):
        b = self.bounds([9], [dict(time_ns=10, first_failure_mass=5e-324)])
        self.assertEqual(b['action'], 'SEND')
        self.assertEqual(API['seconds'](b['lower']), '5E-333')

    def test_zero_probability_is_zero_not_uncertain(self):
        b = self.bounds([None], [dict(time_ns=10, first_failure_mass=0)])
        self.assertEqual((b['sign'], b['action']), ('ROBUST_ZERO', 'DEFER'))

    def test_no_lead_is_zero_even_if_barrier_unknown(self):
        for t in (0, 1):
            b = self.bounds([None], [dict(time_ns=t, first_failure_mass=.5)], start=1)
            self.assertEqual(b['sign'], 'ROBUST_ZERO')

    def test_partial_lead_limits_upper_gain(self):
        b = self.bounds([None], [dict(time_ns=3, first_failure_mass=.5)])
        self.assertEqual(b['upper'], Fraction(3, 2))

    def test_bound_equal_input_has_zero_lower_not_proven_zero(self):
        b = self.bounds([10])
        self.assertEqual(b['lower'], 0)
        self.assertEqual(b['sign'], 'SIGN_UNCERTAIN')  # Only an upper bound on A, no lower bound.

    def test_empty_probability_window(self):
        b = API['aggregate_bounds'](10, 0, [], [])
        self.assertEqual((b['sign'], b['action']), ('ROBUST_ZERO', 'DEFER'))

    def test_invalid_inputs_rejected(self):
        for args in ((10, 11, 0), (10, 0, -1), (10., 0, 1)):
            with self.assertRaises(ValueError): API['gain_ns'](*args)
        with self.assertRaises(ValueError): self.bounds([])
        with self.assertRaises(ValueError): self.bounds([1], [dict(time_ns=1, first_failure_mass=2)])


class FormulaAndCausalTests(unittest.TestCase):
    def test_formula_units_and_ceiling(self):
        self.assertEqual(API['proposed_upper_ns'](row()), 11)
        r = row(backup_bandwidth_bytes_per_s=3*10**9)
        self.assertEqual(API['proposed_upper_ns'](r), 5)

    def test_n_one_still_has_cr(self):
        self.assertEqual(API['proposed_upper_ns'](row(batch_n=1)), 1)

    def test_formula_never_installed_as_causal_certificate(self):
        f, _ = API['causal_features'](row(T_I_net_ns=100))
        self.assertTrue(f['assumed_formula_positive'])
        self.assertIsNone(f['causal_A_upper_ns'])
        self.assertEqual(f['causal_G_lower_s'], '0')
        self.assertEqual(f['causal_candidate_action'], 'DEFER')

    def test_early_mass_remains_unknown_for_diagnostic_too(self):
        f, _ = API['causal_features'](row(estimated_init_ready_time_ns=10))
        self.assertEqual(f['early_unknown_mass'], .5)
        self.assertFalse(f['assumed_formula_positive'])

    def test_profile_label_and_fault_do_not_enter_features(self):
        a = API['causal_features'](row())
        b = API['causal_features'](row(profile='llm', label='NO_FAULT', fault_source='F3', critical_wait_ns=999))
        self.assertEqual(a, b)

    def test_reference_pair_and_future_events_not_used(self):
        self.assertEqual(API['causal_features'](row()), API['causal_features'](row(
            frequency_reference_pair={'remote':999}, future_checkpoint_receipts=[1,2],
            future_recovery_target=999, observed_A=0)))

    def test_point_unknown_not_imputed(self):
        f, _ = API['causal_features'](row(G_cp_s=None))
        self.assertEqual(f['point_class'], 'UNKNOWN')

    def test_mass_mismatch_rejected(self):
        with self.assertRaises(ValueError): API['causal_features'](row(P_F=.1))

    def test_future_metadata_not_turned_into_certificate(self):
        f, _ = API['causal_features'](row(causal_upper_guarantee_ns=1, residual_epsilon_ns=0))
        self.assertEqual(f['causal_candidate_action'], 'DEFER')


class ReportTests(unittest.TestCase):
    def test_full_population_negative_labels_and_f3_views(self):
        pop = [row(), row('2', label='NO_FAULT', critical_wait_ns=0), row('3', fault_source='F3')]
        tables, s = API['assemble'](pop, [])
        all_row = s['views']['ALL_FAULT'][0]
        f12 = s['views']['F1_F2_PREDICTABLE'][0]
        self.assertEqual((all_row['candidate_count'], f12['candidate_count']), (3, 2))
        self.assertEqual((all_row['selected_count'], all_row['planned_staged_application_bytes']), (0, 0))
        self.assertEqual((all_row['captured_wait_share'], all_row['uncovered_wait_ns']), (0, 8))
        self.assertIsNone(s['production_rule'])
        self.assertIsNone(s['epsilon'])
        self.assertEqual(len(tables['gi-causal-bound-branches']), 3)

    def test_realized_violation_is_audit_not_input(self):
        r = row()
        tables, _ = API['assemble']([r], [BASE['recovery']() | dict(local_work_units='10', remote_work_units='2')])
        obs = tables['a-proposed-bound-observation-audit'][0]
        self.assertEqual(obs['observed_gap_wu'], 8)
        self.assertFalse(obs['observed_exceeds_proposal'])
        changed, _ = API['assemble']([r], [])
        self.assertEqual(tables['gi-causal-bound-features'], changed['gi-causal-bound-features'])

    def test_local_delivery_not_network_candidate(self):
        with self.assertRaises(ValueError): API['assemble']([row(network_bytes=0)], [])

    def test_zero_wait_denominator_undefined(self):
        result = API['selection_metrics']([row(label='NO_FAULT', critical_wait_ns=0)], [])
        self.assertIsNone(result['captured_wait_share'])

    def test_uncertain_class_bytes_not_reported_as_sent(self):
        tables, _ = API['assemble']([row()], [])
        cls = next(r for r in tables['gi-sign-class-summary'] if
                   r['view'] == 'ALL_FAULT' and r['sign_class'] == 'SIGN_UNCERTAIN')
        self.assertEqual(cls['member_input_bytes'], 100)
        self.assertNotIn('planned_staged_application_bytes', cls)
        rule = tables['gi-admission-summary'][0]
        self.assertEqual(rule['planned_staged_application_bytes'], 0)

    def test_no_runtime_solver_or_empirical_threshold(self):
        source = (TESTS/'support/protection/input_causal_bound_audit.py').read_text()
        for forbidden in ('import random', 'Simulator::', 'Schedule(', 'residual_epsilon_ns',
                          'SolveFrequency(', 'StartTransfer(', 'quantile('):
            self.assertNotIn(forbidden, source)


class NativeProofTests(unittest.TestCase):
    def test_real_progress_and_layout_counterexamples_without_simulation(self):
        probe = API['NET']['find_probe'](TESTS.parents[2])
        result = subprocess.run([str(probe), '--checkpoint-bound-witness'], text=True,
                                capture_output=True, check=True)
        rows = [line.split('\t') for line in result.stdout.splitlines()]
        self.assertEqual(len(rows), 7)
        self.assertTrue(all(int(r[1]) > int(r[2]) for r in rows))
        self.assertEqual({r[0] for r in rows}, {
            'n_one_cannot_bound_pending_gap_wu', 'nth_receipt_gap_wu',
            'local_receipt_during_remote_merge_wu', 'same_ns_commit_not_fault_valid_wu',
            'blocked_remote_gap_wu', 'application_boundary_exceeds_fraction_wu', 'record_includes_header_bytes'})


if __name__ == '__main__':
    unittest.main()
