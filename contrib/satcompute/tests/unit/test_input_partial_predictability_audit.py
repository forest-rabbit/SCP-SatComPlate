"""No simulation: partial coverage, action-sensitive errors and conditional bounds."""
import itertools
from pathlib import Path
import runpy
import unittest

TESTS=Path(__file__).resolve().parents[1]
API=runpy.run_path(str(TESTS/'support/protection/input_partial_predictability_audit.py'))


def row(tid='1', **updates):
    return dict(dict(task_id=tid,cohort=API['COINIT']['COHORT'],profile='image',P_F=.5,
        label='NEEDED',fault_source='F1',critical_wait_ns=4,network_bytes=100,
        G_I_net_pot_s=5e-9,G_cp_known_contribution_s=2e-9,criticalpath_status=API['CP']['KNOWN'],
        A_hat_ns=6,T_I_net_ns=10,remote_node=2,initializing_start_time_ns=0,
        estimated_init_ready_time_ns=1,_steps=[dict(time_ns=10,first_failure_mass=.5)]),**updates)


def population():
    return [row(),row('2',P_F=.2,label='NO_FAULT',fault_source=None,critical_wait_ns=0),
            row('3',P_F=.4,critical_wait_ns=8,fault_source='F3')]


def recovery(**updates):
    return dict(dict(task_id='1',recovery_node='2',recovery_accept_time_ns='100',state_ready_time_ns='106',
        input_received_time_ns='110',recovery_compute_start_time_ns='110',fault_time_ns='10'),**updates)


class PartialCoverageTests(unittest.TestCase):
    def test_mass_is_sum_of_pf_not_probability_of_any_fault(self):
        f=API['coverage_metrics'](population(),population()[:1])
        self.assertAlmostEqual(f['total_first_failure_mass'],1.1)
        self.assertAlmostEqual(f['first_failure_mass_share'],.5/1.1)
        self.assertIsNone(f['true_G_share'])

    def test_uncovered_needed_wait_and_max(self):
        f=API['coverage_metrics'](population(),population()[:1])
        self.assertEqual(f['uncovered_needed_count'],1)
        self.assertEqual(f['uncovered_wait_ns'],8)
        self.assertEqual(f['uncovered_max_task_wait_ns'],8)

    def test_empty_and_full_coverage(self):
        for covered,expected in (([],0),(population(),1)):
            r=API['coverage_metrics'](population(),covered)
            self.assertEqual(r['task_coverage'],expected)
            self.assertEqual(r['observed_wait_share'],expected)

    def test_zero_wait_is_undefined_not_zero(self):
        r=row(label='NO_FAULT',critical_wait_ns=0)
        self.assertIsNone(API['coverage_metrics']([r],[r])['observed_wait_share'])

    def test_unknown_task_not_dropped_or_its_full_gain_imputed(self):
        r=row(criticalpath_status=API['CP']['EARLY'])
        f=API['coverage_metrics']([r],[r])
        self.assertEqual(f['covered_unknown_gcp_tasks'],1)
        self.assertIsNone(f['true_G_share'])

    def test_cardinality_bounds_match_exhaustive_subsets(self):
        pop=[row(str(i),P_F=i/10,critical_wait_ns=i) for i in range(1,11)]
        for r in API['cardinality_references'](pop,'ALL_FAULT'):
            if r['metric']!='observed_wait_ns':continue
            m=r['covered_tasks'];sets=list(itertools.combinations(pop,m))
            covered=[sum(x['critical_wait_ns'] for x in s)/55 for s in sets]
            self.assertAlmostEqual(r['coverage_share_min'],min(covered))
            self.assertAlmostEqual(r['coverage_share_max'],max(covered))
            self.assertAlmostEqual(r['uniform_subset_expected_share'],sum(covered)/len(covered))
            unc=[max((x['critical_wait_ns'] for x in pop if x not in s),default=0) for s in sets]
            self.assertAlmostEqual(r['uniform_expected_uncovered_max_task_wait_ns'],sum(unc)/len(unc))


