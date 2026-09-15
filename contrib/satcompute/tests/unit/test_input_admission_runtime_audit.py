"""Accounting failures must be detected even when a logical task succeeds."""
from copy import deepcopy
import json
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[4]
API=runpy.run_path(str(Path(__file__).parents[1]/'support/protection/input_admission_runtime_audit.py'))


class RuntimeAuditTests(unittest.TestCase):
    def test_legacy_eager_missing_barrier_is_not_zero_wait(self):
        stats,missing=API['critical_wait_distribution']([dict(input_received_time_ns='10')])
        self.assertEqual(missing,1)
        self.assertEqual(stats['count'],0)
        self.assertIsNone(stats['mean'])
        stats,missing=API['critical_wait_distribution']([
            dict(input_received_time_ns='3000000',state_ready_time_ns='1000000'),
            dict(input_received_time_ns='1000000',state_ready_time_ns='3000000'),
            dict(input_received_time_ns='',state_ready_time_ns='3000000')])
        self.assertEqual((stats['count'],stats['sum'],missing),(2,2.0,1))

    def fixture(self):
        record=dict(task_id=1,target=4,source=0,input_bytes=100,requested_ns=10,flow_id=9,
            sent_bytes=100,used_bytes=100,unused_bytes=0,normal_sent_bytes=40,post_fault_sent_bytes=60,
            state='RELEASED',handed_off=True,refetch_reason='',used_ns=80,ready_ns=70,byte_category='USED')
        summary=dict(tasks=[record],B_prefetch_total=100,B_prefetch_used=100,B_prefetch_unused=0,
            used_bytes_at_end=0,reserved_bytes_at_end=0)
        datasets={
            'input-admission-decisions.csv':[dict(task_id='1',decision='SEND',remote='4',start_time_ns='10',profile='llm')],
            'transfer-summary.csv':[],
            'protection-transfers.csv':[dict(task_id='1',kind='PREFETCH_INPUT',transfer_id='9',sent_bytes='100',
                bytes='100',source_node='0',destination_node='4')],
            'recovery-summary.csv':[dict(task_id='1',input_delivery_mode='PREFETCH_IN_FLIGHT',
                recovery_compute_start_time_ns='80',input_received_time_ns='70',state_ready_time_ns='80')],
            'input-prefetch-events.csv':[dict(task_id='1',event='PREFETCH_USED')]}
        return summary,datasets

    def audit(self,summary,datasets):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'input-prefetch-summary.json').write_text(json.dumps(summary))
            (root/'input-start-snapshots.json').write_text(json.dumps(dict(candidates=[
                dict(task_id=1,remote_node=4,start_time_ns=10,admitted=True)])))
            with patch.dict(API['audit_prefetch'].__globals__,rows=lambda _,name,*args:datasets[name]):
                return API['audit_prefetch'](root)

    def test_complete_lifetime_includes_post_fault_bytes(self):
        summary,data=self.fixture(); self.assertEqual(self.audit(summary,data)['B_prefetch_used'],100)
        bad=deepcopy(summary); bad['tasks'][0].update(used_bytes=40,unused_bytes=60)
        with self.assertRaises(ValueError): self.audit(bad,data)

    def test_receiver_dependency_not_planned_wait(self):
        summary,data=self.fixture(); data['recovery-summary.csv'][0]['recovery_compute_start_time_ns']='60'
        with self.assertRaises(ValueError): self.audit(summary,data)

    def test_same_target_duplicate_is_a_bug(self):
        summary,data=self.fixture()
        data['protection-transfers.csv'].append(dict(task_id='1',kind='RECOVERY_INPUT'))
        with self.assertRaises(ValueError): self.audit(summary,data)

    def test_refetch_after_wrong_target_or_failed_is_legal(self):
        for reason in ('WRONG_TARGET_REFETCH','FAILED_PREFETCH_REFETCH'):
            summary,data=self.fixture(); r=summary['tasks'][0]
            r.update(handed_off=False,used_ns=-1,used_bytes=0,unused_bytes=100,refetch_reason=reason,byte_category='WRONG_TARGET')
            summary.update(B_prefetch_used=0,B_prefetch_unused=100)
            data['protection-transfers.csv'].append(dict(task_id='1',kind='RECOVERY_INPUT'))
            data['input-prefetch-events.csv']=[]; data['recovery-summary.csv'][0]['input_delivery_mode']='NETWORK'
            self.assertEqual(self.audit(summary,data)['B_prefetch_unused'],100)

    def test_storage_and_identity_must_match(self):
        for change in ('pool','target','start','admitted'):
            summary,data=self.fixture()
            if change=='pool': summary['reserved_bytes_at_end']=1
            if change=='target': summary['tasks'][0]['target']=5
            if change=='start': summary['tasks'][0]['requested_ns']=11
            if change=='admitted': data['input-admission-decisions.csv'][0]['decision']='DEFER'
            with self.subTest(change=change),self.assertRaises(ValueError): self.audit(summary,data)

    def test_illegal_cli_combinations_rejected_before_running(self):
        binary=ROOT/'build/contrib/satcompute/ns3.48-satcompute-default'
        for mode,staging,admission in (('fixed','eager','ser-break-even'),('compfrr','eager','net-ready-break-even'),
            ('recompute','deferred','ser-break-even'),('compfrr','deferred','invalid')):
            result=subprocess.run([str(binary),f'--protectionMode={mode}',f'--inputStagingPolicy={staging}',
                f'--inputAdmissionPolicy={admission}'],cwd=ROOT,text=True,capture_output=True,timeout=10)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('inputAdmissionPolicy',result.stdout+result.stderr)


if __name__=='__main__': unittest.main()
