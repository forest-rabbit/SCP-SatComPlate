"""Mean-dependency offline audit contracts; no simulation or future state reconstruction."""
from pathlib import Path
import runpy
import unittest

TESTS = Path(__file__).resolve().parents[1]
API = runpy.run_path(str(TESTS/'support/protection/input_criticalpath_audit.py'))
BASE = runpy.run_path(str(Path(__file__).with_name('test_input_coinitialization_audit.py')))


def snapshot():
    r = BASE['snapshot']()
    r.update(delta_permille=100,batch_n=2,input_staging_policy='deferred',deadline_slack_ns=10**9)
    r['actual_post_batch_validation'].update(Kvar_bytes=r['Kvar_bytes'],total_work=r['compute_work_units'],
        cL_ns=r['cL_ns'],cR_ns=r['cR_ns'],state_transfer_s=0)
    return r


def feature(r=None, ns=1000001000, time=None):
    r = snapshot() if r is None else r
    c = API['COINIT']
    base = c['timing_feature'](r,time or BASE['same_time']())
    net = API['NET']['network_feature'](r,base,ns)
    return API['criticalpath_feature'](r,net)


class MeanDependencyTests(unittest.TestCase):
    def test_total_matches_unchanged_v6_formula(self):
        f=API['mean_recovery'](snapshot())
        self.assertAlmostEqual(f['R_state_bar_s'],200*.1/(2*2000)+200e-9/2+400*.1/(2*100))
        self.assertEqual(f['R_state_bar_s'],f['dependency_ready_bar_s']+f['mean_redo_s'])

    def test_serial_redo_never_masks_input(self):
        self.assertAlmostEqual(API['dependency_gain'](.005,0,.001),.004)
        # The rejected max(INPUT, dependency+redo) model would return zero here.
        self.assertEqual(max(.005,.101)-max(0,.101),0)

    def test_fully_masked(self):
        for dep in (.005,.1):
            self.assertEqual(API['dependency_gain'](.005,0,dep),0)

    def test_partial_input(self):
        self.assertAlmostEqual(API['dependency_gain'](.005,.003,.001),.002)
        self.assertAlmostEqual(API['dependency_gain'](.005,.0005,.001),.004)

    def test_no_lead(self):
        self.assertEqual(API['dependency_gain'](.005,.005,.001),0)

    def test_bounds(self):
        for i in (0,.001,1):
            for fraction in (0,.1,.9,1):
                for dep in (0,.01,10):
                    g=API['dependency_gain'](i,i*fraction,dep)
                    self.assertTrue(0 <= g <= i)

    def test_invalid_times(self):
        for args in ((1,2,0),(1,0,-1),(float('nan'),0,0)):
            with self.assertRaises(ValueError): API['dependency_gain'](*args)

    def test_invalid_committed_cadence(self):
        for d,n in ((0,2),(101,2),(100,11),(10,0),(10.0,2)):
            r=snapshot(); r.update(delta_permille=d,batch_n=n)
            with self.assertRaises(ValueError): API['mean_recovery'](r)

    def test_actual_not_reference_resources_or_configuration(self):
        r=snapshot(); expected=feature(r)
        r.update(frequency_reference_pair={'local':99,'remote':98},
                 frequency_proposal_estimates={'average_recovery_s':999,'initialization_s':999},
                 frequency_proposal_inputs={'backup_bandwidth_bytes_per_s':1,'batch_n':99})
        self.assertEqual(feature(r),expected)
        r['actual_post_batch_validation']['backup_bandwidth_bytes_per_s'] *= 2
        self.assertLess(feature(r)[0]['dependency_ready_bar_s'],expected[0]['dependency_ready_bar_s'])

    def test_actual_resource_disagreement_rejected(self):
        r=snapshot(); r['actual_post_batch_validation']['total_work']=1
        with self.assertRaises(ValueError): feature(r)

    def test_n_one_keeps_redo_outside_dependency_join(self):
        r=snapshot(); r['batch_n']=1
        f,_=feature(r)
        self.assertEqual(f['dependency_ready_bar_s'],0)
        self.assertGreater(f['mean_redo_s'],0)
        self.assertAlmostEqual(f['G_cp_s'],f['G_I_net_pot_s'])

    def test_committed_cadence_changes_mean_without_solver(self):
        r=snapshot(); r['delta_permille']=50
        self.assertLess(feature(r)[0]['mean_redo_s'],feature()[0]['mean_redo_s'])

    def test_label_profile_deadline_and_future_events_do_not_change_score(self):
        r=snapshot(); original=feature(r)[0]
        r.update(profile='arbitrary',label='NEEDED',fault_time_ns=1,final_recovery_node=99,
                 actual_checkpoint_ready_ns=1,deadline_slack_ns=1)
        changed=feature(r)[0]
        for key in ('G_cp_s','dependency_ready_bar_s','R_state_bar_s','estimated_init_ready_time_ns'):
            self.assertEqual(original[key],changed[key])

    def test_exact_values_stay_unknown(self):
        f,_=feature()
        for key in ('U_exact','G_I_exact_s','P_Iimpact','P_Iddl','checkpoint_state_tail_forecast'):
            self.assertIsNone(f[key])

    def test_local_delivery_is_not_network_benefit(self):
        r=snapshot(); r['remote_node']=r['source_node']
        r['input_path'].update(remote_node=r['source_node'],local_delivery=True,hops=[],admitted_rate_bps=None)
        f,steps=feature(r,ns=1)
        self.assertEqual(f['criticalpath_status'],'NOT_APPLICABLE_LOCAL')
        self.assertEqual(f['network_bytes'],0)
        self.assertIsNone(f['G_cp_s']); self.assertFalse(steps)

    def test_unknown_native_path_remains_unknown(self):
        f,_=feature(ns=None)
        self.assertIsNone(f['G_cp_s'])

    def test_unknown_initialization_schedule_remains_unknown(self):
        t=API['COINIT']['initializing_time'](10**9,2*10**9,2*10**9,2*10**9)
        f,_=feature(time=t)
        self.assertIsNone(f['G_cp_s']); self.assertIsNone(f['estimated_init_ready_time_ns'])


