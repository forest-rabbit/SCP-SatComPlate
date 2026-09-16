#!/usr/bin/env python3
"""Stage 2A: exactly one 1Gbps seed1/run11 P development execution; no matrix."""
import argparse
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import time

ROOT=Path(__file__).resolve().parents[5]
SUPPORT=ROOT/'contrib/satcompute/tests/support/protection'
RUN=runpy.run_path(str(Path(__file__).with_name('run-multitree-comparison.py')))
BASE=runpy.run_path(str(SUPPORT/'multitree_comparison_audit.py'))
COVERAGE=runpy.run_path(str(SUPPORT/'candidate_coverage_audit.py'))
CONTROL=RUN['CONTROL']; require=CONTROL['require']
REFERENCE='ebd8a4748893d538c85f0602ab207b74971c93ac'
PRE=ROOT/'output/multitree/bandwidth/1Gbps-run11/compfrr-p'


def source_diff():
    return subprocess.check_output(['git','diff','HEAD','--','contrib/satcompute'],cwd=ROOT,text=True)


def flags(command):
    return dict(t.removeprefix('--').split('=',1) for t in shlex.split(command[-1])[1:])


def verify_identity(command, reference):
    a,b=flags(command),flags(reference)
    for key in ('outputDir','faultTrace'): a.pop(key);b.pop(key)
    require(a==b,'development changed a frozen non-output runtime parameter')
    require((a['islBandwidthBps'],a['randomSeed'],a['randomRun'],a['simulationDuration'])==
            ('1000000000','1','11','1300'),'unauthorized development scene')


def audit(directory):
    old=BASE['audit'](PRE)
    new=BASE['audit'](directory,allow_development=True)
    verify_identity(new['execution']['command'],old['execution']['command'])
    cov=COVERAGE['analyze'](directory)
    left={c['task_id']:c for c in old['catches']};right={c['task_id']:c for c in new['catches']}
    paired=[k for k in left.keys() & right.keys() if BASE['same_primary_fault'](left[k],right[k]) and
            left[k]['catch_ns'] is not None and right[k]['catch_ns'] is not None]
    old_failed=set(old['summary']['failed_task_ids']);new_failed=set(new['summary']['failed_task_ids'])
    fault_fields=('simulation_time_ns','node_id','fault_type','event_type','start_time_ns','duration_ns','fault_source')
    fault_signature=lambda root: [tuple(r[k] for k in fault_fields) for r in COVERAGE['rows'](root,'fault-events.csv')]
    result=dict(development_only=True,status='PASS',before=old['summary'],after=new['summary'],coverage=cov,
        outcome_changes=dict(newly_completed_task_ids=sorted(old_failed-new_failed),
                             newly_failed_task_ids=sorted(new_failed-old_failed)),
        primary_fault_cohorts=dict(common_tasks=len(left.keys() & right.keys()),
            matching_signatures=sum(BASE['same_primary_fault'](left[k],right[k]) for k in left.keys() & right.keys()),
            old_only_task_ids=sorted(left.keys()-right.keys(),key=int),
            new_only_task_ids=sorted(right.keys()-left.keys(),key=int),
            physical_event_sequence_equal=fault_signature(PRE)==fault_signature(directory)),
        paired_catch=dict(tasks=len(paired),before_ms=BASE['distribution']([left[k]['catch_ns']/1e6 for k in paired]),
                         after_ms=BASE['distribution']([right[k]['catch_ns']/1e6 for k in paired])),
        caveat='Online generate has identical model/streams, not guaranteed identical realized faults; missing catch is not zero.')
    CONTROL['write_json'](directory.parent/'development-comparison.json',result)
    print(json.dumps(result,indent=2),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'output/compfrr/candidate-coverage-stage2a/development-run11')
    parser.add_argument('--audit-only',action='store_true')
    args=parser.parse_args();directory=args.output.resolve()
    if args.audit_only: audit(directory);return
    require(not directory.exists(),'refuse to overwrite an existing execution')
    head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    branch=subprocess.check_output(['git','branch','--show-current'],cwd=ROOT,text=True).strip()
    require(head==REFERENCE and branch=='fix/compfrr-p-candidate-coverage','unexpected Stage 2A base/branch')
    diff=source_diff();require(bool(diff),'development patch absent')
    argv=RUN['arguments'](directory,'compfrr-p',isl_bandwidth_bps=10**9)
    command=[str(ROOT/'ns3'),'run','--no-build',shlex.join(argv)]
    reference=json.loads((PRE/'execution.json').read_text())
    verify_identity(command,reference['command'])
    directory.mkdir(parents=True,exist_ok=False)
    (directory/'source-diff.patch').write_text(diff)
    record=dict(reference,stage='candidate-coverage-development',development_only=True,command=command,
                commit=head,worktree_dirty=True,branch=branch,source_diff='source-diff.patch',
                started_utc=CONTROL['utc'](),status='RUNNING',label='CompFRR-P fixed-local coverage development')
    CONTROL['write_json'](directory/'execution.json',record)
    start=time.monotonic()
    with (directory/'run.log').open('w') as log:
        execution=subprocess.run(['/usr/bin/time','-v','-o',str(directory/'time.txt'),*command],
                                 cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    CONTROL['write_json'](directory/'execution-result.json',dict(returncode=execution.returncode,
        elapsed_wall_s=time.monotonic()-start,ended_utc=CONTROL['utc'](),
        status='FINISHED' if execution.returncode==0 else 'FAILED'))
    require(source_diff()==diff,'execution source changed during development run')
    require(execution.returncode==0,'development simulation failed; preserve evidence and inspect run.log')
    audit(directory)


if __name__=='__main__':main()
