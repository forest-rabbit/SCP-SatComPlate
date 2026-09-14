"""Synthetic passive trace contracts; no formal outputs or future outcomes required."""
from pathlib import Path
import runpy
import tempfile
import unittest

TESTS = Path(__file__).resolve().parents[1]
MODULE = TESTS.parent
API = runpy.run_path(str(TESTS / 'support/protection/input_start_trace_audit.py'))
EQUIV = runpy.run_path(str(TESTS / 'integration/regression/run-protection-equivalence.py'))


def snapshot():
    steps = [dict(time_ns=k*10**9, q_f1=.1, q_f2=.2, q_comp=.28) for k in (2, 3)]
    return dict(task_id=1, admitted=True, snapshot_point='PRE_START_CHECKPOINT', checkpoint_ready=False,
        start_time_ns=10**9, remaining_compute_ns=3*10**9, progress_work=100, compute_work_units=400,
        start_trigger='FAULT_EPOCH', source_node=0, local_node=1, remote_node=2,
        profile='dense-image', input_bytes=1000, recovery_rate=100, primary_rate=100,
        actual_post_batch_validation=dict(recovery_rate=100, primary_rate=100),
        input_path=dict(source_node=0, remote_node=2, local_delivery=False, admissible=True,
                        reachable=True, admitted_rate_bps=8000, hops=[dict(source=0, destination=2)]),
        predictor=dict(source='QueryTaskPrediction', finish_exclusive=True, prediction_time_ns=10**9,
            remaining_compute_ns=3*10**9, first_sample_time_ns=2*10**9, check_interval_ns=10**9,
            first_sample_semantics='NEXT_CANONICAL', future_steps_count=2, future_steps=steps,
            P_F=1-.72**2))


