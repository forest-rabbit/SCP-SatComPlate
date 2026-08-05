# SatCompute ns-3.33 到 ns-3.48 迁移矩阵

状态：阶段 0、阶段 1 和阶段 2 已完成；阶段 3 的拓扑 facade 与共享核心审计
完成，阶段检查点 CI 将在本次收口合并后从 `main` 唯一触发。每完成一个阶段
更新“结果”列，最终逐文件审计。

动作定义：

- **恢复**：以 legacy 路径、职责和公共接口为准迁入；
- **适配**：保留 legacy 外观，内部使用 ns-3.48 实现；
- **保留**：current 新增能力在职责明确的位置继续存在；
- **替代**：由 v0.3 指定的新合同取代；
- **删除**：最终主线不保留。

## 根目录与构建

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `README.md`（中文） | 已恢复中文 v0.3 基线 | 恢复 legacy 主体并更新 ns-3.48 | 已恢复；阶段 7 最终校对 |
| `para.h/.cc` | 已恢复并接管生产入口 | 恢复并扩展 | 已恢复；typed defaults、CLI、组合校验和精确时间转换均有独立测试 |
| `satcompute.cc` | 已移回模块根目录 | 移回根目录并切换 para | 已恢复并只使用 `para.cc + CLI` |
| `wscript` | `CMakeLists.txt` | 由 CMake 替代 | 已确定 |
| 无 | `model/satcompute-version.*` | 按实际职责保留或并入运行摘要 | 待审计 |
| 无 | `model/sha256.*` | 移入 manifest/effective-config 所属职责 | 待审计 |
| 无 | `config/scenario*` | 完整 scenario 输入切换后删除 | 已删除 |

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
| `routing/routing-policy-factory.*` | 已恢复，next-hop/path 公共调用均经过工厂 | 保留 legacy 接口并扩展完整路径策略入口 | 已迁移，待阶段 5 回归 |

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
| `traffic/network-transfer.*` | 已恢复并由平台入口调用 | 保留秒兼容入口，内部统一纳秒 | 已迁移，待阶段 5 回归 |
| `traffic/network-transfer-application.*` | 同路径 | 保留并审计 ns-3.48 socket 行为 | 待审计 |
| `traffic/network-transfer-config.*` | 同路径 | 保留数据合同 | 待审计 |
| `traffic/network-transfer-engine.*` | 同路径 | 保留 current 适配 | 待审计 |
| `traffic/network-transfer-receiver.*` | 同路径 | 保留 current 适配 | 待审计 |
| 无 | `traffic/network-transfer-records.h` | 保留内部记录类型 | 已确定 |
| 无 | `traffic/transfer-trace.schema.json` | 保留为独立数据合同 | 已确定 |

## topology

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `topology/satellite-topology.*` | 已恢复 facade | 恢复 facade，内部委托 current | 已完成；平台入口只使用 facade，内部选择 online/replay controller |
| `topology/snapshot/*` | 同路径并扩展 | 保留 legacy replay + manifest 扩展 | 已审计；全量快照、0.2/0.3 manifest、哈希和 cadence 降采样均有测试 |
| `topology/link/satellite-link-state.*` | 同路径并适配 | 保留路径与设备/队列合同 | 已审计；固定 candidate 设备、接口身份、属性更新和启停恢复均有测试 |
| 无 | `topology/orbit/*` | 保留 ns-3.48 原生轨道核心 | 已完成；online/export 共用唯一圆轨道实现 |
| 无 | `topology/online/*` | 保留为 facade 内部 online 实现 | 已完成；固定候选、距离门控、两种时延和边变化路由策略已验证 |
| 无 | `topology/replay/*` | 保留为 facade 内部 replay 实现 | 已完成；按 network cadence 消费全量切片并委托统一运行时接口 |
| 无 | `topology/export/*` | 保留 XYZ/链路/manifest 导出 | 已完成；独立 cadence、ECEF XYZ、有效链路和哈希清单已验证 |
| 无 | `topology/ipv4/*` | 保留为 facade 内部地址实现 | 已审计；稳定 ID canonical service `/32` 与 ISL `/30` 已验证 |
| 无 | `satellite-*view.h`、`satellite-id-map.*` | 保留稳定 ID 和只读状态接口 | 已完成；节点下标与外部卫星 ID 查询已显式区分 |

