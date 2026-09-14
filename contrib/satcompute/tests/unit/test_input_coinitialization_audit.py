"""Causal time, fixed-rate potential and offline population contracts; no simulation."""
from pathlib import Path
import runpy
import tempfile
import unittest

TESTS = Path(__file__).resolve().parents[1]
API = runpy.run_path(str(TESTS/'support/protection/input_coinitialization_audit.py'))
BASE = runpy.run_path(str(Path(__file__).with_name('test_input_start_trace_audit.py')))


def snapshot():
    r = BASE['snapshot']()
    r.update(Kvar_bytes=200, cL_ns=100, cR_ns=200, initial_variable_state_bytes=10)
    r['input_path']['propagation_ns'] = 1000
    r['actual_post_batch_validation'].update(input_bandwidth_bytes_per_s=1000,
                                            backup_bandwidth_bytes_per_s=2000)
    return r


def same_time():
    return API['initializing_time'](*([10**9]*4), synchronous_contract=True)


def features():
    return API['timing_feature'](snapshot(), same_time())


def population():
    return [dict(features(), task_id=str(i), P_F=p, U_pot=p, M_pot=2*p-1,
                 G_I_pot_s=p, label=label, fault_source=source, critical_wait_ns=wait)
            for i, p, label, source, wait in ((1,.8,'NEEDED','F1',20),
                (2,.2,'NEEDED','F3',10), (3,.2,'NO_FAULT',None,0), (4,.1,'NEEDED','F2',5))]


