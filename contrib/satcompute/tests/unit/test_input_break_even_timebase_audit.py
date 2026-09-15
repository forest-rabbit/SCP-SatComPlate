"""Fixed INPUT screens: timebases, causal isolation and monotone offline comparisons."""
from fractions import Fraction
from pathlib import Path
import csv
import runpy
from tempfile import TemporaryDirectory
import unittest

TESTS = Path(__file__).resolve().parents[1]
API = runpy.run_path(str(TESTS/'support/protection/input_break_even_timebase_audit.py'))


def row(tid='1', pf=.6, ser=10, prop=0, lead=100, **updates):
    steps = [dict(time_ns=lead, first_failure_mass=pf)]
    old = API['COINIT']['potential'](steps, pf, ser/1e9, 0)
    net = API['COINIT']['potential'](steps, pf, (ser+prop)/1e9, 0)
    return dict(dict(cohort=API['COINIT']['COHORT'], task_id=tid, profile='dense-image',
        P_F=pf, input_bytes=ser, network_bytes=ser, T_I_s=ser/1e9,
        initializing_start_time_ns=0, serialization_rounded_ns=ser,
        T_I_net_ns=ser+prop, T_I_prop_ns=prop,
        network_timing_status='KNOWN_CURRENT_PATH_POTENTIAL_NOT_ACTUAL_GAIN',
        G_I_net_pot_s=net['G_I_pot_s'], local_node=1, remote_node=2,
        _rate_bps=8*10**9, _steps=steps, label='NEEDED', fault_source='F1',
        critical_wait_ns=5, **old), **updates)


def feature(r):
    return API['comparison_feature'](r, r['_steps'])


