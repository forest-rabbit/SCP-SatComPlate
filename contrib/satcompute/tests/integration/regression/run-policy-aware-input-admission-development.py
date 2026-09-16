#!/usr/bin/env python3
"""Run only CompFRR-P seed1/run11 at 5 and 10 Gbps for policy-aware START review."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import runpy
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[5]
SUPPORT = ROOT/'contrib/satcompute/tests/support/protection'
RUN = runpy.run_path(str(Path(__file__).with_name('run-multitree-comparison.py')))
SCENE = runpy.run_path(str(SUPPORT/'scenario.py'))
AUDIT = runpy.run_path(str(SUPPORT/'policy_aware_input_admission_audit.py'))
CONTROL = RUN['CONTROL']
REQUIRE = CONTROL['require']
BASE_COMMIT = 'b69299e6a04cf2b57a1445175ea6c38d84d9bae1'
BRANCH = 'fix/compfrr-policy-aware-input-admission'
BANDWIDTHS = (5_000_000_000, 10_000_000_000)


def source_diff():
    tracked = subprocess.check_output(
        ['git', 'diff', 'HEAD', '--', 'contrib/satcompute'], cwd=ROOT, text=True)
    untracked = subprocess.check_output(
        ['git', 'ls-files', '--others', '--exclude-standard', '--', 'contrib/satcompute'],
        cwd=ROOT, text=True).splitlines()
    patches = [tracked]
    for name in untracked:
        result = subprocess.run(['git', 'diff', '--no-index', '--', '/dev/null', name],
                                cwd=ROOT, text=True, capture_output=True)
        REQUIRE(result.returncode in (0, 1), f'cannot capture untracked source: {name}')
        patches.append(result.stdout)
    return ''.join(patches)


def arguments(directory, bandwidth_bps):
    REQUIRE(bandwidth_bps in BANDWIDTHS, 'only 5/10 Gbps development runs are authorized')
    argv = RUN['arguments'](directory, 'compfrr-p', random_run=11,
                            isl_bandwidth_bps=10_000_000_000)
    argv = [f'--islBandwidthBps={bandwidth_bps}' if token.startswith('--islBandwidthBps=')
            else token for token in argv]
    if bandwidth_bps == 5_000_000_000:
        task_trace = directory/'derived-input'/'task-trace.json'
        receipt = SCENE['bandwidth_normalized_task_trace'](task_trace, bandwidth_bps)
        argv = [f'--taskTrace={task_trace}' if token.startswith('--taskTrace=') else token
                for token in argv]
    else:
        receipt = {
            'task_id': SCENE['CONTROLLED_F3_TASK_ID'],
            'reference_bandwidth_bps': SCENE['REFERENCE_ISL_BANDWIDTH_BPS'],
            'target_bandwidth_bps': bandwidth_bps,
            'reference_arrival_time_ns': 1024682825747,
            'normalized_arrival_time_ns': 1024682825747,
            'task_trace': f"{SCENE['SCENE']}/workload/task-trace.json",
        }
    return argv, receipt


def execute(directory, bandwidth_bps, diff, head):
    directory.mkdir(parents=True, exist_ok=False)
    argv, task_trace = arguments(directory, bandwidth_bps)
    command = [str(ROOT/'ns3'), 'run', '--no-build', shlex.join(argv)]
    label = f'{bandwidth_bps//1_000_000_000} Gbps CompFRR policy-aware START development'
    record = {
        'stage': 'policy-aware-input-admission-development',
        'development_only': True,
        'label': label,
        'protection_mode': 'compfrr',
        'placement_mode': 'compfrr',
        'input_policy': 'selective',
        'remote_busy_recovery_policy': 'relocate',
        'pressure_model': 'cumulative',
        'command': command,
        'commit': head,
        'branch': BRANCH,
        'worktree_dirty': True,
        'source_diff': 'source-diff.patch',
        'seed': 1,
        'run': 11,
        'random_seed': 1,
        'random_run': 11,
        'simulation_duration_s': 1300,
        'isl_bandwidth_bps': bandwidth_bps,
        'fault_mode': 'generate',
        'task_trace_derivation': task_trace,
        'started_utc': CONTROL['utc'](),
        'status': 'RUNNING',
    }
    (directory/'source-diff.patch').write_text(diff)
    CONTROL['write_json'](directory/'execution.json', record)
    started = time.monotonic()
    print(f'START {label}', flush=True)
    with (directory/'run.log').open('w') as log:
        result = subprocess.run(['/usr/bin/time', '-v', '-o', str(directory/'time.txt'), *command],
                                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    outcome = {'returncode': result.returncode, 'elapsed_wall_s': time.monotonic()-started,
               'ended_utc': CONTROL['utc'](),
               'status': 'FINISHED' if result.returncode == 0 else 'FAILED'}
    CONTROL['write_json'](directory/'execution-result.json', outcome)
    REQUIRE(source_diff() == diff, 'source changed during development execution')
    REQUIRE(result.returncode == 0, f'development simulation failed: {directory}/run.log')
    print(f"FINISHED {label}: {outcome['elapsed_wall_s']:.1f} s", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', type=Path,
                        default=ROOT/'output/compfrr/policy-aware-input-admission')
    parser.add_argument('--jobs', type=int, choices=(1, 2), default=2)
    parser.add_argument('--audit-only', action='store_true')
    args = parser.parse_args()
    root = args.output_root.resolve()
    if args.audit_only:
        print(json.dumps(AUDIT['compare'](root), indent=2)); return
    REQUIRE(not root.exists(), 'refuse to overwrite policy-aware development evidence')
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    branch = subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip()
    REQUIRE(head == BASE_COMMIT and branch == BRANCH, 'unexpected development base or branch')
    diff = source_diff()
    REQUIRE(bool(diff), 'development source patch is absent')
    root.mkdir(parents=True, exist_ok=False)
    failures = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending = {pool.submit(execute, root/f'{bandwidth//1_000_000_000}Gbps-run11',
                               bandwidth, diff, head): bandwidth for bandwidth in BANDWIDTHS}
        for future in as_completed(pending):
            try:
                future.result()
            except Exception as error:
                failures.append(f'{pending[future]}: {error}')
    if failures:
        raise RuntimeError('; '.join(failures))
    print(json.dumps(AUDIT['compare'](root), indent=2), flush=True)


if __name__ == '__main__':
    main()
