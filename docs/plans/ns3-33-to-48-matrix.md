# SatCompute ns-3.33 到 ns-3.48 迁移矩阵

状态：v0.3 初始基线。每完成一个阶段更新“结果”列，最终逐文件审计。

动作定义：

- **恢复**：以 legacy 路径、职责和公共接口为准迁入；
- **适配**：保留 legacy 外观，内部使用 ns-3.48 实现；
- **保留**：current 新增能力在职责明确的位置继续存在；
- **替代**：由 v0.3 指定的新合同取代；
- **删除**：最终主线不保留。

## 根目录与构建

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `README.md`（中文） | `README.md`（英文摘要） | 恢复 legacy 主体并更新 ns-3.48 | 待迁移 |
| `para.h/.cc` | 无；由 `model/scenario-config.*` 替代 | 恢复并扩展 | 待迁移 |
| `satcompute.cc` | `app/satcompute.cc` | 移回根目录并切换 para | 待迁移 |
| `wscript` | `CMakeLists.txt` | 由 CMake 替代 | 已确定 |
| 无 | `model/satcompute-version.*` | 按实际职责保留或并入运行摘要 | 待审计 |
| 无 | `model/sha256.*` | 移入 manifest/effective-config 所属职责 | 待审计 |
| 无 | `config/scenario*` | 完整 scenario 输入切换后删除 | 待迁移 |

## metrics

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `metrics/metrics.*` | `metrics/run-output-writer.*` | 恢复 MetricsRecorder 编排 | 待迁移 |
| `metrics/core/flow-metrics.*` | 无等价分层文件 | 恢复 FlowMonitor 指标 | 待迁移 |
| `metrics/core/run-summary.*` | writer 内部逻辑 | 恢复独立文件 | 待迁移 |
| `metrics/core/task-metrics.*` | writer 内部逻辑 | 恢复独立文件 | 待迁移 |
| `metrics/core/transfer-metrics.*` | writer 内部逻辑 | 恢复独立文件 | 待迁移 |
| `metrics/routing/ecmp-route-recorder.*` | 无等价独立文件 | 恢复 | 待迁移 |
| `metrics/routing/ecmp-metrics.*` | writer 内部逻辑 | 恢复 | 待迁移 |
| `metrics/routing/size-aware-metrics.*` | writer 内部逻辑 | 恢复 | 待迁移 |
| `metrics/routing/capacity-aware-metrics.*` | writer 内部逻辑 | 恢复 | 待迁移 |
| `metrics/diagnostics/failure-diagnostics.*` | writer 中的部分诊断 | 恢复并适配当前记录类型 | 待迁移 |
| `metrics/diagnostics/flow-drop-reason-diagnostics.*` | 无完整等价 | 恢复 | 待迁移 |
| 无 | `metrics/run-output-writer.*` | 分层迁移完成后删除 | 待迁移 |

## routing

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `routing/algorithm/*` | 同路径，已适配 ns-3.48 | 保留路径并逐文件行为审计 | 待审计 |
| `routing/common/*` | 同路径，已适配 ns-3.48 | 保留 legacy hash/候选合同 | 待审计 |
| `routing/ns3/*` | 同路径，已适配 ns-3.48 | 保留 current API 适配 | 待审计 |
| `routing/state/*` | 同路径，已适配 ns-3.48 | 保留并验证确定性 | 待审计 |
| `routing/routing-policy-factory.*` | 被删除 | 恢复 legacy 工厂边界 | 待迁移 |

## task

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `task/compute-profile.*` | 同路径 | 保留路径并验证 JSON 合同 | 待审计 |
| `task/compute-service.*` | 同路径 | 保留并验证 FCFS | 待审计 |
| `task/compute-task.*` | 同路径 | 保留状态机语义 | 待审计 |
| `task/task-coordinator.*` | 同路径 | 保留任务闭环 | 待审计 |
| `task/task-trace.*` | 同路径 | 保留 canonical 输入 | 待审计 |
| 无 | `task/*.schema.json` | 保留为数据合同 | 已确定 |

## traffic

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `traffic/network-transfer.*` | 被删除 | 恢复 legacy wrapper | 待迁移 |
| `traffic/network-transfer-application.*` | 同路径 | 保留并审计 ns-3.48 socket 行为 | 待审计 |
| `traffic/network-transfer-config.*` | 同路径 | 保留数据合同 | 待审计 |
| `traffic/network-transfer-engine.*` | 同路径 | 保留 current 适配 | 待审计 |
| `traffic/network-transfer-receiver.*` | 同路径 | 保留 current 适配 | 待审计 |
| 无 | `traffic/network-transfer-records.h` | 保留内部记录类型 | 已确定 |
| 无 | `traffic/transfer-trace.schema.json` | 保留为独立数据合同 | 已确定 |

