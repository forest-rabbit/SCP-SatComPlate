#!/usr/bin/env python3
"""ONE explicitly approved run11 development trace, never a performance matrix."""
import argparse
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[5]
REFERENCE = ROOT / 'output/checkpoint-maintenance-fixed/run-11/full'


def prepare(reference, output):
    """Freeze every nonlogging canonical argument; no user parameter overrides."""
    execution = json.loads((reference / 'execution.json').read_text())
    if execution['commit'] != 'c7889de89cf02363a2694a18fa2d5fa59a1b18e4' or execution['worktree_dirty']:
        raise ValueError('incorrect corrected-chain execution identity')
    original = shlex.split(execution['command'][-1])
    controls = dict(a.split('=', 1) for a in original[1:])
    expected = {'--simulationDuration': '1300', '--randomSeed': '1', '--randomRun': '11',
        '--protectionMode': 'compfrr', '--placementMode': 'n5c', '--n5cVariant': 'full',
        '--inputStagingPolicy': 'deferred', '--remoteBusyRecoveryPolicy': 'relocate', '--faultMode': 'generate'}
    if original[0] != 'satcompute' or any(controls.get(k) != v for k, v in expected.items()):
        raise ValueError('canonical run11 controls mismatch')
    workload = json.loads((ROOT / controls['--taskTrace']).read_text())['tasks']
    if len(workload) != 800:
        raise ValueError('not the frozen 800-task workload')
    # Byte equality of versioned scene files is an identity check, not SHA-256.
    for key in ('--taskTrace', '--computeProfile', '--constellationConfig'):
        path = Path(controls[key])
        old = subprocess.check_output(['git', 'show', execution['commit']+':'+str(path)], cwd=ROOT)
        if old != (ROOT / path).read_bytes():
            raise ValueError('canonical input changed: '+key)
    argv = [f'--outputDir={output}' if a.startswith('--outputDir=') else
            f'--faultTrace={output}/fault-trace.json' if a.startswith('--faultTrace=') else a for a in original]
    argv.append('--inputStartAudit=1')
    changed = dict(a.split('=', 1) for a in argv[1:])
    if {k: v for k, v in controls.items() if k not in ('--outputDir', '--faultTrace')} != {
            k: v for k, v in changed.items() if k not in ('--outputDir', '--faultTrace', '--inputStartAudit')}:
        raise AssertionError('nonlogging parameter drift')
    return execution, [str(ROOT / 'ns3'), 'run', '--no-build', shlex.join(argv)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--gate-root', type=Path, required=True, help='Passed small off/on equivalence outputs')
    args = parser.parse_args()
    output, gate = args.output_dir.resolve(), args.gate_root.resolve()
    if output.exists() or output == REFERENCE or output.is_relative_to(REFERENCE):
        parser.error('refusing to overwrite evidence')
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip():
        parser.error('commit and verify implementation before the development execution')
    compare = runpy.run_path(str(Path(__file__).with_name('run-protection-equivalence.py')))['compare']
    gates = dict(corrected_baseline=compare(ROOT / 'output/n5r-equivalence/final-owner-closeout', gate / 'off'),
                 logging_on_off=compare(gate / 'off', gate / 'on', allow_input_start_audit=True))
    source, command = prepare(REFERENCE, output)
    execution = dict(source, command=command,
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        branch=subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
        worktree_dirty=False, input_start_audit=True, selective_input_enabled=False,
        purpose='DEVELOPMENT_CALIBRATION', final_performance_result=False,
        canonical_reference=str(REFERENCE), canonical_reference_commit=source['commit'], equivalence_gates=gates)
    output.mkdir(parents=True)
    (output / 'execution.json').write_text(json.dumps(execution, indent=2)+'\n')
    print(json.dumps(dict(status='STARTED', output=str(output), commit=execution['commit'], purpose=execution['purpose'])), flush=True)
    began = time.monotonic()
    with (output / 'execution.log').open('x') as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    complete = dict(returncode=result.returncode, wall_clock_s=time.monotonic()-began)
    (output / 'execution-result.json').write_text(json.dumps(complete, indent=2)+'\n')
    print(json.dumps(complete), flush=True)
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