class IntervalTests(unittest.TestCase):
    def test_strict_boundaries_equal_is_uncertain(self):
        for i,expected in ((4,'DEFER_CANDIDATE'),(5,'UNCERTAIN'),(7,'UNCERTAIN'),(8,'SEND_CANDIDATE')):
            self.assertEqual(API['interval_class'](i,5,7),expected)

    def test_invalid_interval_rejected(self):
        with self.assertRaises(ValueError):API['interval_class'](5,7,6)

    def test_unknown_early_branch_never_confident(self):
        self.assertEqual(API['interval_class'](10,0,1,False),'UNCERTAIN')

    def test_lower_clamped_to_nonnegative(self):
        self.assertEqual(API['interval'](row(),10),(0,16))

    def test_monotone_gain_bounds(self):
        previous=None
        for e in (0,1,3,6,100):
            lo,hi=API['conditional_gain_bounds'](row(),e)
            self.assertTrue(0 <= lo <= hi <= 5e-9)
            if previous:
                self.assertLessEqual(lo,previous[0]);self.assertGreaterEqual(hi,previous[1])
            previous=(lo,hi)

    def test_no_early_zero_fill(self):
        lo,hi=API['conditional_gain_bounds'](row(estimated_init_ready_time_ns=10),0)
        self.assertEqual(lo,0);self.assertEqual(hi,5e-9)

    def test_same_interval_bounds_cover_all_possible_barriers(self):
        r=row();lo,hi=API['conditional_gain_bounds'](r,2)
        for a in range(4,9):
            g=.5*max(0,10-a)/1e9
            self.assertLessEqual(lo,g);self.assertGreaterEqual(hi,g)

    def test_coverage_ratio_bounds_use_uncovered_denominator(self):
        pop=[row(),row('2',A_hat_ns=2)]
        f=API['share_bounds'](pop,pop[:1],1)
        values=[]
        for a,b in itertools.product(range(5,8),range(1,4)):
            x=.5*(10-a);y=.5*(10-b);values.append(x/(x+y))
        self.assertAlmostEqual(f['assumed_G_coverage_share_lower'],min(values))
        self.assertAlmostEqual(f['assumed_G_coverage_share_upper'],max(values))

    def test_outcomes_do_not_change_confidence_classes(self):
        pop=population();_,before=API['interval_point'](pop,[],'ALL_FAULT',1)
        changed=[dict(r,label='NO_FAULT',critical_wait_ns=0) for r in pop]
        _,after=API['interval_point'](changed,[],'ALL_FAULT',1)
        self.assertEqual(before,after)

    def test_confident_defer_wait_not_counted_as_send_benefit(self):
        pop=[row(A_hat_ns=20)]
        r,_=API['interval_point'](pop,[],'ALL_FAULT',1)
        self.assertEqual(r['observed_wait_share'],1)
        self.assertEqual(r['confident_send_wait_share'],0)
        self.assertEqual(r['confident_defer_needed_count'],1)