## topology

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `topology/satellite-topology.*` | 被多个 controller 取代 | 恢复 facade，内部委托 current | 待迁移 |
| `topology/snapshot/*` | 同路径并扩展 | 保留 legacy replay + manifest 扩展 | 待审计 |
| `topology/link/satellite-link-state.*` | 同路径并适配 | 保留路径与设备/队列合同 | 待审计 |
| 无 | `topology/orbit/*` | 保留 ns-3.48 原生轨道核心 | 已确定 |
| 无 | `topology/online/*` | 保留为 facade 内部 online 实现 | 已确定 |
| 无 | `topology/replay/*` | 保留为 facade 内部 replay 实现 | 已确定 |
| 无 | `topology/export/*` | 保留 XYZ/链路/manifest 导出 | 已确定 |
| 无 | `topology/ipv4/*` | 保留为 facade 内部地址实现 | 待审计 |
| 无 | `satellite-*view.h`、`satellite-id-map.*` | 保留稳定 ID 和只读状态接口 | 已确定 |

## input

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `input/topology/examples/xw-66sat*` | 缺失 | 恢复 | 待迁移 |
| `input/topology/resources/workload/*` | 缺失 | 恢复算力输入 | 待迁移 |
| `input/traffic/workload/*` | 缺失 | 恢复流量输入 | 待迁移 |
| 无 | `input/topology/constellations/*` | 新增精简星座结构合同 | 待迁移 |
| 无 | `input/examples/synthetic-66-fixed.json` | 删除完整配置 | 待迁移 |
| 无 | `input/examples/synthetic-66-distance.json` | 删除完整配置 | 待迁移 |

## tools

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `tools/generation/generate-task-workload.py` | 缺失 | 恢复并适配路径 | 待迁移 |
| `tools/generation/generate-transfer-workload.py` | 缺失 | 恢复并适配路径 | 待迁移 |
| `tools/generation/scenario/*` | 缺失 | 恢复非轨道场景生成边界 | 待迁移 |
| `tools/generation/topology/common/*` | 缺失 | 恢复原子输出/schema/checker 工具 | 待迁移 |
| `tools/generation/topology/static/*` | 缺失 | 按当前合同审计后恢复 | 待迁移 |
| `tools/generation/topology/dynamic/*` | 缺失 | 由共享 C++ 轨道 exporter 替代传播部分 | 待迁移 |
| `tools/generation/topology/orbit/hypatia/*` | 缺失 | 不迁移 | 已确定删除 |
| `tools/analysis/topology_interval/*` | 缺失 | 恢复并适配 CMake tool targets | 待迁移 |
| `tools/validation/*` | 缺失 | 恢复并对齐 metrics 输出 | 待迁移 |
| `tools/visualization/orbit/*` | 缺失 | 恢复为切片消费者，不计算轨道 | 待迁移 |

## tests

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `tests/fixtures/topology/snapshots/*` | 大量缺失或改名 | 恢复黄金 fixtures | 待迁移 |
| `tests/fixtures/topology/compute-profiles/*` | 部分改名/缺失 | 恢复 | 待迁移 |
| `tests/fixtures/traffic/transfers/*` | 部分改名/缺失 | 恢复 | 待迁移 |
| `tests/fixtures/traffic/tasks/*` | 部分改名/缺失 | 恢复 | 待迁移 |
| `tests/support/*` | 缺失 | 恢复 | 待迁移 |
| legacy Python generation/analysis/visualization tests | 缺失 | 随 tools 恢复 | 待迁移 |
| legacy smoke/regression scripts | 被两个 `run-all.sh` 替代 | 恢复分层脚本并保留统一入口 | 待迁移 |
| current C++ unit executables | 新增 | 保留并按最终接口适配 | 待审计 |

## 文档与 CI

| 项目 | current main | 最终动作 | 结果 |
|---|---|---|---|
| `AGENTS.md` | v0.2 scenario 规则 | 改为 v0.3 para/constellation 边界 | 待迁移 |
| `docs/specs/platform-v0.2.md` | 声明已完成 | 标记为已废止 | 待迁移 |
| `docs/plans/ns3-48-migration.md` | 声明已完成 | 标记为已废止 | 待迁移 |
| `docs/ns3-48-migration-status.md` | 声明迁移完成 | 标记为 v0.2 历史状态 | 待迁移 |
| `.github/workflows/per_commit.yml` | push/PR 自动运行 | 重命名为 `phase_gate.yml`，仅 `workflow_dispatch` | 已迁移 |

## 最终逐文件审计

阶段 7 使用以下命令导出两条树，并将所有未在本矩阵中覆盖的文件补充为逐文件
记录：

```bash
git ls-tree -r --name-only legacy/ns-3.33 -- contrib/satcompute
git ls-tree -r --name-only main -- contrib/satcompute
```

最终不允许存在“无结论”的 legacy 文件，也不允许以目录重构为理由删除可观察
行为。
