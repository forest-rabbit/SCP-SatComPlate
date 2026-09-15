#!/usr/bin/env python3
"""Read-only three-round audit; aggregate actual observations without model fitting."""
import argparse
import json
from pathlib import Path
import runpy
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[5]
API = runpy.run_path(str(ROOT/'contrib/satcompute/tests/support/protection/multitree_comparison_audit.py'))
GROUPS = ('recompute', 'one-plus-one', 'cb-sat', 'multitree', 'compfrr-p', 'compfrr-fa-ffp')
require = API['require']


def flags(execution):
    tokens = shlex.split(execution['command'][-1])[1:]
    values = dict(t.removeprefix('--').split('=', 1) for t in tokens)
    require(len(tokens) == len(values), 'duplicate runtime flag')
    return values


def verify_round_identity(reports):
    """Within each scheme the only experimental variable is randomRun."""
    expected = {'A': 11, 'B': 12, 'C': 13}
    require(set(reports) == set(expected), 'need exactly Run A, B, C')
    for group in GROUPS:
        reference = None
        for label, run in expected.items():
            execution = reports[label]['groups'][group]['execution']
            values = flags(execution)
            require((values['randomSeed'], values['ecmpHashSeed'], values['randomRun']) ==
                    ('1', '1', str(run)), 'run/seed identity changed')
            require((execution['seed'], execution['run']) == (1, run), 'metadata/argv mismatch')
            stable = {k: v for k, v in values.items() if k not in ('randomRun', 'outputDir', 'faultTrace')}
            if reference is None:
                reference = stable
            require(stable == reference, f'{group}: non-run parameter changed')


def source_equivalence(reports):
    base = reports['A']['commit']
    evidence = {}
    for label, report in reports.items():
        changed = subprocess.check_output(
            ['git', 'diff', '--name-only', base, report['commit']], cwd=ROOT, text=True).splitlines()
        require(all(p == 'AGENTS.md' or p.endswith('.md') or
                    p.startswith('contrib/satcompute/tests/') for p in changed),
                f'Run {label}: simulator/input/calibration changes need separate review')
        evidence[label] = dict(commit=report['commit'], changed_paths_from_a=changed)
    return evidence


def aggregate(reports):
    verify_round_identity(reports)
    result = {}
    for group in GROUPS:
        entries = [reports[label]['groups'][group] for label in ('A', 'B', 'C')]
        summaries = [r['summary'] for r in entries]
        caught = [c['catch_ns']/1e6 for r in entries for c in r['catches'] if c['catch_ns'] is not None]
        metrics = {field: API['distribution']([s[field] for s in summaries]) for field in
                   ('extra_sent_bytes', 'proactive_lifetime_sent_bytes', 'recovery_sent_bytes',
                    'total_network_sent_bytes', 'task_execution_waste_wu', 'normal_protection_eq_wu',
                    'reserved_idle_eq_wu', 'w_waste_actual', 'mean_link_utilization_percent',
                    'max_link_whole_run_utilization_percent')}
        result[group] = dict(label=summaries[0]['label'],
            tasks=sum(s['tasks'] for s in summaries), completed=sum(s['completed'] for s in summaries),
            completed_by_round=[s['completed'] for s in summaries],
            catch_pooled_ms=API['distribution'](caught),
            catch_run_means_ms=API['distribution']([s['fault_to_catch_ms']['mean'] for s in summaries
                                                   if s['fault_to_catch_ms']['mean'] is not None]),
            primary_fault_catch_cohort=sum(s['primary_fault_catch_cohort'] for s in summaries),
            no_observed_catch=sum(s['no_observed_catch'] for s in summaries),
            metrics_across_rounds=metrics)
    paired = {}
    for other in GROUPS:
        if other == 'compfrr-p':
            continue
        left, right, identities = [], [], []
        common_count = 0
        for label in ('A', 'B', 'C'):
            groups = reports[label]['groups']
            fa = {c['task_id']: c for c in groups[other]['catches']}
            fb = {c['task_id']: c for c in groups['compfrr-p']['catches']}
            for key in sorted(fa.keys() & fb.keys(), key=int):
                if not API['same_primary_fault'](fa[key], fb[key]):
                    continue
                common_count += 1
                if fa[key]['catch_ns'] is None or fb[key]['catch_ns'] is None:
                    continue
                left.append(fa[key]['catch_ns']/1e6)
                right.append(fb[key]['catch_ns']/1e6)
                identities.append(dict(round=label, task_id=int(key), fault_signature=fa[key]['fault_signature']))
        reference, candidate = API['distribution'](left), API['distribution'](right)
        paired[other] = dict(same_physical_primary_faults=common_count, both_caught=len(left),
            reference_catch_ms=reference, compfrr_p_catch_ms=candidate, identities=identities,
            compfrr_p_reduction_percent=(100*(1-candidate['mean']/reference['mean'])
                                         if reference['mean'] else None))
    return dict(status='PASS', groups=result, paired_vs_compfrr_p=paired,
                per_round={label: {g: r['summary'] for g, r in report['groups'].items()}
                           for label, report in reports.items()},
                scope='Three frozen rounds, randomSeed=1, ecmpHashSeed=1, randomRun=11/12/13 only. '
                      'Online realized faults remain endogenous; no parameter tuning or forced replay.',
                units='Decimal GB, ms; total waste is execution WU plus normal and reserved-idle equivalent costs.',
                aggregation='Completion and pooled catch sum actual samples. Network/waste table values are per-run means. '
                            'Uncaught observations are missing, not zero. Paired identities include round, task, primary, time, type and cause.',
                inference='Three runs describe this frozen workload, not statistical significance or universal superiority.')