class FormulaTests(unittest.TestCase):
    def test_zero_propagation_exactly_reduces_on_common_integer_timebase(self):
        for ser in (1, 3, 10, 10**12):
            for lead in (0, 1, ser//2, ser, ser+1):
                for pf in (0, .1, .5, .9, 1):
                    result = API['evaluate'](ser, ser, 0,
                        [dict(time_ns=lead, first_failure_mass=pf)], pf)
                    self.assertEqual(result['g_ser'], result['g_net'])
                    self.assertEqual(result['old_integer_action'], result['new_action'])

    def test_small_input_propagation_can_select_new_only(self):
        f = feature(row(pf=.1, ser=1, prop=1000))
        self.assertEqual((f['old_decision'], f['new_decision']), ('DEFERRED', 'PROACTIVE'))

    def test_large_input_small_propagation_keeps_clear_decisions(self):
        for pf in (.1, .4, .6, .9):
            f = feature(row(pf=pf, ser=10**8, prop=1000, lead=10**9))
            self.assertEqual(f['old_decision'], f['new_decision'])

    def test_zero_probability_remains_deferred(self):
        f = feature(row(pf=0, prop=10000))
        self.assertEqual(f['G_net_pot_ns'], '0')
        self.assertEqual(f['new_decision'], 'DEFERRED')

    def test_probability_one_positive_lead(self):
        self.assertEqual(feature(row(pf=1, lead=1))['new_decision'], 'PROACTIVE')

    def test_zero_lead_even_with_probability_one(self):
        self.assertEqual(feature(row(pf=1, lead=0))['new_decision'], 'DEFERRED')

    def test_before_start_has_no_lead(self):
        result = API['evaluate'](10, 20, 5, [dict(time_ns=3, first_failure_mass=.8)], .8)
        self.assertEqual(result['g_net'], 0)

    def test_partial_ready_not_full_input_credit(self):
        result = API['evaluate'](10, 20, 0, [dict(time_ns=3, first_failure_mass=.5)], .5)
        self.assertEqual(result['g_net'], Fraction(3, 2))
        self.assertFalse(result['new_action'])

    def test_full_ready_uses_unconditional_probability_once(self):
        result = API['evaluate'](10, 20, 0, [dict(time_ns=20, first_failure_mass=.5)], .5)
        self.assertEqual((result['g_net'], result['cost']), (10, 5))

    def test_equality_is_deferred_without_epsilon(self):
        self.assertEqual(feature(row(pf=.5))['new_decision'], 'DEFERRED')
        result = API['evaluate'](10, 30, 0, [dict(time_ns=30, first_failure_mass=.25)], .25)
        self.assertEqual(result['g_net'], result['cost'])
        self.assertFalse(result['new_action'])

    def test_empty_window_and_fractional_expected_ns(self):
        self.assertFalse(API['evaluate'](1, 2, 0, [], 0)['new_action'])
        result = API['evaluate'](1, 2, 0, [dict(time_ns=1, first_failure_mass=.1)], .1)
        self.assertEqual(result['g_net'], Fraction(1, 10))

    def test_multiple_steps_not_renormalized(self):
        steps = [dict(time_ns=1, first_failure_mass=.1), dict(time_ns=2, first_failure_mass=.2)]
        result = API['evaluate'](1, 2, 0, steps, .3)
        self.assertEqual((result['g_net'], result['cost'], result['mass_residual']),
                         (Fraction(1, 2), Fraction(7, 10), 0))

    def test_probability_representation_residual_is_not_hidden(self):
        result = API['evaluate'](10, 20, 0,
            [dict(time_ns=20, first_failure_mass=.30000000000000004)], .3)
        self.assertEqual(result['mass_residual'], Fraction(4, 10**17))

    def test_superset_invariant_across_propagation_and_leads(self):
        for prop in (0, 1, 100):
            for lead in range(20):
                for pf in (.01, .49, .5, .51, 1):
                    r = API['evaluate'](10, 10+prop, 0, [dict(time_ns=lead, first_failure_mass=pf)], pf)
                    self.assertFalse(r['old_integer_action'] and not r['new_action'])

    def test_bad_inputs_stop_not_silent_defer(self):
        for args in ((0, 1, 0, [], 0), (2, 1, 0, [], 0), (1, 2, -1, [], 0), (1, 2, 0, [], .5)):
            with self.assertRaises(ValueError): API['evaluate'](*args)
        for p in (-1, 2, float('nan')):
            with self.assertRaises(ValueError): API['evaluate'](1, 2, 0, [], p)


class CausalityAndTimebaseTests(unittest.TestCase):
    def test_labels_wait_profile_and_future_truth_do_not_enter_feature(self):
        original = row()
        changed = dict(original, label='NO_FAULT', fault_source='F3', critical_wait_ns=999,
            profile='llm', actual_fault_time_ns=99, actual_recovery_target=999,
            observed_A_ns=0, reference_remote=999)
        self.assertEqual(feature(original), feature(changed))

    def test_local_delivery_has_no_network_screen(self):
        f = feature(row(network_bytes=0))
        self.assertEqual(f['new_decision'], 'N/A_LOCAL')
        self.assertNotIn('extra_serialization_cost_ns', f)

    def test_unreachable_path_or_missing_data_is_unknown(self):
        for updates in (dict(T_I_net_ns=None), dict(_steps=None), dict(P_F=None),
                        dict(network_timing_status='UNKNOWN')):
            self.assertEqual(feature(row(**updates))['status'], 'UNKNOWN')

    def test_rounding_conflict_stops_without_overwriting_historical_action(self):
        r = row(pf=.61, ser=2, lead=1, input_bytes=3, network_bytes=3,
                _rate_bps=16*10**9, T_I_s=1.5e-9)
        r.update(API['COINIT']['potential'](r['_steps'], .61, 1.5e-9, 0))
        f = feature(r)
        self.assertEqual(f['fractional_ns_rounding'], '0.5')
        self.assertEqual(f['old_decision'], 'PROACTIVE')
        self.assertEqual(f['integer_old_decision'], 'DEFERRED')
        self.assertEqual(f['status'], 'NUMERIC_REVIEW_REQUIRED')
        _, summary = API['assemble']([r])
        self.assertEqual(summary['verdict'], 'D. INSUFFICIENT_EVIDENCE')
        self.assertFalse(summary['views'])

    def test_native_estimator_rounding_and_local_contract(self):
        probe = API['NET']['find_probe'](TESTS.parents[2])
        cases = [(3, 16*10**9, 0, 1, 0), (3, 16*10**9, 5, 1, 0),
                 (3, 0, 0, 1, 1), (3, 16*10**9, 5, 0, 0)]
        self.assertEqual(API['NET']['native_estimates'](probe, cases), [2, 7, 1, None])


class ReportTests(unittest.TestCase):
    def test_two_views_full_negatives_f3_and_local_kept_separate(self):
        pop = [row(), row('2', pf=.1, ser=1, prop=100),
               row('3', pf=.1, ser=1, prop=100, fault_source='F3'),
               row('4', pf=.1, ser=1, prop=100, label='NO_FAULT', critical_wait_ns=0),
               row('5', network_bytes=0, label='FAULT_NONCRITICAL', critical_wait_ns=0)]
        tables, summary = API['assemble'](pop)
        self.assertEqual((summary['total_candidates'], summary['network_candidates']), (5, 4))
        self.assertEqual(len(tables['local_delivery_audit']), 1)
        self.assertEqual(summary['views']['ALL_FAULT']['new']['selected_task_count'], 4)
        f12 = summary['views']['F1_F2_PREDICTABLE']['new']
        self.assertEqual((f12['candidate_count'], f12['no_fault_selected']), (3, 1))
        self.assertEqual(summary['verdict'], 'B. SMALL_REVISION_CROSS_TRADEOFF')
        self.assertIsNone(summary['production_threshold'])

    def test_new_no_fault_majority_does_not_override_positive_tradeoff(self):
        pop = [row('1', pf=.1, ser=1, prop=100)] + [
            row(str(i), pf=.1, ser=1, prop=100, label='NO_FAULT', critical_wait_ns=0)
            for i in range(2, 12)]
        _, summary = API['assemble'](pop)
        self.assertEqual(summary['verdict'], 'B. SMALL_REVISION_CROSS_TRADEOFF')

    def test_more_bytes_no_added_wait_is_dominated(self):
        _, summary = API['assemble']([row(pf=.1, ser=1, prop=100, label='NO_FAULT', critical_wait_ns=0)])
        self.assertEqual(summary['views']['ALL_FAULT']['tradeoff']['reason'], 'DOMINATED_NO_ADDED_WAIT')

    def test_identical_selection_no_strict_pareto_claim(self):
        _, summary = API['assemble']([row()])
        self.assertEqual(summary['views']['ALL_FAULT']['tradeoff']['reason'], 'IDENTICAL_SELECTION')

    def test_missing_candidate_does_not_shrink_denominator(self):
        _, summary = API['assemble']([row(), row('2', T_I_net_ns=None)])
        self.assertEqual(summary['network_candidates'], 2)
        self.assertEqual(summary['numeric_or_coverage_blockers'], ['2'])

    def test_unknown_first_row_diagnostics_can_be_written(self):
        tables, _ = API['assemble']([row('1', T_I_net_ns=None), row('2')])
        with TemporaryDirectory(prefix='scp-timebase-unit-') as temp:
            API['write_tables'](Path(temp), tables)
            with (Path(temp)/'formula_audit.csv').open(newline='') as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual([r['status'] for r in rows], ['UNKNOWN', 'PASS'])
            self.assertEqual(rows[0]['G_net_pot_ns'], '')
            self.assertNotEqual(rows[1]['G_net_pot_ns'], '')

    def test_invalid_cohort_and_duplicates_fail(self):
        for pop in ([row(cohort='OLDER_BASE')], [row(), row()]):
            with self.assertRaises(ValueError): API['assemble'](pop)

    def test_metrics_empty_selection_and_no_wait(self):
        _, summary = API['assemble']([row(pf=0, label='NO_FAULT', critical_wait_ns=0)])
        result = summary['views']['ALL_FAULT']['new']
        self.assertEqual(result['selected_task_count'], 0)
        self.assertIsNone(result['captured_wait_recall'])

    def test_no_audit_optimizer_or_runtime_entry(self):
        source = (TESTS/'support/protection/input_break_even_timebase_audit.py').read_text()
        for token in ('import random', 'Simulator::', 'Schedule(', 'SolveFrequency(',
                      'input_criticalpath_audit.py', 'input_causal_bound_audit.py', 'quantile('):
            self.assertNotIn(token, source)


if __name__ == '__main__':
    unittest.main()