class InputCoinitializationTests(unittest.TestCase):
    def test_all_ready_identity_does_not_split_equal_pf_ties(self):
        f = API['potential']([dict(time_ns=10**9, first_failure_mass=.1),
                              dict(time_ns=2*10**9, first_failure_mass=.2)], .3, 1, 0)
        self.assertEqual(f['U_pot'], .3)
        self.assertEqual(f['P_I_partial_pot'], 0)

    def test_same_ns_requires_code_contract(self):
        self.assertEqual(same_time()['status'], 'PASS')
        self.assertEqual(API['initializing_time'](*([10**9]*4))['status'], 'UNKNOWN')

    def test_delayed_known_init_time(self):
        t = API['initializing_time'](10,20,20,20, decision_known_init_ns=20)
        self.assertEqual((t['delta_ns'], t['causal_initializing_start_time_ns']), (10,20))

    def test_retrospective_delayed_admission_not_a_feature(self):
        t = API['initializing_time'](10**9,3*10**9,3*10**9,3*10**9, synchronous_contract=True)
        f = API['timing_feature'](snapshot(), t)
        self.assertIsNone(f['U_pot']); self.assertIsNotNone(f['P_F'])

    def test_bad_time_order_and_schedule_rejected(self):
        with self.assertRaises(ValueError): API['initializing_time'](10,9,10,10)
        with self.assertRaises(ValueError):
            API['initializing_time'](10,20,20,20, decision_known_init_ns=11)

    def test_weights_sum_to_pf_without_renormalization(self):
        f = features()
        self.assertAlmostEqual(f['P_I_full_pot']+f['P_I_partial_pot']+f['P_I_zero_lead_pot'], f['P_F'])

    def test_before_equal_after_input_ready(self):
        steps = [dict(time_ns=t, first_failure_mass=.2) for t in (5,10,15)]
        f = API['potential'](steps, .6, 1e-8, 0)
        self.assertAlmostEqual(f['U_pot'], .5)
        self.assertAlmostEqual(f['P_I_partial_pot'], .2)
        self.assertAlmostEqual(f['P_I_full_pot'], .4)

    def test_pre_init_and_zero_lead_mass_preserved(self):
        steps = [dict(time_ns=t, first_failure_mass=.2) for t in (5,10,15)]
        f = API['potential'](steps, .6, 1e-8, 10)
        self.assertAlmostEqual(f['P_I_zero_lead_pot'], .4)
        self.assertAlmostEqual(f['U_pot'], .1)

    def test_delayed_known_changes_lead_not_probability_window(self):
        t = API['initializing_time'](10**9,2500000000,2500000000,2500000000,
                                    decision_known_init_ns=2500000000)
        f = API['timing_feature'](snapshot(), t)
        self.assertAlmostEqual(f['P_F'], features()['P_F'])
        self.assertAlmostEqual(f['P_I_zero_lead_pot'], .28)
        self.assertAlmostEqual(f['U_pot'], .72*.28*.5)

    def test_bounds_and_gain_identity(self):
        for size in (1,1000,10000):
            r = snapshot(); r['input_bytes'] = size
            f = API['timing_feature'](r, same_time())
            self.assertTrue(0 <= f['U_pot'] <= f['P_F'] <= 1)
            self.assertAlmostEqual(f['G_I_pot_s'], f['T_I_s']*f['U_pot'])

    def test_propagation_and_cl_do_not_enter_main_scores(self):
        r = snapshot(); r['input_path']['propagation_ns'] = 10**12; r['cL_ns'] = 10**12
        f = API['timing_feature'](r, same_time())
        self.assertEqual([f[k] for k in API['SCORES']], [features()[k] for k in API['SCORES']])

    def test_local_normalized_scores_are_na_not_zero(self):
        r = snapshot(); r['remote_node'] = r['source_node']
        r['input_path'].update(remote_node=r['source_node'], local_delivery=True, hops=[], admitted_rate_bps=None)
        f = API['timing_feature'](r, same_time())
        self.assertIsNone(f['U_pot']); self.assertIsNone(f['M_pot'])
        self.assertEqual((f['network_bytes'], f['G_I_pot_s']), (0,0))

    def test_future_labels_cannot_change_features(self):
        r = snapshot(); r.update(label='NEEDED', fault_time_ns=2*10**9, input_critical_wait_ns=10**9,
                                 future_checkpoint_bytes=10, final_recovery_node=99)
        self.assertEqual(features(), API['timing_feature'](r, same_time()))

    def test_profile_stratifies_but_does_not_change_scores(self):
        r = snapshot(); r['profile'] = 'arbitrary-new-profile'
        f = API['timing_feature'](r, same_time())
        self.assertEqual([f[k] for k in API['SCORES']], [features()[k] for k in API['SCORES']])

    def test_unknown_predictor_not_zero(self):
        r = snapshot(); r['predictor'] = None
        self.assertIsNone(API['timing_feature'](r, same_time())['U_pot'])

    def test_actual_bandwidth_not_frequency_reference(self):
        r = snapshot(); r['frequency_proposal_inputs'] = dict(input_bandwidth_bytes_per_s=1)
        self.assertEqual(API['timing_feature'](r, same_time())['T_I_s'], 1)
        r['actual_post_batch_validation']['input_bandwidth_bytes_per_s'] = 1
        with self.assertRaises(ValueError): API['timing_feature'](r, same_time())

    def test_full_state_reference_is_not_future_tail(self):
        f = features()
        self.assertEqual(f['full_state_serialization_reference_s'], .1)
        for k in ('U_exact','G_I_exact_s','P_Iimpact','P_Iddl','checkpoint_state_tail_forecast'):
            self.assertIsNone(f[k])

    def test_fixed_screen_sign_and_necessary_condition(self):
        for p in (0,.2,.5,.8,1):
            f = API['potential']([dict(time_ns=10**9,first_failure_mass=p)], p, 1, 0)
            self.assertEqual(f['break_even_class'], API['POSSIBLE'] if p > .5 else API['REJECT'])

    def test_full_margin_ranking_reaches_all_negative_cuts(self):
        sweep = API['ranking'](population(), 'ALL_FAULT', 'M_pot')
        self.assertEqual(sweep[-1]['recall'], 1)
        self.assertLess(sweep[-1]['score_cut'], 0)
        self.assertEqual(API['fixed_screen'](population(), 'ALL_FAULT')['unreachable_recall_landmarks'], [.8,.9,1])

    def test_f3_excluded_but_no_fault_negatives_retained(self):
        members = dict(API['views'](population()))['F1_F2_PREDICTABLE']
        self.assertEqual([r['task_id'] for r in members], ['1','3','4'])

    def test_ties_not_split_by_id(self):
        sweep = API['ranking'](population(), 'ALL_FAULT', 'P_F')
        self.assertEqual([r['selected_task_count'] for r in sweep], [1,3,4])

    def test_all_four_scores_use_identical_population(self):
        for score in API['SCORES']:
            self.assertEqual(API['ranking'](population(), 'ALL_FAULT', score)[-1]['selected_task_count'], 4)
        with self.assertRaises(ValueError):
            API['ranking']([dict(population()[0], network_bytes=0)], 'ALL_FAULT', 'P_F')

    def test_no_cohort_pooling(self):
        with self.assertRaises(ValueError):
            API['join_labels']([dict(features(), cohort='OLD_FA_LRL')], [])

    def test_incomplete_mass_and_invalid_input_rejected(self):
        for s, pf in ((0,.2),(-1,.2),(float('inf'),.2),(1,.8)):
            with self.assertRaises(ValueError):
                API['potential']([dict(time_ns=1, first_failure_mass=.2)], pf, s, 0)

    def test_empty_reference_and_empty_strata_undefined(self):
        p = API['operating_point'](population(), [], 'ALL_FAULT', 'NONE_STAGE')
        self.assertIsNone(p['precision']); self.assertIsNone(p['byte_precision'])
        self.assertIsNone(API['distribution']([])['P50'])

    def test_quantile_interpolation_and_small_sample_flag(self):
        d = API['distribution']([1,2,3,4])
        self.assertEqual(d['P50'], 2.5); self.assertEqual(d['P10'], 1.3)
        self.assertTrue(d['small_sample'])

    def test_output_guard_preserves_raw_and_existing_directories(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            for out in (root, root/'nested'):
                with self.assertRaises(ValueError): API['API']['HISTORY']['output_guard'](out, [root])

    def test_end_to_end_population_tables_no_threshold_or_exact_surrogate(self):
        pop = population()
        refs = dict(views={})
        for view, members in API['views'](pop):
            points = API['ranking'](members, view, 'P_F')
            refs['views'][view] = dict(candidate_count=len(members), recall_landmarks=[
                dict(target_recall=level, point=dict(pf_cut=next(p['score_cut'] for p in points
                    if p['recall'] >= level))) for level in API['LEVELS']])
        tables, summary = API['analyze_population'](pop, refs)
        self.assertIsNone(summary['selected_production_threshold'])
        self.assertIsNone(summary['selected_production_score'])
        self.assertFalse(summary['selective_input_enabled'])
        self.assertEqual(summary['A1'], 'INCOMPLETE')
        marks = [r for r in tables['multi-score-landmarks'] if r['target_recall'] == 1]
        self.assertEqual(len(marks), 8)
        self.assertTrue(all(r['status'] == 'REACHABLE' for r in marks))
        self.assertEqual(len(tables['profile-selection-landmarks']), 8)

    def test_common_coverage_cannot_silently_drop_unknown_candidates(self):
        with self.assertRaises(ValueError):
            API['analyze_population']([dict(population()[0], U_pot=None)], {})

    def test_unknown_path_retains_probability_but_not_timing_score(self):
        r = snapshot(); r['input_path'].update(admissible=False, admitted_rate_bps=None, hops=[])
        f = API['timing_feature'](r, same_time())
        self.assertIsNotNone(f['P_F']); self.assertIsNone(f['U_pot'])

    def test_zero_lead_has_zero_benefit_despite_mass_roundoff(self):
        f = API['potential']([dict(time_ns=1, first_failure_mass=.1),
                              dict(time_ns=2, first_failure_mass=.2)], .30000000000000004, 1, 3)
        self.assertEqual(f['U_pot'], 0)


if __name__ == '__main__':
    unittest.main()