class FirstFailureTests(unittest.TestCase):
    def potential(self, times=(1,2,3), masses=(.2,.16,.128), init=0, ready=0, dep=.1, input_s=2):
        steps=[dict(time_ns=int(t*1e9),first_failure_mass=w) for t,w in zip(times,masses)]
        return API['criticalpath_potential'](steps,sum(masses),input_s,int(init*1e9),int(ready*1e9),dep)

    def test_before_exactly_at_and_after_input_ready(self):
        f,s=self.potential()
        self.assertAlmostEqual(s[0]['delta_dependency_wait_s'],1)
        self.assertAlmostEqual(s[1]['delta_dependency_wait_s'],1.9)
        self.assertEqual(s[1]['delta_dependency_wait_s'],s[2]['delta_dependency_wait_s'])
        self.assertAlmostEqual(f['G_cp_s'],.2+(.16+.128)*1.9)

    def test_all_masked_mass_yields_zero(self):
        f,_=self.potential(dep=10)
        self.assertEqual(f['G_cp_s'],0)

    def test_early_mass_unknown_not_renormalized(self):
        f,s=self.potential(ready=1.1)
        self.assertIsNone(f['G_cp_s'])
        self.assertAlmostEqual(f['unknown_first_failure_mass'],.2)
        self.assertAlmostEqual(f['known_first_failure_mass'],.288)
        self.assertAlmostEqual(f['G_cp_known_contribution_s'],.288*1.9)
        self.assertIsNone(s[0]['weighted_gain_s'])
        self.assertAlmostEqual(f['G_cp_assumption_upper_s'],.288*1.9+.2)

    def test_init_ready_same_ns_does_not_assert_valid_receipt(self):
        f,_=self.potential(ready=1)
        self.assertEqual(f['criticalpath_status'],API['EARLY'])

    def test_pre_init_lead_clamped_no_automatic_score_imputation(self):
        f,s=self.potential(init=2,ready=2)
        self.assertEqual([r['lead_s'] for r in s],[0,0,1])
        self.assertIsNone(f['G_cp_s'])
        self.assertEqual(f['G_cp_assumption_lower_s'],f['G_cp_assumption_upper_s'])

    def test_zero_early_mass_does_not_invalidate_known_score(self):
        f,_=self.potential(masses=(0,.2,.16),ready=1)
        self.assertEqual(f['criticalpath_status'],API['KNOWN'])
        self.assertAlmostEqual(f['G_cp_s'],.36*1.9)

    def test_all_probability_is_early(self):
        f,_=self.potential(ready=4)
        self.assertEqual(f['known_first_failure_mass'],0)
        self.assertEqual(f['G_cp_known_contribution_s'],0)
        self.assertIsNone(f['G_cp_s'])

    def test_no_dependency_reduces_to_existing_potential(self):
        f,_=self.potential(dep=0)
        self.assertAlmostEqual(f['G_cp_s'],.2+(.16+.128)*2)

    def test_incomplete_mass_rejected(self):
        with self.assertRaises(ValueError):
            API['criticalpath_potential']([dict(time_ns=1,first_failure_mass=.2)],.3,1,0,0,0)

    def test_finish_exclusive_and_first_sample_preserved(self):
        f,s=feature()
        self.assertTrue(f['finish_exclusive'])
        self.assertEqual(f['first_sample_semantics'],'NEXT_CANONICAL')
        self.assertEqual([p['time_ns'] for p in s],[2*10**9,3*10**9])


