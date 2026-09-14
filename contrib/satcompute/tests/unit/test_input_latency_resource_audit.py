"""No simulation: attainable traffic/wait points and causal INPUT readiness contracts."""
from pathlib import Path
import runpy
import unittest

TESTS = Path(__file__).resolve().parents[1]
API = runpy.run_path(str(TESTS/'support/protection/input_latency_resource_audit.py'))
BASE = runpy.run_path(str(Path(__file__).with_name('test_input_coinitialization_audit.py')))


class TrafficWaitTests(unittest.TestCase):
    def test_complete_tables_keep_two_views_and_no_production_choice(self):
        extension=API['network_feature'](BASE['snapshot'](),BASE['features'](),1000001000)
        population=[dict(extension | r, G_I_net_pot_s=r['P_F']*1.000001) for r in BASE['population']()]
        tables,summary=API['assemble'](population)
        self.assertIsNone(summary['selected_production_score'])
        self.assertIsNone(summary['selected_production_threshold'])
        self.assertFalse(summary['binary_decision_ready'])
        self.assertFalse(summary['selective_input_enabled'])
        self.assertEqual(summary['A1'],'INCOMPLETE')
        self.assertEqual(len(tables['network-timing-landmarks']),30)
        self.assertEqual(summary['views']['F1_F2_PREDICTABLE']['network_candidates'],3)
        self.assertEqual(summary['views']['F1_F2_PREDICTABLE']['no_fault_count'],1)
        self.assertEqual(len(tables['reference-points']),14)
        self.assertEqual(set(summary['views']['ALL_FAULT']['comparison_summary']),
                         {'SERIAL_VS_PF','NET_VS_SERIAL','NET_VS_PF'})

    def test_missing_native_coverage_cannot_drop_candidates(self):
        with self.assertRaises(ValueError):
            API['assemble']([dict(BASE['population']()[0],network_timing_status='UNKNOWN')])

    def test_mixed_historical_cohorts_rejected(self):
        with self.assertRaises(ValueError):
            API['assemble']([dict(BASE['population']()[0],cohort='OLD_FA_LRL')])

    def test_stage_b_identity_gate_still_rejects_changed_execution(self):
        with self.assertRaises(ValueError):
            API['COINIT']['TRACE']['execution_identity'](dict(purpose='FORMAL_PERFORMANCE'),{},{},{})

    def test_pareto_keeps_actual_points_and_duplicate_identities(self):
        points = [dict(planned_staged_network_bytes=b, captured_actual_critical_wait_ns=w)
                  for b,w in ((0,0),(1,2),(2,2),(3,3),(3,3),(3,2),(4,4))]
        self.assertEqual(API['frontier'](points), [True,True,False,True,True,False,True])

    def test_landmarks_use_waiting_not_task_count(self):
        result = API['wait_analysis'](BASE['population'](), 'ALL_FAULT', ['P_F'], 'SERIAL')
        mark = result['landmarks'][0]
        self.assertEqual(mark['target_wait_coverage'], .8)
        self.assertEqual(mark['selected_task_count'], 3)
        self.assertEqual(mark['recall'], 2/3)
        self.assertGreaterEqual(mark['captured_wait_recall'], .8)

    def test_ties_cannot_be_split_to_hit_landmark(self):
        result = API['wait_analysis'](BASE['population'](), 'ALL_FAULT', ['P_F'], 'SERIAL')
        self.assertEqual([p['selected_task_count'] for p in result['sweeps']], [0,1,3,4])

    def test_positive_wait_targets_compare_all_attainable_levels(self):
        result = API['wait_analysis'](BASE['population'](), 'ALL_FAULT', ['P_F','U_pot'], 'SERIAL')
        comparison = API['compare_wait_curves'](result['by_score']['P_F'], result['by_score']['U_pot'],
                                               'ALL_FAULT', 'IDENTICAL')
        self.assertEqual([r['target_captured_wait_ns'] for r in comparison], [20,30,35])
        self.assertTrue(all(r['delta_planned_bytes'] == 0 for r in comparison))

    def test_oracle_not_in_causal_pareto(self):
        result = API['wait_analysis'](BASE['population'](), 'ALL_FAULT', ['P_F'], 'SERIAL')
        self.assertFalse(any(r['operating_point'] == 'ORACLE_NEEDED' for r in result['sweeps']))
        self.assertEqual(result['references'][-1]['operating_point'], 'ORACLE_NEEDED')

    def test_undefined_waiting_denominator_not_zero(self):
        with self.assertRaises(ValueError):
            API['wait_analysis']([dict(r, label='NO_FAULT', critical_wait_ns=0)
                                  for r in BASE['population']()], 'ALL_FAULT', ['P_F'], 'SERIAL')

    def test_first_cover_does_not_interpolate(self):
        points = [dict(captured_actual_critical_wait_ns=n) for n in (0,5,15)]
        self.assertEqual(API['first_cover'](points, 10)['captured_actual_critical_wait_ns'],15)

    def test_integer_coverage_does_not_round_down(self):
        result = API['wait_analysis'](BASE['population'](), 'ALL_FAULT', ['P_F'], 'SERIAL')
        mark = result['landmarks'][1]
        self.assertEqual(mark['required_wait_ns'], 32)
        self.assertEqual(mark['selected_task_count'], 4)


