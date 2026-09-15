"""Six-scheme actual-ledger comparison, including mixed RS/RP. No model fitting."""
from collections import Counter
import json
from pathlib import Path
import runpy
import shlex
import sys

ROOT = Path(__file__).resolve().parents[5]
BASE = runpy.run_path(str(Path(__file__).with_name('baseline_audit.py')))
INPUT = runpy.run_path(str(Path(__file__).with_name('input_admission_runtime_audit.py')))
rows, require = BASE['rows'], BASE['require']
NS = 10**9


def distribution(values):
    return dict(BASE['stats'](values), mean=sum(values)/len(values) if values else None)


def replica_catch_ns(fault_ns, fault_work, attempt):
    """Observed continuous replica service reaches primary W_f, no planned-WU substitution.

    Catch is not takeover: a replica may already be ahead, or may still need INPUT
    and computation. Only a surviving, post-batch promoted attempt can supply catch.
    """
    if not attempt or not attempt['takeover_time_ns'] or not attempt['compute_start_time_ns']:
        return None
    rate = int(attempt['rate_wu_per_s'])
    require(rate > 0, 'invalid replica service rate')
    start, service = int(attempt['compute_start_time_ns']), int(attempt['actual_service_ns'])
    require(int(attempt['takeover_time_ns']) >= fault_ns, 'replica promoted before primary fault')
    required_ns = (fault_work*NS + rate-1)//rate
    if int(attempt['actual_work_units']) < fault_work or service < required_ns:
        return None
    # Completion can predate the fault; its delivered computation remains ready.
    return max(0, start + required_ns - fault_ns)


def same_primary_fault(a, b):
    """Same primary/time/type/cause, not numerical fault IDs or forced replay."""
    return a['fault_signature'] == b['fault_signature']


