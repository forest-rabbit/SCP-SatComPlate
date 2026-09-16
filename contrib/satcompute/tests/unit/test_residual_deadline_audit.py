"""Residual classification, causal timing and single-run identity; never tune production."""
import json
from pathlib import Path
import runpy
import shlex
import tempfile
import unittest

TESTS=Path(__file__).resolve().parents[1]
A=runpy.run_path(str(TESTS/'support/protection/residual_deadline_audit.py'))
R=runpy.run_path(str(TESTS/'integration/regression/run-residual-deadline-development.py'))


class ResidualDeadlineTests(unittest.TestCase):
    def test_classes_and_boundaries(self):
        for costs,expected in [((2,.1,1),'A'),((.1,2,1),'B'),((.6,.6,1),'C'),
                               ((2,2,1),'D'),((1,0,1),'F'),((1,None,1),'E'),((0,.1,-1),'D')]:
            self.assertEqual(A['classify'](*costs),expected)
        self.assertEqual(A['minimum_prefetch'](2,.1,1),(1.1,.55))
        self.assertEqual(A['minimum_prefetch'](0,2,1),(None,None))
        with self.assertRaises(ValueError):A['classify'](-1,.1,1)

    def record(self):
        return dict(time_ns=0,selective_p_fail=.75,
            selective_steps=[dict(time_ns=10**9,q_comp=.5),dict(time_ns=2*10**9,q_comp=.5)],
            selective_first_sample_ns=10**9,selective_finish_exclusive=True,remaining_ns=3*10**9,
            initialization_seconds=.2,selective_reason='POSITIVE_BREAK_EVEN',
            selective_pure_send=True,serialization_ns=2*10**9,network_ready_ns=2001000000,
            observed_fault_hit=False)

    def test_first_risk_insufficient_does_not_erase_later_opportunity(self):
        r=A['timing'](self.record(),2,.1,1)
        self.assertFalse(r['firstRiskMeetsPrefetchLowerBound'])
        self.assertTrue(r['laterRiskMeetsBothModelBounds'])
        self.assertEqual(r['firstRiskMeetingBothModelBoundsNs'],2*10**9)
        self.assertEqual(r['probabilityMassMeetingBothModelBounds'],.25)
        self.assertEqual(r['actualInputProgressAtFault'],'UNKNOWN_NOT_SENT')

    def test_initialization_and_same_batch_veto_separate_from_pure_ser(self):
        row=self.record();row['initialization_seconds']=2.1
        self.assertFalse(A['timing'](row,2,.1,1)['actionableModelOpportunity'])
        row=self.record();row['observed_fault_hit']=True
        value=A['timing'](row,2,.1,1)
        self.assertFalse(value['actionableModelOpportunity'])
        self.assertEqual(value['selectiveWouldSendAtThisTime'],'SEND')

    def test_missing_prediction_is_unknown_not_defer(self):
        row=self.record();row.update(selective_p_fail=None,selective_steps=[],selective_reason='PREDICTION_UNAVAILABLE')
        self.assertEqual(A['timing'](row,2,.1,1)['selectiveWouldSendAtThisTime'],'UNKNOWN')

    def test_finish_inclusive_is_not_rewritten_as_exclusive(self):
        step=[dict(time_ns=10,q_comp=.5)]
        self.assertEqual(A['masses'](step,0,10,10,False,.5),[(10,.5)])
        with self.assertRaises(ValueError):A['masses'](step,0,10,10,True,.5)
        with self.assertRaises(ValueError):A['masses'](step,0,11,12,True,.5)
        with self.assertRaises(ValueError):A['masses'](step,0,10,12,True,.6)

    def test_zero_risk_sample_not_first_relevant_sample(self):
        row=self.record();row['selective_steps'][0]['q_comp']=0;row['selective_p_fail']=.5
        self.assertEqual(A['timing'](row,2,.1,1)['firstRelevantRiskSampleNs'],2*10**9)

    def candidate(self):
        r=self.record()
        r.update(task_id=5,trigger='TASK_RUNNING',fixed_local=2,remote=3,candidate_index=1,candidate_count=1,
            source=3,node_available=True,path_available=True,input_bytes=100,
            input_path=dict(local=True,admissible=True,rate_bps=0),input_bandwidth_bytes_per_s=1e300,
            observed_committed=False,original_reason='DEADLINE_INFEASIBLE',fault_input_seconds=0,
            recovery_no_full_input_min_seconds=.005,delta_permille=10,batch_n=1,
            variable_bytes=1000,backup_bandwidth_bytes_per_s=125000000,cR_ns=500000,work=100000,
            recovery_rate=100000,best_local_additional_bytes=10,best_remote_additional_bytes=20,
            local_free_bytes=100,remote_free_bytes=100,progress=0,deadline_ns=1001000000,slack_seconds=.001,
            frequency_finish_exclusive=True,frequency_first_sample_ns=10**9,frequency_steps=r['selective_steps'],
            frequency_p_fail=.75)
        c=dict(task_id='5',time_ns='0',decision_trigger='TASK_RUNNING',remote_candidates_checked='1',
            remote_candidates_total='1',all_candidates_infeasible='1',reference_local='2',actual_fault_hit='0')
        return r,c

    def test_localdelivery_is_zero_and_missing_candidates_fail_coverage(self):
        r,c=self.candidate()
        self.assertEqual(A['validate']([r],[c])['candidate_snapshots'],1)
        c['remote_candidates_checked']='2';c['remote_candidates_total']='2'
        with self.assertRaises(ValueError):A['validate']([r],[c])

    def test_candidate_path_not_global_bandwidth_or_other_target(self):
        r,c=self.candidate();r['source']=4;r['input_path']=dict(local=False,admissible=True,rate_bps=800)
        r['input_bandwidth_bytes_per_s']=100;r['fault_input_seconds']=1
        A['validate']([r],[c])
        r['input_bandwidth_bytes_per_s']=125000000
        with self.assertRaises(ValueError):A['validate']([r],[c])

    def test_only_passive_flag_and_output_paths_may_change(self):
        argv=['satcompute','--faultMode=generate','--islBandwidthBps=1000000000',
            '--randomSeed=1','--randomRun=11','--simulationDuration=1300','--outputDir=old','--faultTrace=old.json']
        command=lambda x:['ns3','run','--no-build',shlex.join(x)]
        new=argv+['--compfrrResidualDeadlineTasks='+','.join(map(str,R['TASKS']))]
        R['verify_identity'](command(new),command(argv))
        for before,after in [('--randomRun=11','--randomRun=12'),('--faultMode=generate','--faultMode=validation-replay')]:
            with self.assertRaises(ValueError):
                R['verify_identity'](command([after if s==before else s for s in new]),command(argv))

    def test_exact_comparison_detects_same_size_content_changes(self):
        with tempfile.TemporaryDirectory() as temp:
            a,b=Path(temp)/'a',Path(temp)/'b';a.mkdir();b.mkdir()
            (a/'x.csv').write_text('1');(b/'x.csv').write_text('1')
            self.assertEqual(R['compare_files'](a,b)['identical_files'],1)
            (b/'x.csv').write_text('2')
            with self.assertRaises(ValueError):R['compare_files'](a,b)

    def test_dirty_stage2b_cannot_be_formal_or_lack_source_patch(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)
            (path/'execution.json').write_text(json.dumps(dict(protection_mode='compfrr',worktree_dirty=True,
                stage='residual-deadline-development',development_only=True)))
            (path/'execution-result.json').write_text(json.dumps(dict(returncode=0,status='FINISHED')))
            (path/'run-summary.json').write_text('{}')
            for allow in (False,True):
                with self.assertRaisesRegex(ValueError,'dirty execution'):
                    R['BASE']['audit'](path,allow_development=allow)

    def test_run_summary_only_ignores_host_wall_clock(self):
        with tempfile.TemporaryDirectory() as temp:
            a,b=Path(temp)/'a',Path(temp)/'b';a.mkdir();b.mkdir()
            left=dict(wall_clock_ns=1,wall_clock_s=1e-9,completed=785)
            right=dict(left,wall_clock_ns=2,wall_clock_s=2e-9)
            (a/'run-summary.json').write_text(json.dumps(left))
            (b/'run-summary.json').write_text(json.dumps(right))
            R['compare_run_summary'](a,b)
            right['completed']=786
            (b/'run-summary.json').write_text(json.dumps(right))
            with self.assertRaises(ValueError):R['compare_run_summary'](a,b)


if __name__=='__main__':unittest.main()