class BarrierErrorTests(unittest.TestCase):
    def observe(self,r=None,rec=None):
        return API['realized_barriers']([r or row()],[rec or recovery()])[0]

    def test_observation_reproduces_dependency_join_not_redo(self):
        r=self.observe()
        self.assertEqual((r['A_observed_ns'],r['T_I_observed_ns']),(6,10))
        self.assertTrue(r['canonical_step_observed'])
        self.assertFalse(r['observed_I_barrier_flip'])

    def test_overestimate_can_flip_to_false_defer(self):
        r=self.observe(row(A_hat_ns=20))
        self.assertEqual(r['A_error_direction'],'OVER');self.assertTrue(r['observed_I_barrier_flip'])
        s=API['error_summary']([r],'ALL_FAULT')
        p=next(x for x in s if x['basis']=='observed_I' and x['error_direction']=='OVER')
        self.assertEqual(p['false_defer'],1);self.assertIsNone(p['actual_policy_action_flip_fraction'])

    def test_underestimate_can_flip_to_false_send(self):
        r=self.observe(row(critical_wait_ns=0),recovery(state_ready_time_ns='120',recovery_compute_start_time_ns='120'))
        self.assertEqual(r['A_error_direction'],'UNDER');self.assertTrue(r['observed_I_barrier_flip'])

    def test_large_error_without_action_flip(self):
        r=self.observe(row(A_hat_ns=1000,critical_wait_ns=0),recovery(state_ready_time_ns='120',recovery_compute_start_time_ns='120'))
        self.assertEqual(r['A_error_ns'],980);self.assertFalse(r['observed_I_barrier_flip'])

    def test_input_error_is_not_attributed_to_barrier_error(self):
        r=self.observe(row(A_hat_ns=8,T_I_net_ns=7))
        self.assertTrue(r['snapshot_I_barrier_flip']);self.assertFalse(r['observed_I_barrier_flip'])

    def test_no_fault_is_unknown_not_zero_barrier(self):
        r=API['realized_barriers']([row()],[])[0]
        self.assertEqual(r['status'],'NO_REALIZED_FAULT');self.assertIsNone(r['A_observed_ns'])

    def test_migrated_node_not_comparable_with_original_pair(self):
        r=self.observe(rec=recovery(recovery_node='3'))
        self.assertEqual(r['status'],'DIFFERENT_RECOVERY_TARGET');self.assertIsNone(r['A_error_ns'])

    def test_missing_timeline_does_not_impute(self):
        r=self.observe(rec=recovery(state_ready_time_ns=''))
        self.assertEqual(r['status'],'UNKNOWN_OBSERVED_TIMELINE')

    def test_unmodeled_admission_wait_rejected(self):
        with self.assertRaises(ValueError):self.observe(rec=recovery(recovery_compute_start_time_ns='111'))

    def test_f3_excluded_only_from_predictable_view(self):
        r=self.observe(row(fault_source='F3'))
        self.assertEqual(API['error_summary']([r],'F1_F2_PREDICTABLE')[0]['observed_count'],0)


class AssemblyTests(unittest.TestCase):
    def test_full_population_f3_views_no_threshold_or_policy(self):
        tables,s=API['assemble'](population(),[recovery()])
        self.assertEqual(s['views']['ALL_FAULT']['candidate_count'],3)
        self.assertEqual(s['views']['F1_F2_PREDICTABLE']['candidate_count'],2)
        self.assertEqual(s['views']['F1_F2_PREDICTABLE']['no_fault_count'],1)
        self.assertEqual(len(tables['coverage-landmarks']),8)
        for key in ('production_rule','selected_threshold','selected_error_radius','actual_policy_action_flip_fraction'):
            self.assertIsNone(s[key])

    def test_ties_not_split_for_requested_task_coverage(self):
        tables,_=API['assemble'](population(),[recovery()])
        r=tables['coverage-landmarks'][0]
        self.assertEqual(r['target_task_coverage'],.7)
        self.assertEqual(r['task_coverage'],1)
        self.assertEqual(r['preceding_attainable_coverage'],0)

    def test_scope_mixed_or_local_rejected(self):
        for r in (row(cohort='OLD_RUN'),row(network_bytes=0)):
            with self.assertRaises(ValueError):API['assemble']([r],[])

    def test_no_rng_solver_or_simulation(self):
        text=(TESTS/'support/protection/input_partial_predictability_audit.py').read_text()
        for forbidden in ('import random','Simulator::','StartTransfer(','.Evaluate(', 'Schedule(', 'ns3 run'):
            self.assertNotIn(forbidden,text)

    def test_confidence_set_can_remain_correct_despite_bound_violation(self):
        pop=[row(A_hat_ns=0)]
        observations=API['realized_barriers'](pop,[recovery()])
        r,_=API['interval_point'](pop,observations,'ALL_FAULT',1)
        self.assertEqual(r['observed_bound_violations'],1)
        self.assertEqual(r['observed_confident_barrier_errors'],0)


if __name__=='__main__':
    unittest.main()
