#!/usr/bin/env python3
"""Stage 2B: passive equivalence gates and at most one frozen online development run."""
import argparse
import filecmp
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[5]
SUPPORT = ROOT/'contrib/satcompute/tests/support/protection'
BASE = runpy.run_path(str(SUPPORT/'multitree_comparison_audit.py'))
require = BASE['require']
REFERENCE = 'b69299e6a04cf2b57a1445175ea6c38d84d9bae1'
TASKS = (5,12,13,193,244,300,302,304,513,584,766)
OLD = ROOT/'output/compfrr/candidate-coverage-stage2a/development-run11'
OUT = ROOT/'output/compfrr/1g-residual-deadline-audit'


def write(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')


def compare_files(before, after, *, skip=()):
    """Exact content comparisons, no digests or schema/number normalization."""
    files = [p.relative_to(before) for p in before.rglob('*') if p.is_file() and p.name not in skip]
    missing = [str(p) for p in files if not (after/p).is_file()]
    changed = [str(p) for p in files if (after/p).is_file() and
               not filecmp.cmp(before/p, after/p, shallow=False)]
    require(not missing and not changed, f'semantic evidence differs: missing={missing}, changed={changed}')
    return dict(status='PASS', identical_files=len(files))


def compare_run_summary(before, after):
    a,b=(json.loads((p/'run-summary.json').read_text()) for p in (before,after))
    for key in ('wall_clock_ns','wall_clock_s'):
        a.pop(key);b.pop(key)
    require(a==b,'run summary differs beyond measured host wall clock')
    return dict(status='PASS',ignored_fields=['wall_clock_ns','wall_clock_s'])


def source_patch():
    prefix = 'contrib/satcompute'
    def production(name):
        return name.endswith(('.cc','.h','CMakeLists.txt')) and '/tests/' not in name
    tracked = subprocess.check_output(['git','ls-files',prefix], cwd=ROOT, text=True).splitlines()
    names = [p for p in tracked if production(p)]
    result = subprocess.check_output(['git','diff','HEAD','--',*names], cwd=ROOT, text=True)
    added = subprocess.check_output(['git','ls-files','--others','--exclude-standard',prefix],
                                    cwd=ROOT, text=True).splitlines()
    for path in sorted(p for p in added if production(p)):
        diff = subprocess.run(['git','diff','--no-index','--','/dev/null',path],
                              cwd=ROOT, text=True, capture_output=True)
        require(diff.returncode == 1, 'unable to record new diagnostic source')
        result += diff.stdout
    return result


def inventory():
    roots = [ROOT/'output/multitree', ROOT/'output/compfrr/1g-candidate-feasibility-audit', OLD.parent]
    return {str(p.relative_to(ROOT)): [p.stat().st_size, p.stat().st_mtime_ns]
            for directory in roots for p in directory.rglob('*') if p.is_file()}


def flags(command):
    return dict(t.removeprefix('--').split('=',1) for t in shlex.split(command[-1])[1:])


def verify_identity(command, reference):
    a,b = flags(command),flags(reference)
    require(a.pop('compfrrResidualDeadlineTasks') == ','.join(map(str,TASKS)), 'diagnostic population changed')
    for k in ('outputDir','faultTrace'): a.pop(k);b.pop(k)
    require(a == b, 'frozen runtime arguments differ')
    require((a['faultMode'],a['islBandwidthBps'],a['randomSeed'],a['randomRun'],a['simulationDuration']) ==
            ('generate','1000000000','1','11','1300'), 'unauthorized run or replay')


def gates():
    old = ROOT/'output/compfrr/candidate-coverage-stage2a/unit-after'
    off,on = OUT/'unit-off',OUT/'unit-on-verified'
    result = dict(previous_fixtures=compare_files(old,off), passive_equivalence=compare_files(off,on))
    extra = {p.relative_to(on) for p in on.rglob('*') if p.is_file()} - {
        p.relative_to(off) for p in off.rglob('*') if p.is_file()}
    require(extra and all(p.name == 'residual-deadline-candidates.json' for p in extra),
            'logging changed output population beyond the diagnostic')
    require(any(json.loads((on/p).read_text())['records'] for p in extra), 'empty passive gate coverage')
    result['additional_diagnostic_files'] = len(extra)
    result['status'] = 'PASS'
    write(OUT/'small-gates.json', result)
    return result


def audit():
    current = OUT/'instrumented-run11'
    # Host wall time and execution metadata are not simulated semantics.
    equivalence = compare_files(OLD,current,skip=('execution.json','execution-result.json',
        'time.txt','run.log','source-diff.patch','run-summary.json'))
    equivalence['run_summary']=compare_run_summary(OLD,current)
    report = BASE['audit'](current, allow_development=True)
    verify_identity(report['execution']['command'],json.loads((OLD/'execution.json').read_text())['command'])
    require(inventory() == json.loads((OUT/'old-evidence-inventory.json').read_text()), 'historical evidence changed')
    result = dict(status='PASS', development_only=True, semantic_equivalence=equivalence,
                  preserved_historical_files=len(inventory()), summary=report['summary'])
    write(OUT/'execution-equivalence.json',result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gates-only',action='store_true')
    parser.add_argument('--audit-only',action='store_true')
    args = parser.parse_args()
    if args.audit_only: print(json.dumps(audit(),indent=2));return
    print(json.dumps(gates(),indent=2),flush=True)
    if args.gates_only:return
    destination = OUT/'instrumented-run11'
    require(not destination.exists(), 'refuse a second execution or overwrite')
    head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    branch = subprocess.check_output(['git','branch','--show-current'],cwd=ROOT,text=True).strip()
    require(head == REFERENCE and branch == 'fix/compfrr-p-candidate-coverage', 'unexpected audited base')
    patch = source_patch(); require(bool(patch), 'missing passive instrumentation patch')
    require('residual-deadline-audit.cc' in patch, 'new diagnostic source missing from patch')
    old = json.loads((OLD/'execution.json').read_text())
    argv = shlex.split(old['command'][-1])
    argv = [f'--outputDir={destination}' if t.startswith('--outputDir=') else
            f'--faultTrace={destination}/fault-trace.json' if t.startswith('--faultTrace=') else t for t in argv]
    argv += ['--compfrrResidualDeadlineTasks='+','.join(map(str,TASKS))]
    command = [str(ROOT/'ns3'),'run','--no-build',shlex.join(argv)]
    verify_identity(command,old['command'])
    write(OUT/'old-evidence-inventory.json',inventory())
    destination.mkdir(exist_ok=False)
    (destination/'source-diff.patch').write_text(patch)
    write(destination/'execution.json',dict(old,stage='residual-deadline-development',commit=head,
        command=command,development_only=True,worktree_dirty=True,branch=branch,
        label='Stage 2B passive residual deadline development',started_unix_s=time.time(),
        started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),status='RUNNING'))
    start=time.monotonic()
    with (destination/'run.log').open('w') as log:
        process=subprocess.run(['/usr/bin/time','-v','-o',str(destination/'time.txt'),*command],
                               cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    write(destination/'execution-result.json',dict(returncode=process.returncode,
        elapsed_wall_s=time.monotonic()-start,status='FINISHED' if process.returncode == 0 else 'FAILED'))
    require(source_patch() == patch,'production source changed during execution')
    require(process.returncode == 0,'simulation failed; preserve evidence and stop')
    print(json.dumps(audit(),indent=2),flush=True)


if __name__ == '__main__':main()
