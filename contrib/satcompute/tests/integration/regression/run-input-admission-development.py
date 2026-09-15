#!/usr/bin/env python3
"""Final clean-source D/S development closure; no threshold search or formal matrix."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import time
import zipfile

ROOT=Path(__file__).resolve().parents[5]
API=runpy.run_path(str(Path(__file__).parents[2]/'support/protection/input_admission_runtime_audit.py'))
PREPARE=runpy.run_path(str(Path(__file__).with_name('run-input-start-calibration.py')))['prepare']
GROUPS={'D':('deferred','none'),'S':('deferred','ser-break-even')}
APPROVED_ANCESTOR='a4315e2b88ac70b614e457757237cb6307da76dc'


def execution_identity(gates):
    """Require tested clean source, not a hardcoded historical implementation parent."""
    def git(*args):
        return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()
    if not gates.get('all_passed'):
        raise ValueError('small semantic gates must pass before development runs')
    if git('status','--porcelain'):
        raise ValueError('final D/S execution requires a clean committed worktree')
    if subprocess.run(['git','merge-base','--is-ancestor',APPROVED_ANCESTOR,'HEAD'],cwd=ROOT).returncode:
        raise ValueError('approved INPUT implementation is not an ancestor')
    head,tree,branch=git('rev-parse','HEAD'),git('rev-parse','HEAD^{tree}'),git('branch','--show-current')
    if gates.get('tested_tree')!=tree:
        raise ValueError('small gates do not certify this execution source tree')
    return dict(commit=head,source_tree=tree,branch=branch,worktree_dirty=False,
                approved_implementation_ancestor=APPROVED_ANCESTOR)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root',type=Path,required=True)
    parser.add_argument('--gates',type=Path,required=True)
    args=parser.parse_args(); output=args.output_root.resolve()
    if output.exists(): parser.error('refusing to overwrite an existing execution')
    gates=json.loads(args.gates.read_text())
    identity=execution_identity(gates)
    # Archive bytes and compare them directly. No SHA-256, crypto seal, or auto-commit.
    files=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z',
                                  '--','contrib/satcompute'],cwd=ROOT).split(b'\0')
    sources={f.decode():(ROOT/f.decode()).read_bytes() for f in files if f and (ROOT/f.decode()).is_file()}
    output.mkdir(parents=True)
    with zipfile.ZipFile(output/'execution-source.zip','x',zipfile.ZIP_DEFLATED) as archive:
        for path,data in sorted(sources.items()): archive.writestr(path,data)
    (output/'implementation.patch').write_bytes(subprocess.check_output(['git','diff','--binary'],cwd=ROOT))
    (output/'small-gates.json').write_text(json.dumps(gates,indent=2)+'\n')
    def unchanged():
        if execution_identity(gates)!=identity:
            raise RuntimeError('execution identity changed during final closure')
        if any((ROOT/p).read_bytes()!=data for p,data in sources.items()):
            raise RuntimeError('source changed during frozen development execution')
    def execute(group):
        unchanged(); path=output/group
        source,command=PREPARE(ROOT/'output/checkpoint-maintenance-fixed/run-11/full',path)
        argv=shlex.split(command[-1]); staging,admission=GROUPS[group]
        argv=[f'--inputStagingPolicy={staging}' if a.startswith('--inputStagingPolicy=') else
              '--inputStartAudit=0' if a.startswith('--inputStartAudit=') else a for a in argv]
        argv.append(f'--inputAdmissionPolicy={admission}'); command[-1]=shlex.join(argv)
        path.mkdir()
        meta=dict(source,**identity,command=command,
            execution_source_archive=str(output/'execution-source.zip'),source_file_count=len(sources),
            purpose='FINAL_DEVELOPMENT_CLOSURE',
            final_performance_result=False,input_staging_policy=staging,input_admission_policy=admission,
            input_start_audit=False,selective_input_enabled=admission!='none')
        (path/'execution.json').write_text(json.dumps(meta,indent=2)+'\n')
        began=time.monotonic(); print(json.dumps(dict(group=group,status='STARTED')),flush=True)
        with (path/'execution.log').open('x') as stream:
            result=subprocess.run(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
        status=dict(returncode=result.returncode,wall_clock_s=time.monotonic()-began)
        (path/'execution-result.json').write_text(json.dumps(status,indent=2)+'\n')
        unchanged(); print(json.dumps(dict(group=group,**status)),flush=True)
        if result.returncode: return group,dict(status='EXECUTION_FAILED',**status)
        audit=API['analyze'](path)
        if audit['tasks']!=800 or audit['simulation_duration_ns']!=1300*10**9:
            raise RuntimeError('development run did not execute the full frozen scene')
        (path/'input-admission-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
        return group,audit
    with ThreadPoolExecutor(max_workers=2) as pool:
        results=dict(pool.map(execute,GROUPS))
    (output/'comparison.json').write_text(json.dumps(results,indent=2)+'\n')
    if any('completed' not in r for r in results.values()):
        raise RuntimeError('final closure execution failed; preserve outputs and audit before retry')
    if all('completed' in r for r in results.values()):
        paired=API['paired_comparison']({g:output/g for g in GROUPS},results)
        (output/'paired-comparison.json').write_text(json.dumps(paired,indent=2)+'\n')
    print(json.dumps(dict(status='STOP_FOR_MANUAL_REVIEW',output=str(output))),flush=True)


if __name__=='__main__': main()
