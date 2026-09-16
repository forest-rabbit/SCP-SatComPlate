"""Frozen historical N semantics and residual task/candidate aggregation."""
import json
from pathlib import Path
import runpy
import unittest

ROOT=Path(__file__).resolve().parents[4]
A=runpy.run_path(str(ROOT/'contrib/satcompute/tests/support/protection/selective_sn_residual_audit.py'))


class ResidualSelectiveSnTests(unittest.TestCase):
    def sample(self,q=.4,lead=20,prop=10):
        return dict(start_ns=0,remaining_ns=101,first_sample_ns=0,finish_exclusive=True,input_bytes=10,
            path=dict(admissible=True,local=False,rate_bps=8_000_000_000,propagation_ns=prop),p_fail=q,
            steps=[dict(time_ns=lead,q_comp=q)])

    def test_historical_n_adds_network_ready_gain_but_keeps_ser_cost(self):
        value=A['evaluate'](**self.sample())
        self.assertFalse(value['sSend']);self.assertTrue(value['nSend'])
        self.assertEqual((value['serializationNs'],value['networkReadyNs'],value['costNs']),(10,20,6))
        self.assertEqual((value['serialGainNs'],value['networkGainNs']),(4,8))

    def test_strict_tie_defer_and_s_is_subset_of_n(self):
        tie=A['evaluate'](**self.sample(q=.5,lead=10,prop=0))
        self.assertFalse(tie['sSend']);self.assertFalse(tie['nSend'])
        for q in (0,.01,.25,.5,.75,1):
            for lead in (0,1,5,10,20,100):
                out=A['evaluate'](**self.sample(q=q,lead=lead))
                self.assertFalse(out['sSend'] and not out['nSend'])

    def test_native_integer_ceiling_and_local_delivery(self):
        self.assertEqual(A['transfer_times'](3,dict(admissible=True,local=False,
            rate_bps=16_000_000_000,propagation_ns=7)),(2,9))
        local=self.sample();local['path']['local']=True
        out=A['evaluate'](**local)
        self.assertTrue(out['sSend']);self.assertTrue(out['nSend'])
        self.assertEqual((out['serializationNs'],out['networkReadyNs']),(0,0))

    def test_historical_409_anchor_is_68_vs_115(self):
        result=A['validate_historical_n'](ROOT/'contrib/satcompute/tests/fixtures/protection/selective-input-ser-anchor.json')
        self.assertEqual((result['networkCandidates'],result['networkSSend'],result['networkNSend'],
                          result['localDelivery'],result['sOnly']),(405,68,115,4,0))
        self.assertEqual(result['sourceCommit'],'a4315e2b8')

    def test_task_new_requires_n_without_any_s_candidate(self):
        base=dict(taskId=5,decisionTimeNs=1,decisionTrigger='TASK_RUNNING',S_SEND=False,N_SEND=True,
            S_TO_N_DEFER_TO_SEND=True,zeroInputAdmissionFeasible=True,deadlineBecameFeasibleAfterZeroInput=True)
        base.update(serialGainNs=1,networkGainNs=2,costNs=1.5)
        tasks=A['summarize']([dict(base,taskId=t) for t in A['TASKS']])
        self.assertTrue(all(t['S_TO_N_NEW_TASK'] and t['newCandidatesZeroInputFeasible']==1 for t in tasks))
        rows=[dict(base,taskId=t) for t in A['TASKS']]
        rows.append(dict(base,taskId=5,S_SEND=True,N_SEND=True,S_TO_N_DEFER_TO_SEND=False))
        task5=A['summarize'](rows)[0]
        self.assertFalse(task5['S_TO_N_NEW_TASK'])
        self.assertEqual(task5['sToNNewCandidates'],1)


if __name__=='__main__':unittest.main()