## input

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `input/topology/examples/xw-66sat*` | 已恢复 | 恢复 | 已完成；原 66 星/132 ISL blob、12 切片与真实 replay 已验证 |
| `input/topology/resources/workload/*` | 已恢复 | 恢复算力输入 | 已完成；22/66 节点 ComputeProfile 均通过当前解析器 |
| `input/traffic/workload/*` | 已恢复 | 恢复流量输入 | 已完成；10/5000 条 NetworkTransfer 均通过当前解析器 |
| 无 | `input/topology/constellations/*` | 新增精简星座结构合同 | 已完成；closed-world schema、读取器和 66 星默认输入已验证 |
| 无 | `input/examples/synthetic-66-fixed.json` | 删除完整配置 | 已删除 |
| 无 | `input/examples/synthetic-66-distance.json` | 删除完整配置 | 已删除 |

## tools

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `tools/generation/generate-task-workload.py` | 已恢复 | 恢复并适配路径 | 已完成；只读取节点/算力并输出确定性 TaskTrace + summary |
| `tools/generation/generate-transfer-workload.py` | 已恢复 | 恢复并适配路径 | 已完成；只读取稳定卫星 ID 并输出 NetworkTransfer |
| `tools/generation/scenario/*` | 已重建独立 input bundle 组合器 | 恢复非轨道场景生成边界 | 已完成；不生成完整运行配置，fault 槽位暂为 null |
| `tools/generation/topology/common/*` | 已重建只读 manifest checker | 恢复原子输出/schema/checker 工具 | 已完成；原子写入/schema 复用 C++ exporter，Python 只验清单与哈希 |
| `tools/generation/topology/static/*` | 由同一 C++ 工具的单切片参数覆盖 | 按当前合同审计后恢复 | 已替代；不保留第二套静态拓扑算法 |
| `tools/generation/topology/dynamic/*` | 已由共享 C++ executable 替代 | 由共享 C++ 轨道 exporter 替代传播部分 | 已完成；与平台 export-only 逐字节等价 |
| `tools/generation/topology/orbit/hypatia/*` | 缺失 | 不迁移 | 已确定删除 |
| `tools/analysis/topology_interval/*` | v0.3 trace 降采样、边状态与 ECMP 分析已恢复 | 恢复适用分析；C++ route gate 随阶段 5 接回 | Python 核心已迁移；旧 scenario 编排器和冻结报告不迁移 |
| `tools/validation/*` | 已恢复 legacy 检查器 | 恢复并对齐 metrics 输出 | 入口和 CLI 已恢复；阶段 6 随分层 metrics 接回黄金输出 |
| `tools/visualization/orbit/*` | 已恢复为 v0.3 trace/XYZ 消费者 | 恢复为切片消费者，不计算轨道 | 已完成；旧 Hypatia PNG 不迁移，显示间隔采用上一切片 |

## tests

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `tests/fixtures/topology/snapshots/*` | 大量缺失或改名 | 恢复黄金 fixtures | 待迁移 |
| `tests/fixtures/topology/compute-profiles/*` | 部分改名/缺失 | 恢复 | 待迁移 |
| `tests/fixtures/traffic/transfers/*` | 部分改名/缺失 | 恢复 | 待迁移 |
| `tests/fixtures/traffic/tasks/*` | 部分改名/缺失 | 恢复 | 待迁移 |
| `tests/support/*` | 缺失 | 恢复 | 待迁移 |
| legacy Python generation/analysis/visualization tests | 适用于 v0.3 的核心测试已恢复 | 随 tools 恢复 | 已完成阶段 4 范围；61 个项目 Python 测试通过 |
| legacy smoke/regression scripts | 被两个 `run-all.sh` 替代 | 恢复分层脚本并保留统一入口 | 待迁移 |
| current C++ unit executables | 新增 | 保留并按最终接口适配 | 待审计 |

## 文档与 CI

| 项目 | current main | 最终动作 | 结果 |
|---|---|---|---|
| `AGENTS.md` | v0.3 para/constellation 规则 | 改为 v0.3 para/constellation 边界 | 已迁移 |
| `docs/specs/platform-v0.2.md` | 已标记为 v0.2 历史规格 | 标记为已废止 | 已迁移 |
| `docs/plans/ns3-48-migration.md` | 已标记为 v0.2 历史计划 | 标记为已废止 | 已迁移 |
| `docs/ns3-48-migration-status.md` | 已标记为 v0.2 历史状态 | 标记为 v0.2 历史状态 | 已迁移 |
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