def markdown(report):
    lines = ['# Multi-tree 六方案：Run A/B/C 对比', '',
             '三轮固定 randomSeed=1、ecmpHashSeed=1，仅 randomRun=11/12/13。每轮均 800 任务、1300s。', '',
             '| 方案 | A/B/C 完成 | 合计完成 | 平均每轮额外 GB | pooled catch 均值 / P90 ms | 已 catch / 样本 | 平均每轮总浪费 M eq-WU |',
             '| --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    def number(value):
        return '—' if value is None else f'{value:.3f}'
    for s in report['groups'].values():
        m, c = s['metrics_across_rounds'], s['catch_pooled_ms']
        completed = '/'.join(map(str, s['completed_by_round']))
        lines.append(f"| {s['label']} | {completed} | {s['completed']}/{s['tasks']} | "
                     f"{m['extra_sent_bytes']['mean']/1e9:.6f} | {number(c['mean'])} / {number(c['p90'])} | "
                     f"{c['count']}/{s['primary_fault_catch_cohort']} | {m['w_waste_actual']['mean']/1e6:.6f} |")
    lines += ['', 'catch 均值只包含真实追平样本；未追平不填零，必须与完成数一起看。',
              '总浪费含实际执行浪费、常态等效成本、实际预留空等等效成本，不等于纯 CPU 消耗。', '',
              '## 相同故障且双方均追平的配对比较', '',
              '| 对照方案 | 共同故障 / 双方追平 | 对照 catch ms | CompFRR-P catch ms | CompFRR-P 降低 |',
              '| --- | ---: | ---: | ---: | ---: |']
    for name, pair in report['paired_vs_compfrr_p'].items():
        lines.append(f"| {report['groups'][name]['label']} | {pair['same_physical_primary_faults']} / {pair['both_caught']} | "
                     f"{number(pair['reference_catch_ms']['mean'])} | {number(pair['compfrr_p_catch_ms']['mean'])} | "
                     f"{number(pair['compfrr_p_reduction_percent'])}% |")
    lines += ['', '各对照的配对集合不同，不能拿此表不同列集合的均值直接相互排名。',
              '三轮仅描述该固定场景，不声称统计显著性。各轮、资源分项、流量和链路利用率见 JSON。', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('a', 'b', 'c'):
        parser.add_argument('--run-'+name, type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    roots = {label: getattr(args, 'run_'+label.lower()).resolve() for label in ('A', 'B', 'C')}
    # Audit raw evidence again; never trust a stale aggregate or rerun the simulation.
    reports = {label: API['comparison'](root, GROUPS) for label, root in roots.items()}
    result = aggregate(reports)
    result['source_equivalence'] = source_equivalence(reports)
    result['evidence_roots'] = {label: str(root) for label, root in roots.items()}
    require(args.output_root.resolve() not in roots.values(), 'do not overwrite a round root')
    args.output_root.mkdir(parents=True, exist_ok=False)
    (args.output_root/'comparison.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    (args.output_root/'comparison.md').write_text(markdown(result))
    print(markdown(result))


if __name__ == '__main__':
    main()