def audit(root):
    root = Path(root)
    identity = json.loads((root/'execution.json').read_text())
    outcome = json.loads((root/'execution-result.json').read_text())
    run = json.loads((root/'run-summary.json').read_text())
    scheme = identity['protection_mode']
    require(outcome['returncode'] == 0 and outcome['status'] == 'FINISHED' and
            not identity['worktree_dirty'], 'incomplete/dirty execution')
    require(run['simulation_duration_ns'] == round(identity['simulation_duration_s']*NS), 'incomplete horizon')
    ts = rows(root, 'task-summary.csv'); tasks = {r['task_id']: r for r in ts}
    require(len(tasks) == len(ts) and all(t['final_state'] in ('COMPLETED', 'FAILED') for t in ts), 'invalid terminals')
    terminals = Counter(e['task_id'] for e in rows(root, 'task-events.csv') if e['to_state'] in ('COMPLETED', 'FAILED'))
    require(terminals == Counter({k: 1 for k in tasks}), 'logical terminal duplication')
    if identity['stage'] in ('multitree-comparison-run11', 'multitree-comparison'):
        require((len(ts), run['total_input_bytes'], run['total_output_bytes'], run['total_compute_work_units'],
                 run['compute_node_count'], run['simulation_duration_ns']) ==
                (800, 194119753287, 100166291859, 352513119, 66, 1300*NS), 'canonical scene changed')
    impacts = rows(root, 'fault-task-impact.csv')
    attempts = rows(root, 'replica-attempts.csv', True)
    replicas = {r['task_id']: r for r in rows(root, 'replica-summary.csv', True)}
    recovery_name = 'cb-sat-recovery.csv' if scheme == 'cb-sat' else 'recovery-summary.csv'
    recoveries = rows(root, recovery_name, True)
    rs = {r['task_id']: r for r in recoveries}
    require(len(rs) == len(recoveries), 'duplicate recovery')
    extra_report = {}
    if scheme == 'cb-sat':
        tools = ROOT/'contrib/satcompute/protection/baseline/checkbullet/tools'
        sys.path.insert(0, str(tools))
        try: cb = runpy.run_path(str(tools/'audit-cb-sat-run.py'))['audit'](root)
        finally: sys.path.pop(0)
        executed = cb['task_execution']
        network_extra = cb['summary']['extra_sent_bytes']
        network_normal = cb['summary']['normal_ft_sent_bytes']
        extra_report['cb_sat'] = cb['summary']
    else:
        protected = {r['task_id']: r for r in rows(root, 'protection-task-summary.csv', True)}
        if scheme == 'compfrr':
            extra_report['compfrr'] = INPUT['analyze'](root)
        else:
            require(not protected, 'RS/RP acquired checkpoint state')
            for r in recoveries: BASE['ACCOUNTING']['recovery_check'](r)
        for key, r in replicas.items():
            BASE['replica_check'](r, [a for a in attempts if a['task_id'] == key])
        executed = []
        for key, t in tasks.items():
            r, b = rs.get(key, {}), replicas.get(key, {})
            normal = float(protected.get(key, {}).get('normal_protection_eq_wu') or 0)
            idle = float(r.get('recovery_reserved_idle_eq_wu') or 0) + float(b.get('replica_reserved_idle_eq_wu') or 0)
            primary = [i for i in impacts if i['task_id'] == key and i['impact_type'].startswith('RUNNING_INTERRUPTED')]
            e = BASE['task_execution'](t, r, b, [a for a in attempts if a['task_id'] == key], primary, normal, idle)
            executed.append(dict(task_id=key, normal_protection_eq_wu=normal, reserved_idle_eq_wu=idle, **e))
        physical = BASE['physical_network'](root, ts, extra_kinds=('PREFETCH_INPUT',))
        network_extra = physical['extra_sent_bytes']
        # Classify by mechanism/lifetime, not only the prefix before the fault.
        network_recovery = sum(physical['by_kind'][k]['sent_bytes'] for k in
                               ('RECOVERY_INPUT', 'RECOVERY_STATE', 'RECOVERY_TAIL'))
        network_normal = network_extra - network_recovery
        require(physical['business_sent_bytes'] == run['sent_application_bytes'], 'business byte mismatch')
        extra_report['physical_network'] = physical
        final = json.loads((root/'protection-finalization.json').read_text())
        require(final['quiescent'], 'final protection ownership leak')
        pools = rows(root, 'protection-node-storage-summary.csv')
        require(all(int(p['final_used_bytes']) == int(p['final_reserved_bytes']) == 0 for p in pools), 'storage leak')
        if scheme != 'compfrr': require(all(int(p['peak_total_bytes']) == 0 for p in pools), 'RS/RP allocated checkpoint storage')
    service = sum(e[k] for e in executed for k in
                  ('primary_actual_service_ns', 'recovery_actual_service_ns', 'replica_actual_service_ns'))
    require(service == sum(int(n['busy_time_ns']) for n in rows(root, 'compute-node-summary.csv')), 'physical compute conservation')
    flow = rows(root, 'network-flow-metrics.csv')[0]
    all_bytes = int(flow['tx_bytes']) - 28*int(flow['tx_packets'])
    require(all_bytes == run['sent_application_bytes'] + network_extra, 'physical UDP payload conservation')
    capacity = json.loads((root/'capacity-aware-summary.json').read_text())
    require(all(v == 0 for k, v in capacity.items() if k.endswith('_at_end')), 'network reservation leak')
    catches = []
    for r in recoveries:
        value = r.get('actual_T_catch_ns')
        catches.append(dict(task_id=r['task_id'], fault_time_ns=int(r['fault_time_ns']),
                            source=recovery_name, catch_ns=int(value) if value else None))
    # Recoveries have direct runtime milestones. RP uses exact, uninterrupted,
    # actual service ledgers; never mistake missing milestones for 0 ms.
    for key, b in replicas.items():
        direct = [i for i in impacts if i['task_id'] == key and i['impact_type'] == 'PRIMARY_INTERRUPTED'
                  and i['progress_valid'] == '1' and i['task_state_before_fault'] == 'RUNNING']
        require(len(direct) <= 1, 'multiple primary replica faults')
        if not direct: continue
        i = direct[0]
        a = next((a for a in attempts if a['task_id'] == key and a['role'] == 'replica'), None)
        catches.append(dict(task_id=key, fault_time_ns=int(i['fault_time_ns']), source='replica actual continuous service',
            catch_ns=replica_catch_ns(int(i['fault_time_ns']), int(i['completed_work_units_at_fault']), a)))
    require(len({c['task_id'] for c in catches}) == len(catches), 'RS/RP catch double count')
    if scheme == 'multitree':
        decisions = {r['task_id']: r['decision'] for r in rows(root, 'multitree-decisions.csv')}
        require(all(decisions.get(k) == 'RP' for k in replicas) and
                all(decisions.get(k) == 'RS' for k in rs), 'mixed RS/RP or hidden fallback')
        require({k for k, v in decisions.items() if v == 'RP'} == set(replicas), 'RP request omitted/retried')
        extra_report['multitree'] = json.loads((root/'multitree-summary.json').read_text())
        if identity['stage'] in ('multitree-comparison-run11', 'multitree-comparison'):
            mapping = runpy.run_path(str(ROOT/'contrib/satcompute/tests/integration/regression/audit-multitree-mapping.py'))
            extra_report['mapping'] = mapping['audit'](root)
    faults = json.loads((root/'fault-trace.json').read_text())['faults']
    physical_faults = {(f['node_id'], f['start_time_ns']): f for f in faults}
    require(len(physical_faults) == len(faults), 'ambiguous physical fault identity')
    for c in catches:
        node = int(tasks[c['task_id']]['compute_node_id'])
        f = physical_faults[node, c['fault_time_ns']]
        c['fault_signature'] = [node, c['fault_time_ns'], f['fault_type'], f['f1_occurred'], f['f2_occurred']]
    links = rows(root, 'link-summary.csv')
    totals = {k: sum(e[k] for e in executed) for k in ('normal_protection_eq_wu', 'reserved_idle_eq_wu',
        'task_execution_waste_wu', 'w_waste_actual', 'total_executed_wu', 'useful_work_wu')}
    summary = dict(label=identity['label'], tasks=len(ts), completed=sum(t['task_success'] == '1' for t in ts),
        failed_task_ids=[int(t['task_id']) for t in ts if t['task_success'] != '1'],
        failure_reasons=dict(Counter(t['failure_reason'] for t in ts if t['task_success'] != '1')),
        extra_sent_bytes=network_extra, proactive_lifetime_sent_bytes=network_normal,
        recovery_sent_bytes=network_extra-network_normal, total_network_sent_bytes=all_bytes,
        fault_to_catch_ms=distribution([c['catch_ns']/1e6 for c in catches if c['catch_ns'] is not None]),
        primary_fault_catch_cohort=len(catches), no_observed_catch=sum(c['catch_ns'] is None for c in catches),
        paths=dict(Counter(r['chosen_path'] or 'REJECTED' for r in recoveries)),
        replica_requests=len(replicas), replica_admitted=sum(r['replica_admitted'] == '1' for r in replicas.values()),
        replica_takeovers=sum(bool(r['takeover_time_ns']) for r in replicas.values()),
        faults=dict(F1=sum(bool(f['f1_occurred']) for f in faults), F2=sum(bool(f['f2_occurred']) for f in faults),
                    F3=sum(f['fault_type'] == 'satellite' for f in faults)),
        mean_link_utilization_percent=sum(float(r['utilization_percent']) for r in links)/len(links),
        max_link_whole_run_utilization_percent=max(float(r['utilization_percent']) for r in links),
        status='PASS', **totals)
    return dict(summary=summary, catches=catches, execution=identity, per_task_execution=executed, **extra_report)


