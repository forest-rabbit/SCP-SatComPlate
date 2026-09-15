"""Stage 2A audit counts and frozen single-development-run identity."""
from pathlib import Path
import copy
import json
import runpy
import shlex
import tempfile
import unittest

TESTS=Path(__file__).resolve().parents[1]
A=runpy.run_path(str(TESTS/'support/protection/candidate_coverage_audit.py'))
R=runpy.run_path(str(TESTS/'integration/regression/run-candidate-coverage-development.py'))


class CandidateCoverageTests(unittest.TestCase):
    def evidence(self):
        f=dict(task_id='1',fault_epoch_time_ns='100',decision_trigger='TASK_RUNNING',phase_before='OFF',
            pair_candidates_total='20',pair_path_feasible='6',pair_hard_checked='2',pair_hard_feasible='1',
            local_node='2',remote_node='4',proposed_action='START',proposal_reason='START_BENEFICIAL',
            actual_fault_hit='0',decision_committed='1',proposed_delta_permille='10',proposed_n='1',reason='START_BENEFICIAL')
        c=dict(task_id='1',time_ns='100',decision_trigger='TASK_RUNNING',reference_local='2',reference_remote='0',
            reference_reject_reason='DEADLINE_INFEASIBLE',remote_candidates_total='3',remote_candidates_checked='2',
            first_feasible_anchor_index='2',feasible_anchor_remote='3',final_selected_remote='4',all_candidates_infeasible='0',
            proposed_action='START',proposal_reason='START_BENEFICIAL',actual_fault_hit='0',decision_committed='1',
            anchor_delta_permille='10',anchor_n='1',resolution_reason='START_BENEFICIAL')
        return c,f

    def test_anchor_and_ranked_remote_can_differ_without_config_change(self):
        c,f=self.evidence();r=A['validate']([c],[f])
        self.assertEqual(r['fallback_used_count'],1)
        self.assertEqual(r['fallback_depth']['p50'],1)
        self.assertEqual(r['reference_failed_alternate_feasible_tasks'],1)
        self.assertEqual(r['fallback_committed_start_tasks'],1)

    def test_no_retry_after_nonbeneficial_candidate_or_different_local(self):
        for changes in (dict(reference_reject_reason='OFF_NOT_MORE_EXPENSIVE'),dict(reference_local='5'),
                        dict(first_feasible_anchor_index='1'),dict(anchor_n='2'),dict(remote_candidates_total='1')):
            c,f=self.evidence();c.update(changes)
            with self.assertRaises((ValueError,RuntimeError,AssertionError)):
                A['validate']([c],[f])

    def test_exhaustion_is_not_an_anchor_or_zero_depth_success(self):
        c,f=self.evidence()
        c.update(remote_candidates_checked='3',first_feasible_anchor_index='',feasible_anchor_remote='',
                 final_selected_remote='',all_candidates_infeasible='1',proposed_action='NONE',
                 proposal_reason='DEADLINE_INFEASIBLE',decision_committed='0',anchor_delta_permille='',anchor_n='',
                 resolution_reason='DEADLINE_INFEASIBLE')
        f.update(pair_hard_checked='3',pair_hard_feasible='0',proposed_action='NONE',
                 proposal_reason='DEADLINE_INFEASIBLE',decision_committed='0',proposed_delta_permille='',proposed_n='',
                 reason='DEADLINE_INFEASIBLE')
        r=A['validate']([c],[f])
        self.assertEqual(r['all_remotes_infeasible_count'],1)
        self.assertEqual(r['fallback_depth']['max'],2)
        self.assertEqual(r['reference_failed_alternate_feasible_tasks'],0)
        c['remote_candidates_total']='4'
        with self.assertRaises((ValueError,RuntimeError,AssertionError)): A['validate']([c],[f])

    def test_same_task_repeated_events_not_repeated_tasks(self):
        c,f=self.evidence();c2,f2=copy.deepcopy(c),copy.deepcopy(f)
        c2.update(time_ns='200',actual_fault_hit='1',decision_committed='0',resolution_reason='CURRENT_FAULT_HIT')
        f2.update(fault_epoch_time_ns='200',actual_fault_hit='1',decision_committed='0',reason='CURRENT_FAULT_HIT')
        r=A['validate']([c,c2],[f,f2])
        self.assertEqual(r['fallback_used_count'],2)
        self.assertEqual(r['fallback_used_tasks'],1)
        self.assertEqual(r['fallback_anchor_fault_hit_count'],1)
        with self.assertRaises((ValueError,RuntimeError,AssertionError)): A['validate']([c,c],[f])

    def test_development_only_changes_output_paths(self):
        argv=R['RUN']['arguments'](Path('/tmp/dev'),'compfrr-p',isl_bandwidth_bps=10**9)
        baseline=R['RUN']['arguments'](Path('/tmp/old'),'compfrr-p',isl_bandwidth_bps=10**9)
        cmd=lambda x:['ns3','run','--no-build',shlex.join(x)]
        R['verify_identity'](cmd(argv),cmd(baseline))
        for before,after in (('--randomRun=11','--randomRun=12'),('--randomSeed=1','--randomSeed=2'),
            ('--compfrrInputPolicy=selective','--compfrrInputPolicy=eager'),
            ('--islBandwidthBps=1000000000','--islBandwidthBps=10000000000')):
            changed=[after if v==before else v for v in argv]
            with self.assertRaises((ValueError,RuntimeError,AssertionError)):
                R['verify_identity'](cmd(changed),cmd(baseline))

    def test_formal_auditor_does_not_silently_accept_dirty_development(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            (root/'execution.json').write_text(json.dumps(dict(protection_mode='compfrr',worktree_dirty=True,
                stage='candidate-coverage-development',development_only=True)))
            (root/'execution-result.json').write_text(json.dumps(dict(returncode=0,status='FINISHED')))
            (root/'run-summary.json').write_text('{}')
            for allow in (False,True):
                with self.assertRaisesRegex(ValueError,'dirty execution'):
                    R['BASE']['audit'](root,allow_development=allow)


if __name__=='__main__': unittest.main()
