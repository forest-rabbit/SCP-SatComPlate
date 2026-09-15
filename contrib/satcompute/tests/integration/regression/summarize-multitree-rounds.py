#!/usr/bin/env python3
"""Read-only three-round audit; aggregate actual observations without model fitting."""
import argparse
import csv
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
            require((values['randomSeed'], values['ecmpHashSeed'], values['randomRun'], values['islBandwidthBps']) ==
                    ('1', '1', str(run), '10000000000'), 'run/seed/bandwidth identity changed')
            require((execution['seed'], execution['run']) == (1, run), 'metadata/argv mismatch')
            stable = {k: v for k, v in values.items() if k not in ('randomRun', 'outputDir', 'faultTrace')}
            if reference is None:
                reference = stable
            require(stable == reference, f'{group}: non-run parameter changed')


def source_equivalence(reports, reference='A'):
    base = reports[reference]['commit']
    evidence = {}
    for label, report in reports.items():
        changed = subprocess.check_output(
            ['git', 'diff', '--name-only', base, report['commit']], cwd=ROOT, text=True).splitlines()
        require(all(p == 'AGENTS.md' or p.endswith('.md') or
                    p.startswith('contrib/satcompute/tests/') for p in changed),
                f'Run {label}: simulator/input/calibration changes need separate review')
        evidence[label] = dict(commit=report['commit'], changed_paths_from_a=changed)
    return evidence


def verify_bandwidth_identity(reports):
    expected = {'1Gbps': 1000000000, '10Gbps': 10000000000, '100Gbps': 100000000000}
    require(set(reports) == set(expected), 'need exactly 1/10/100 Gbps')
    for label, report in reports.items():
        missing = set(GROUPS) - report['groups'].keys()
        excluded = report.get('excluded_groups', {})
        require(not (report['groups'].keys() - set(GROUPS)) and missing == excluded.keys(),
                'unexpected/missing scheme or stale exclusion')
        require(not missing or (label == '100Gbps' and missing == {'cb-sat'} and
                excluded['cb-sat']['status'] == 'CANCELLED_BY_USER'), 'unauthorized exclusion')
    for group in GROUPS:
        reference = None
        for label, bandwidth in expected.items():
            if group not in reports[label]['groups']:
                excluded = reports[label].get('excluded_groups', {})
                require(label == '100Gbps' and group == 'cb-sat' and
                        excluded.get(group, {}).get('status') == 'CANCELLED_BY_USER',
                        'missing execution without the authorized cancellation')
                continue
            execution = reports[label]['groups'][group]['execution']
            values = flags(execution)
            require((values['randomSeed'], values['ecmpHashSeed'], values['randomRun'], values['islBandwidthBps']) ==
                    ('1', '1', '11', str(bandwidth)), 'bandwidth/run/seed identity changed')
            require((execution['seed'], execution['run']) == (1, 11), 'metadata/argv mismatch')
            stable = {k: v for k, v in values.items() if k not in ('islBandwidthBps', 'outputDir', 'faultTrace')}
            if reference is None:
                reference = stable
            require(stable == reference, f'{group}: non-bandwidth parameter changed')


def audit_bandwidth_case(root, allow_cancelled=False):
    """Audit finished executions, preserving the explicitly cancelled CB-SAT gap."""
    groups, excluded = list(GROUPS), {}
    marker = root/'cancellation.json'
    if marker.exists():
        receipt = json.loads(marker.read_text())
        require(allow_cancelled and receipt['status'] == 'INCOMPLETE_USER_CANCELLED' and
                receipt['cancelled_groups'] == ['cb-sat'], 'unexpected cancellation scope')
        state = json.loads((root/'matrix-status.json').read_text())
        require(state['groups']['cb-sat'] == 'CANCELLED_BY_USER', 'cancellation status mismatch')
        execution = json.loads((root/'cb-sat/execution.json').read_text())
        values = flags(execution)
        require((values['randomSeed'], values['randomRun'], values['ecmpHashSeed'], values['islBandwidthBps']) ==
                ('1', '11', '1', '100000000000'), 'cancelled execution identity mismatch')
        outcome = root/'cb-sat/execution-result.json'
        require(not outcome.exists() or json.loads(outcome.read_text())['status'] != 'FINISHED',
                'completed execution cannot be silently excluded')
        groups.remove('cb-sat')
        excluded['cb-sat'] = dict(status='CANCELLED_BY_USER', reason=receipt['reason'],
            last_observed_complete_link_window_s=receipt['last_observed_complete_link_window_s'],
            evidence_root=str(root/'cb-sat'))
    report = API['comparison'](root, groups)
    report['excluded_groups'] = excluded
    return report


def bandwidth_report(reports, roots):
    verify_bandwidth_identity(reports)
    exclusions = {label: r['excluded_groups'] for label, r in reports.items() if r.get('excluded_groups')}
    return dict(status='PASS_COMPLETED_EXECUTIONS_WITH_EXCLUSION' if exclusions else 'PASS',
                excluded_groups=exclusions,
                source_equivalence=source_equivalence(reports, reference='10Gbps'),
                evidence_roots={k: str(v) for k, v in roots.items()},
                per_bandwidth={label: {g: r['summary'] for g, r in report['groups'].items()}
                               for label, report in reports.items()},
                paired_vs_compfrr_p={label: report['paired_vs_compfrr_p'] for label, report in reports.items()},
                scope='Only islBandwidthBps varies. seed1/run11/ecmpHashSeed1 and all algorithms/calibration stay frozen. '
                      'Do not pool different bandwidths as random repetitions.')