def comparison(root, groups):
    results = {g: audit(root/g) for g in groups}
    commits = {v['execution']['commit'] for v in results.values()}
    require(len(commits) == 1, 'comparison mixes execution commits')
    config = runpy.run_path(str(Path(__file__).with_name('config_arguments.py')))
    omitted = config['NEW'] | config['SHARED'] | {'outputDir', 'faultTrace'}
    common_scene = None
    for r in results.values():
        flags = dict(t.removeprefix('--').split('=', 1) for t in shlex.split(r['execution']['command'][-1])[1:])
        scene = {k: v for k, v in flags.items() if k not in omitted}
        if common_scene is None: common_scene = scene
        require(scene == common_scene, 'comparison mixes non-protection runtime inputs')
    paired = {}
    for other in groups:
        if other == 'compfrr-p': continue
        a, b = results[other], results['compfrr-p']
        fa = {c['task_id']: c for c in a['catches']}
        fb = {c['task_id']: c for c in b['catches']}
        common = [key for key in fa.keys() & fb.keys() if same_primary_fault(fa[key], fb[key])]
        valid = [key for key in common if fa[key]['catch_ns'] is not None and fb[key]['catch_ns'] is not None]
        paired[other] = dict(same_physical_primary_faults=len(common), both_caught=len(valid),
            reference_mean_ms=sum(fa[k]['catch_ns']/1e6 for k in valid)/len(valid) if valid else None,
            compfrr_p_mean_ms=sum(fb[k]['catch_ns']/1e6 for k in valid)/len(valid) if valid else None,
            task_ids=sorted(map(int, valid)))
    return dict(status='PASS', commit=next(iter(commits)), common_runtime_inputs=common_scene,
        groups=results, paired_vs_compfrr_p=paired,
        scope=f"Same input/model/seed{common_scene['randomSeed']}/run{common_scene['randomRun']}, "
              'online generate; realized faults are endogenous, not forced replay.',
        units='Bytes are actual application payload, counted once per physical flow, decimal GB. Resource totals are eq-WU.',
        waste='Executed primary+recovery+replica WU minus W only for logical success; plus normal cost and actual reserved-idle eq-WU.',
        catch='Primary RUNNING faults only. Actual recovery milestone or surviving RP actual continuous service reaches W_f. Missing is not zero.',
        lifecycle='Proactive/replica bytes cover whole physical lifetimes including post-fault suffix; recovery bytes exclude business RESULT.')


