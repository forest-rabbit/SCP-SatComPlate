"""Production SER formula and self-contained causal snapshot anchor."""
import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[4]
BINARY = ROOT / 'build/contrib/satcompute/tests/ns3.48-satcompute-input-admission-test-default'


def snapshot(q=.5, at=20, prop=10):
    return dict(task_id=1, start_time_ns=0, remaining_compute_ns=100, input_bytes=10,
        predictor=dict(first_sample_time_ns=0, finish_exclusive=True, P_F=q,
            future_steps=[dict(time_ns=at, q_f1=q, q_f2=0, q_comp=q)]),
        input_path=dict(admissible=True, local_delivery=False,
                        admitted_rate_bps=8_000_000_000, propagation_ns=prop))


class InputAdmissionPolicyTests(unittest.TestCase):
    def evaluate(self, rows, success=True):
        self.assertTrue(BINARY.exists(), 'build satcompute-input-admission-test first')
        result = subprocess.run([str(BINARY)], input=json.dumps(rows), text=True, capture_output=True)
        self.assertEqual(result.returncode == 0, success, result.stderr)
        return json.loads(result.stdout) if success else None

    def test_strict_tie_and_endpoints(self):
        rows = [snapshot(q, at) for q, at in [(0,20),(1,0),(1,20),(.5,20),(.5,0)]]
        results = self.evaluate(rows)
        self.assertEqual([r['ser'] for r in results], [False,False,True,False,False])

    def test_partial_lead_and_propagation(self):
        rows = [snapshot(q, at, prop) for q in (0,.01,.25,.5,.75,1)
                for at in (0,1,5,10,15,20) for prop in (0,10)]
        for src, out in zip(rows, self.evaluate(rows)):
            q = src['predictor']['P_F']
            gain = q * min(10, src['predictor']['future_steps'][0]['time_ns'])
            self.assertEqual(out['ser'], gain > (1-q)*10)

    def test_integer_ceil_and_overflow(self):
        r = snapshot(.7, 2, 0); r['input_bytes'] = 3
        r['input_path']['admitted_rate_bps'] = 16_000_000_000
        self.assertEqual(self.evaluate([r])[0]['serialization_ns'],2)
        r['input_bytes'] = 2**64-1; r['input_path']['admitted_rate_bps'] = 1
        self.assertFalse(self.evaluate([r])[0]['ser'])

    def test_window_validation(self):
        for change in ('horizon','finish','mass','order'):
            r = snapshot()
            if change == 'horizon': r['start_time_ns'] = 2**63-2
            if change == 'finish': r['remaining_compute_ns'] = 20
            if change == 'mass': r['predictor']['P_F'] = .3
            if change == 'order': r['predictor']['future_steps'] *= 2
            with self.subTest(change=change): self.evaluate([r], False)

    def test_local_delivery_and_no_path(self):
        r = snapshot(); r['input_path']['local_delivery'] = True
        out = self.evaluate([r])[0]
        self.assertTrue(out['ser']); self.assertEqual(out['serialization_ns'],0)
        r['input_path']['local_delivery'] = False; r['input_path']['admissible'] = False
        self.assertFalse(self.evaluate([r])[0]['ser'])

    def test_fixed_stage_b_selector_anchor(self):
        trace = ROOT / 'contrib/satcompute/tests/fixtures/protection/selective-input-ser-anchor.json'
        inputs = json.loads(trace.read_text())['candidates']
        results = {r['task_id']:r for r in self.evaluate(inputs)}
        self.assertEqual(len(results), 409)
        network = local = selected = 0
        for row in inputs:
            actual = results[row['task_id']]['ser']
            self.assertEqual(actual, row['expected_send'], row['task_id'])
            if row['input_path']['local_delivery']:
                local += 1
                self.assertTrue(actual)
            else:
                network += 1
                selected += actual
        self.assertEqual((network, selected, local), (405,68,4))


if __name__ == '__main__': unittest.main()
