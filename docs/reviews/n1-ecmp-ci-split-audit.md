# N1 ECMP CI 分级审计

## 1. 目标与基线

```text
Base main: 6d95056f81154bfdcfb0d872e527371e5e422d80
Branch: ci/split-satcompute-regression
Previous workflow: .github/workflows/satcompute-smoke.yml
Policy: preserve every test contract and change only its execution frequency
```

Pull request 运行 `SatCompute Fast Smoke`。`main` push 与
`workflow_dispatch` 运行 `SatCompute Full Regression`；Full 依次调用全部
Fast 脚本和两个扩展脚本。

## 2. 原工作流映射

| 原步骤 | 新脚本 | 频率 |
| --- | --- | --- |
| Topology-only default output | `run-routing-smoke.sh` | Fast + Full |
| Static diamond repeat | `run-routing-smoke.sh` | Fast + Full |
| Dynamic diamond | `run-routing-smoke.sh` | Fast + Full |
| Stable HRW dynamic epochs | `run-routing-smoke.sh` | Fast + Full |
| Size-aware HRW lifecycle | `run-routing-smoke.sh` | Fast + Full |
| Canonical endpoint and remainder | `run-full-routing-regression.sh` | Full |
| Varied 5000-transfer workload | `run-full-routing-regression.sh` | Full |
| Mixed large workload | `run-full-routing-regression.sh` | Full |
| FlowMonitor DropReason | `run-diagnostics-smoke.sh` | Fast + Full |
| Task single ECMP | `run-task-smoke.sh` | Fast + Full |
| Task FCFS | `run-task-smoke.sh` | Fast + Full |
| Task completion policy | `run-diagnostics-smoke.sh` | Fast + Full |
| Task heterogeneous | `run-full-workload-regression.sh` | Full |
| Task order determinism | `run-full-workload-regression.sh` | Full |
| Compute profile order determinism | `run-full-workload-regression.sh` | Full |
| Check deterministic outputs | 两个 routing 脚本内就近执行 | Fast 核心 + Full 扩展 |
| Check task outputs | 两个 task/workload 脚本内就近执行 | Fast 核心 + Full 扩展 |
| Generated task workload contract | `run-full-workload-regression.sh` | Full |
| Generated task workload preflight | `run-full-workload-regression.sh` | Full |
| Generated task end-to-end | `run-task-smoke.sh` 与 `run-full-workload-regression.sh` | Fast 使用已提交 fixture；Full 再验证本轮生成文件 |

`check-task-output.py smoke` 只暴露已有 `validate_scenario`、
`validate_single` 和 `validate_fcfs` 合同，未更改验证语义。默认完整模式及
`failure`、`stress`、`run` 模式保持不变。

## 3. 本地验证

五个脚本均以 `set -euo pipefail` 运行并为每组场景打印明确名称。它们已在同一
clean build 上按 Full 顺序直接执行：

```text
run-routing-smoke.sh             PASS
run-task-smoke.sh                PASS
run-diagnostics-smoke.sh         PASS
run-full-routing-regression.sh   PASS
run-full-workload-regression.sh  PASS
```

关键规模结果保持为：

```text
5000-transfer: 5000/5000, 53100 packets, lost=0
mixed-large:   12/12, 41507 packets, lost=0
generated:     40/40 tasks, 80/80 transfers, lost=0
```

远程验证：

```text
PR Fast:
  https://github.com/forest-rabbit/SatCompute/actions/runs/30353113556
  status=completed, conclusion=success

main push Full:
  https://github.com/forest-rabbit/SatCompute/actions/runs/30353450349
  status=completed, conclusion=success
```

Full 还以 `satcompute/full-regression` commit status 发布精确 run URL，供不返回
push/workflow_dispatch run 的审查连接器读取。最终收尾的自动 Full 与
`workflow_dispatch` Full 证据记录在收尾 PR 和 `n1-ecmp-complete` annotated
tag 中。

## 4. 仓库级门禁

`fast-smoke` 是本项目规定的 PR 合并门禁。本次收尾分别读取 GitHub branch
protection 与 repository ruleset API；当前私有仓库的两个接口都返回
`HTTP 403: Upgrade to GitHub Pro or make this repository public to enable this
feature`。因此当前只能在 PR 流程中先验证再合并，不能声称已配置仓库级 required
check。仓库升级套餐或改为公开后，应把 `fast-smoke` 配置为 `main` 的 required
status check；这不会要求修改 SatCompute 代码或测试脚本。