def markdown(report):
    scene = report['common_runtime_inputs']
    lines = [f"# 六组 seed{scene['randomSeed']}/run{scene['randomRun']} 实际执行对比", '',
        '| 方案 | 完成 | 额外流量 GB | catch 均值 / P90 ms | 已 catch / 故障样本 | 执行浪费 M WU | 常态 M eq-WU | 空等 M eq-WU | 总浪费 M eq-WU |',
        '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    def num(value): return '—' if value is None else f'{value:.3f}'
    for value in report['groups'].values():
        s = value['summary']; c = s['fault_to_catch_ms']
        lines.append(f"| {s['label']} | {s['completed']}/{s['tasks']} | {s['extra_sent_bytes']/1e9:.6f} | "
            f"{num(c['mean'])} / {num(c['p90'])} | {c['count']}/{s['primary_fault_catch_cohort']} | "
            f"{s['task_execution_waste_wu']/1e6:.6f} | {s['normal_protection_eq_wu']/1e6:.6f} | "
            f"{s['reserved_idle_eq_wu']/1e6:.6f} | {s['w_waste_actual']/1e6:.6f} |")
    lines += ['', 'catch 从主任务故障到幸存/恢复任务实际追平故障前 WU；缺失不填零。1+1 接管不等于已追平。',
        '总浪费 = 实际任务执行浪费 + 常态保护等效成本 + 实际预留空等等效成本；最后一项不是已执行 CPU。',
        '流量统计整个实际 flow 生命周期的额外应用载荷，不是按跳累加。各组 online generate 负载不同，实际故障不强制相同。',
        '本表为单轮结果；配对共同故障、逐任务账本与完整单位说明见 comparison.json。', '']
    return '\n'.join(lines)