class NetworkPotentialTests(unittest.TestCase):
    def feature(self, ns=1000001000, r=None, base=None):
        return API['network_feature'](r or BASE['snapshot'](), base or BASE['features'](), ns)

    def test_separate_serialization_propagation_and_completion(self):
        f = self.feature()
        self.assertEqual(f['T_I_ser_s'], 1)
        self.assertEqual(f['T_I_prop_ns'], 1000)
        self.assertEqual(f['serialization_rounded_ns'], 10**9)
        self.assertEqual(f['T_I_net_ns'], 1000001000)

    def test_before_at_after_net_ready(self):
        for ns, full in ((3*10**9,0),(2*10**9,.72*.28),(10**9,.4816)):
            f = self.feature(ns)
            self.assertAlmostEqual(f['P_I_net_full_pot'], full)

    def test_normalized_bound_and_time_identity(self):
        for ns in (10**9,2*10**9,3*10**9):
            f = self.feature(ns)
            self.assertTrue(0 <= f['U_net_pot'] <= f['P_F'] <= 1)
            self.assertAlmostEqual(f['G_I_net_pot_s'], f['T_I_net_s']*f['U_net_pot'])

    def test_full_partial_zero_mass_conserved(self):
        f = self.feature(3*10**9)
        self.assertAlmostEqual(sum(f[k] for k in ('P_I_net_full_pot','P_I_net_partial_pot','P_I_net_zero_lead_pot')),
                               f['P_F'])

    def test_propagation_changes_new_scores_not_old_scores(self):
        r = BASE['snapshot'](); r['input_path']['propagation_ns'] = 2*10**9
        f = self.feature(3*10**9, r)
        old = BASE['features']()
        self.assertEqual([f[k] for k in API['COINIT']['SCORES']], [old[k] for k in API['COINIT']['SCORES']])
        self.assertGreater(f['G_I_net_pot_s'], old['G_I_pot_s'])
        self.assertLess(f['U_net_pot'], old['U_pot'])

    def test_pre_initialization_mass_not_discarded(self):
        r = BASE['snapshot'](); c = API['COINIT']
        t = c['initializing_time'](10**9,2500000000,2500000000,2500000000,
                                   decision_known_init_ns=2500000000)
        base = c['timing_feature'](r,t)
        f = self.feature(10**9,r,base)
        self.assertAlmostEqual(f['P_I_net_zero_lead_pot'],.28)
        self.assertAlmostEqual(f['U_net_pot'], .72*.28*.5)

    def test_unknown_admission_time_not_filled_by_actual_future(self):
        f = self.feature(base=dict(BASE['features'](),initializing_start_time_ns=None))
        self.assertIsNone(f['G_I_net_pot_s'])

    def test_unknown_native_estimate_not_replaced_by_zero(self):
        f = self.feature(None)
        self.assertIsNone(f['T_I_net_ns']); self.assertIsNone(f['U_net_pot'])

    def test_local_one_ns_not_network_time(self):
        r=BASE['snapshot'](); r['remote_node']=r['source_node']
        r['input_path'].update(remote_node=r['source_node'],local_delivery=True,hops=[],admitted_rate_bps=None)
        base=API['COINIT']['timing_feature'](r,BASE['same_time']())
        f=self.feature(1,r,base)
        self.assertEqual(f['local_delivery_boundary_ns'],1)
        self.assertEqual(f['network_bytes'],0)
        self.assertIsNone(f['U_net_pot']); self.assertIsNone(f['T_I_net_s'])

    def test_outcomes_do_not_enter_new_features(self):
        r=BASE['snapshot'](); r.update(label='NEEDED',fault_time_ns=2*10**9,final_recovery_node=99)
        self.assertEqual(self.feature(),self.feature(r=r))

    def test_profile_is_not_score_input(self):
        r=BASE['snapshot'](); r['profile']='new-profile'
        f=self.feature(r=r,base=dict(BASE['features'](),profile='new-profile'))
        self.assertEqual(f['G_I_net_pot_s'],self.feature()['G_I_net_pot_s'])

    def test_exact_recovery_values_remain_unknown(self):
        for k in ('U_exact','G_I_exact_s','P_Iimpact','P_Iddl','checkpoint_state_tail_forecast'):
            self.assertIsNone(self.feature()[k])

    def test_native_bridge_never_starts_simulator_or_live_path_query(self):
        text=(TESTS/'support/protection/input-transfer-estimate.cc').read_text()
        self.assertIn('estimate.TransferTimeNs(bytes)',text)
        for token in ('Simulator::','EstimateAdmissiblePath(', 'CreateObject<', 'StartTransfer', 'GetValue(', 'Schedule('):
            self.assertNotIn(token,text)


class NativeEstimatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.probe=API['find_probe'](TESTS.parents[2])
        except ValueError as error:
            raise unittest.SkipTest(str(error))

    def test_native_rounding_and_propagation(self):
        got=API['native_estimates'](self.probe,[(1,10000000000,1000000,True,False),
                                              (1000,8000,1000,True,False)])
        self.assertEqual(got,[1000001,1000001000])

    def test_native_local_zero_and_unavailable(self):
        got=API['native_estimates'](self.probe,[(100,0,0,True,True),(0,0,0,True,True),
                                              (100,1,0,False,False)])
        self.assertEqual(got,[1,0,None])

    def test_native_zero_rate_negative_propagation_overflow(self):
        got=API['native_estimates'](self.probe,[(100,0,0,True,False),(100,1,-1,True,False),
                                              (2**64-1,1,0,True,False)])
        self.assertEqual(got,[None,None,None])

    def test_native_integer_formula_oracle(self):
        cases=[(s,r,p,True,False) for s,r,p in ((325,10000000000,1000000),
            (845,10000000000,8000000),(1000000000,10000000000,10000000),(7,11,13))]
        self.assertEqual(API['native_estimates'](self.probe,cases),
                         [(s*8000000000+r-1)//r+p for s,r,p,_,_ in cases])


if __name__ == '__main__':
    unittest.main()