def population():
    f,_=feature()
    return [dict(f | r,G_I_net_pot_s=r['P_F'],G_cp_s=r['P_F']*.5) for r in BASE['population']()]


class CriticalpathTablesTests(unittest.TestCase):
    def test_two_views_local_and_no_production_rule(self):
        rows=population()
        local=dict(rows[0],task_id='5',network_bytes=0,G_cp_s=None,
                   criticalpath_status='NOT_APPLICABLE_LOCAL')
        tables,s=API['assemble'](rows+[local])
        self.assertEqual(s['views']['ALL_FAULT']['network_candidates'],4)
        self.assertEqual(s['views']['ALL_FAULT']['local_delivery_count'],1)
        self.assertEqual(s['views']['F1_F2_PREDICTABLE']['network_candidates'],3)
        self.assertEqual(s['f3_out_of_model'][0]['task_id'],'2')
        self.assertEqual(len(tables['criticalpath-wait-landmarks']),50)
        for key in ('selected_production_threshold','selected_production_score'):
            self.assertIsNone(s[key])
        self.assertFalse(s['binary_decision_ready']); self.assertFalse(s['selective_input_enabled'])
        self.assertEqual(s['A1'],'INCOMPLETE')

    def test_ties_kept_whole_including_zero_scores(self):
        rows=population()
        for r in rows: r['G_cp_s']=0
        tables,_=API['assemble'](rows)
        cp=[p for p in tables['criticalpath-score-sweeps'] if p['score']=='G_cp_s' and p['view']=='ALL_FAULT']
        self.assertEqual([p['selected_task_count'] for p in cp],[0,4])

    def test_unknown_is_disclosed_not_imputed_or_dropped_from_full_population(self):
        rows=population()
        rows[2].update(G_cp_s=None,criticalpath_status=API['EARLY'],unknown_first_failure_mass=.01)
        tables,s=API['assemble'](rows)
        view=s['views']['ALL_FAULT']
        self.assertEqual(view['network_candidates'],4); self.assertEqual(view['comparable_candidates'],3)
        self.assertEqual(view['unknown_task_ids'],['3'])
        self.assertEqual(view['unknown_labels'],{'NO_FAULT':1})
        self.assertEqual(view['full_gcp_sweep_status'],'UNAVAILABLE_UNKNOWN_CONTRIBUTIONS')
        cuts=tables['criticalpath-score-sweeps']
        common=[r for r in cuts if r['population_scope']=='NETWORK_COMPLETE_GCP' and r['view']=='ALL_FAULT']
        self.assertEqual({r['candidate_count'] for r in common},{3})
        self.assertEqual({r['score'] for r in common},set(API['SCORES']))
        full=[r for r in cuts if r['population_scope']=='FULL_NETWORK_REFERENCE']
        self.assertNotIn('G_cp_s',{r['score'] for r in full})

    def test_unknown_needed_wait_keeps_full_denominator_visible(self):
        rows=population(); rows[0].update(G_cp_s=None,criticalpath_status=API['EARLY'])
        tables,s=API['assemble'](rows)
        self.assertAlmostEqual(s['views']['ALL_FAULT']['common_observed_wait_fraction'],15/35)
        marks=[r for r in tables['criticalpath-wait-landmarks'] if r['population_scope']=='NETWORK_COMPLETE_GCP'
               and r['view']=='ALL_FAULT' and r['target_wait_coverage']==1]
        self.assertTrue(all(r['captured_wait_recall']==1 for r in marks))
        self.assertTrue(all(r['captured_full_network_wait_recall']==15/35 for r in marks))

    def test_known_score_missing_is_rejected(self):
        rows=population(); rows[0]['G_cp_s']=None
        with self.assertRaises(ValueError): API['assemble'](rows)

    def test_undefined_common_wait_keeps_full_references(self):
        rows=population()
        for r in rows: r.update(G_cp_s=None,criticalpath_status=API['EARLY'])
        tables,s=API['assemble'](rows)
        self.assertTrue(tables['criticalpath-reference-points'])
        self.assertEqual(s['views']['ALL_FAULT']['common_sweep_status'],'UNDEFINED_NO_KNOWN_NEEDED_WAIT')

    def test_reject_mixed_cohort(self):
        rows=population(); rows[0]['cohort']='OLD_JIT'
        with self.assertRaises(ValueError): API['assemble'](rows)

    def test_current_source_has_no_solver_or_simulator_call(self):
        text=(TESTS/'support/protection/input_criticalpath_audit.py').read_text()
        for forbidden in ('Simulator::','StartTransfer(','.Evaluate(', 'Schedule(', 'GetValue(', 'ns3 run'):
            self.assertNotIn(forbidden,text)
        self.assertIn("NET['native_estimates']",text)


if __name__ == '__main__':
    unittest.main()