def export_cases(path, reports):
    """One actual execution per row; no pooling across bandwidths or lost raw fields."""
    fields = ('tasks', 'completed', 'extra_sent_bytes', 'proactive_lifetime_sent_bytes', 'recovery_sent_bytes',
              'total_network_sent_bytes', 'primary_fault_catch_cohort', 'no_observed_catch',
              'task_execution_waste_wu', 'normal_protection_eq_wu', 'reserved_idle_eq_wu', 'w_waste_actual',
              'mean_link_utilization_percent', 'max_link_whole_run_utilization_percent')
    records = []
    for case, report in reports.items():
        for group, r in report['groups'].items():
            s, e = r['summary'], r['execution']
            values = flags(e)
            records.append(dict(case=case, scheme=group, random_seed=e['seed'], random_run=e['run'],
                isl_bandwidth_bps=int(values['islBandwidthBps']),
                **{k: s[k] for k in fields}, caught=s['fault_to_catch_ms']['count'],
                catch_mean_ms=s['fault_to_catch_ms']['mean'], catch_p90_ms=s['fault_to_catch_ms']['p90'],
                faults_f1=s['faults']['F1'], faults_f2=s['faults']['F2'], faults_f3=s['faults']['F3'],
                execution_commit=e['commit'], output_root=values['outputDir']))
    with path.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(records[0]))
        writer.writeheader(); writer.writerows(records)


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
    if 'bandwidth' in report:
        data = report['bandwidth']['per_bandwidth']
        labels = ('1Gbps', '10Gbps', '100Gbps')
        lines += ['## run11 带宽对照', '',
                  '每组均 800 任务；单元格为“完成数；catch 均值 ms [已追平/样本]”。未追平不填零。', '',
                  '| 方案 | 1 Gbps | 10 Gbps | 100 Gbps |', '| --- | ---: | ---: | ---: |']
        for group in GROUPS:
            cells = []
            for label in labels:
                if group not in data[label]:
                    cells.append('已取消（不计入）'); continue
                s = data[label][group]; c = s['fault_to_catch_ms']
                cells.append(f"{s['completed']}; {number(c['mean'])} [{c['count']}/{s['primary_fault_catch_cohort']}]")
            lines.append('| '+report['groups'][group]['label']+' | '+' | '.join(cells)+' |')
        lines += ['', '单元格为“额外实际流量 GB；总浪费 M eq-WU”。不将少完成任务造成的少发流量视作优化。', '',
                  '| 方案 | 1 Gbps | 10 Gbps | 100 Gbps |', '| --- | ---: | ---: | ---: |']
        for group in GROUPS:
            cells = [f"{data[label][group]['extra_sent_bytes']/1e9:.6f}; {data[label][group]['w_waste_actual']/1e6:.6f}"
                     if group in data[label] else '已取消（不计入）'
                     for label in labels]
            lines.append('| '+report['groups'][group]['label']+' | '+' | '.join(cells)+' |')
        lines += ['', '带宽对照仅改变链路容量；三组均 seed1/run11，不重新拟合树或 MTBF，不调整 deadline。',
                  '各带宽的实际故障集合可能不同。组内配对和全部已完成执行分项见 JSON / cases.csv；取消项不补零。', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('a', 'b', 'c'):
        parser.add_argument('--run-'+name, type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--bandwidth-1gbps', type=Path)
    parser.add_argument('--bandwidth-100gbps', type=Path)
    args = parser.parse_args()
    require(bool(args.bandwidth_1gbps) == bool(args.bandwidth_100gbps), 'supply both bandwidth roots together')
    roots = {label: getattr(args, 'run_'+label.lower()).resolve() for label in ('A', 'B', 'C')}
    # Audit raw evidence again; never trust a stale aggregate or rerun the simulation.
    reports = {label: API['comparison'](root, GROUPS) for label, root in roots.items()}
    result = aggregate(reports)
    result['source_equivalence'] = source_equivalence(reports)
    result['evidence_roots'] = {label: str(root) for label, root in roots.items()}
    cases = {f'10Gbps-Run-{label}': r for label, r in reports.items()}
    evidence = list(roots.values())
    if args.bandwidth_1gbps:
        band_roots = {'1Gbps': args.bandwidth_1gbps.resolve(), '10Gbps': roots['A'],
                      '100Gbps': args.bandwidth_100gbps.resolve()}
        band_reports = {label: reports['A'] if label == '10Gbps' else
                        audit_bandwidth_case(root, allow_cancelled=label == '100Gbps')
                        for label, root in band_roots.items()}
        result['bandwidth'] = bandwidth_report(band_reports, band_roots)
        cases.update({f'{label}-Run-A': band_reports[label] for label in ('1Gbps', '100Gbps')})
        evidence.extend(band_roots.values())
    result['completed_simulations'] = sum(len(r['groups']) for r in cases.values())
    result['cancelled_simulations'] = sum(len(r.get('excluded_groups', {})) for r in cases.values())
    require(args.output_root.resolve() not in evidence, 'do not overwrite an execution root')
    args.output_root.mkdir(parents=True, exist_ok=False)
    (args.output_root/'comparison.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    (args.output_root/'comparison.md').write_text(markdown(result))
    export_cases(args.output_root/'cases.csv', cases)
    print(markdown(result))


if __name__ == '__main__':
    main()