class PassiveInputTraceTests(unittest.TestCase):
    def test_execution_identity_rejects_parameters_and_incomplete_sources(self):
        command = ['satcompute --simulationDuration=1300 --randomRun=11 --inputStagingPolicy=deferred']
        canonical = dict(commit=API['CANONICAL_COMMIT'], command=command)
        execution = dict(commit='current-clean-code', canonical_reference_commit=canonical['commit'],
            purpose='DEVELOPMENT_CALIBRATION', final_performance_result=False, input_start_audit=True,
            selective_input_enabled=False, worktree_dirty=False, command=[command[0]+' --inputStartAudit=1'])
        result, run = dict(returncode=0), dict(simulation_duration_ns=1300*10**9, task_count=800)
        self.assertEqual(API['execution_identity'](execution, result, run, canonical)['status'], 'PASS')
        for field, value in (('worktree_dirty', True), ('final_performance_result', True),
                             ('selective_input_enabled', True), ('canonical_reference_commit', 'wrong'),
                             ('command', [execution['command'][0].replace('--randomRun=11', '--randomRun=12')])):
            with self.subTest(field=field), self.assertRaises(ValueError):
                API['execution_identity'](dict(execution, **{field:value}), result, run, canonical)
        with self.assertRaises(ValueError):
            API['execution_identity'](execution, dict(returncode=1), run, canonical)
        with self.assertRaises(ValueError):
            API['execution_identity'](execution, result, dict(run, task_count=799), canonical)

    def test_full_union_trajectory_without_advanced_surrogate(self):
        f = API['feature_snapshot'](snapshot())
        self.assertAlmostEqual(f['P_F'], .4816)
        self.assertAlmostEqual(sum(s['first_failure_mass'] for s in f['future_first_failure_trajectory']), f['P_F'])
        self.assertEqual(f['input_serialization_s'], 1)
        for field in ('G_I_s', 'P_Iimpact', 'P_Iddl', 'checkpoint_state_tail_forecast'):
            self.assertIsNone(f[field])

    def test_future_labels_never_supply_features(self):
        r = snapshot()
        baseline = API['feature_snapshot'](r)
        r.update(fault_time_ns=1, future_checkpoint_bytes=0, final_outcome='FAILED', input_critical_wait_ns=100)
        self.assertEqual(baseline, API['feature_snapshot'](r))

    def test_no_invented_probability_on_missing_predictor(self):
        r = snapshot(); r['predictor'] = None
        self.assertIsNone(API['feature_snapshot'](r)['P_F'])

    def test_reject_inclusive_missing_step_bad_union_and_bad_product(self):
        for change in ('inclusive', 'missing', 'union', 'product', 'count'):
            r = snapshot(); p = r['predictor']
            if change == 'inclusive': p['finish_exclusive'] = False
            if change == 'missing': p['future_steps'].pop()
            if change == 'union': p['future_steps'][0]['q_f1'] = .9
            if change == 'product': p['P_F'] = .9
            if change == 'count': p['future_steps_count'] = 1
            with self.subTest(change=change), self.assertRaises(ValueError):
                API['feature_snapshot'](r)

    def test_pending_current_is_allowed_only_before_check(self):
        r = snapshot(); p = r['predictor']
        p['first_sample_time_ns'] = r['start_time_ns']; p['first_sample_semantics'] = 'PENDING_CURRENT'
        p['future_steps'].insert(0, dict(p['future_steps'][0], time_ns=r['start_time_ns']))
        p['future_steps_count'] = 3; p['P_F'] = 1-.72**3
        for trigger in ('TASK_RUNNING', 'CAPACITY_RELEASE'):
            r['start_trigger'] = trigger
            API['feature_snapshot'](r)
        r['start_trigger'] = 'FAULT_EPOCH'
        with self.assertRaises(ValueError): API['feature_snapshot'](r)

    def test_local_delivery_zero_network_and_unknown_unavailable_path(self):
        r = snapshot(); r['remote_node'] = r['source_node']
        r['input_path'].update(remote_node=r['source_node'], local_delivery=True, hops=[], admitted_rate_bps=None)
        f = API['feature_snapshot'](r)
        self.assertEqual((f['planned_network_input_bytes'], f['G_I_s']), (0, 0))
        r = snapshot(); r['input_path'].update(admissible=False, admitted_rate_bps=None, hops=[])
        f = API['feature_snapshot'](r)
        self.assertIsNone(f['input_serialization_s'])
        self.assertEqual(f['path_observation'], 'UNAVAILABLE')

    def test_actual_pair_and_rate_are_not_reference(self):
        r = snapshot(); r['frequency_reference_pair'] = dict(local=1, remote=9)
        self.assertEqual(API['feature_snapshot'](r)['remote_node'], 2)
        r['input_path']['remote_node'] = 9
        with self.assertRaises(ValueError): API['feature_snapshot'](r)
        r = snapshot(); r['actual_post_batch_validation']['recovery_rate'] = 101
        with self.assertRaises(ValueError): API['feature_snapshot'](r)

    def test_source_is_default_off_and_has_no_event_rng_or_admission_calls(self):
        capture = (MODULE / 'protection/policy/compfrr/input/input-start-audit.cc').read_text()
        for forbidden in ('Schedule(', 'CreateFlow', 'StartTransfer', 'Reserve(', 'GetValue(', 'Solve('):
            self.assertNotIn(forbidden, capture)
        self.assertIn('QueryTaskPrediction', capture)
        self.assertIn('PredictComputeFailureBeforeFinish', capture)
        self.assertIn('inputStartAudit = false', (MODULE / 'para.cc').read_text())
        controller = (MODULE / 'protection/policy/compfrr/compfrr-controller.cc').read_text()
        freeze = controller.index('CaptureInputStart(row, time)')
        execute = controller.index('m_manager.Execute(context', freeze)
        admit = controller.index('m_inputStartRecords.push_back', execute)
        self.assertLess(freeze, execute); self.assertLess(execute, admit)

    def test_on_off_comparison_exempts_only_the_new_audit_file(self):
        with tempfile.TemporaryDirectory() as d:
            left, right = Path(d)/'off', Path(d)/'on'
            left.mkdir(); right.mkdir()
            for p in (left, right): (p/'ledger.csv').write_text('bytes,wu\n1,2\n')
            (right/'input-start-snapshots.json').write_text('{}')
            result = EQUIV['compare'](left, right, allow_input_start_audit=True)
            self.assertEqual(result['additional_passive_audit_files'], 1)
            with self.assertRaises(AssertionError): EQUIV['compare'](left, right)
            (right/'ledger.csv').write_text('bytes,wu\n1,3\n')
            with self.assertRaises(AssertionError): EQUIV['compare'](left, right, allow_input_start_audit=True)


if __name__ == '__main__': unittest.main()
