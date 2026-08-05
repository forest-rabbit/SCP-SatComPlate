# SatCompute ns-3.33 到 ns-3.48 迁移矩阵

状态：阶段 0 至阶段 6 已完成；阶段 6 唯一一次 GitHub CI run `30983721371`
已通过。阶段 7 的测试层次、中文 README 和逐文件审计均已实施，待最终本地门禁和
阶段 7 唯一一次 GitHub CI 后关闭 v0.3。

动作定义：

- **恢复**：以 legacy 路径、职责和公共接口为准迁入；
- **适配**：保留 legacy 外观，内部使用 ns-3.48 实现；
- **保留**：current 新增能力在职责明确的位置继续存在；
- **替代**：由 v0.3 指定的新合同取代；
- **删除**：最终主线不保留。

## 根目录与构建

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `README.md`（中文） | 已恢复中文 v0.3 文档 | 恢复 legacy 主体并更新 ns-3.48 | 已完成；legacy 章节主线、当前命令、输入边界和延期范围均已校对 |
| `para.h/.cc` | 已恢复并接管生产入口 | 恢复并扩展 | 已恢复；typed defaults、CLI、组合校验和精确时间转换均有独立测试 |
| `satcompute.cc` | 已移回模块根目录 | 移回根目录并切换 para | 已恢复并只使用 `para.cc + CLI` |
| `wscript` | `CMakeLists.txt` | 由 CMake 替代 | 已确定 |
| 无 | 根目录 `satcompute-version.*` | 作为跨组件 provenance 证据保留 | 已完成；移出通用 `model/` 并由 effective config 与 metrics 共用 |
| 无 | 根目录 `sha256.*` | 作为输入与 manifest 哈希工具保留 | 已完成；移出通用 `model/` 并由配置、切片与指标所属组件共用 |
| 无 | `config/scenario*` | 完整 scenario 输入切换后删除 | 已删除 |

## metrics

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `metrics/metrics.*` | 已恢复并由平台直接调用 `MetricsRecorder` | 恢复 MetricsRecorder 编排 | 已迁移；统一编排 core、routing、diagnostics 与 ns-3.48 provenance |
| `metrics/core/flow-metrics.*` | 已恢复并接入真实 FlowMonitor | 恢复 FlowMonitor 指标 | 已迁移；聚合与逐流五元组输出通过门禁 |
| `metrics/core/run-summary.*` | 已恢复独立实现 | 恢复独立文件 | 已迁移；legacy 扁平字段与 ns-3.48 provenance 加法兼容 |
| `metrics/core/task-metrics.*` | 已恢复独立实现 | 恢复独立文件 | 已迁移；task、compute node 输出使用 resolved 纳秒时长 |
| `metrics/core/transfer-metrics.*` | 已恢复独立实现 | 恢复独立文件 | 已迁移；legacy 19 列表头通过精确检查 |
| `metrics/routing/ecmp-route-recorder.*` | 已恢复并连接所有卫星 routing trace | 恢复 | 已迁移；真实 task 仿真验证逐流事件 |
| `metrics/routing/ecmp-metrics.*` | 已恢复独立实现 | 恢复 | 已迁移；legacy 15 列表头与事件值通过门禁 |
| `metrics/routing/size-aware-metrics.*` | 已恢复独立实现 | 恢复 | 已迁移；reservation 生命周期和结束状态通过门禁 |
| `metrics/routing/capacity-aware-metrics.*` | 已恢复独立实现 | 恢复 | 已迁移；容量账本结束状态通过门禁 |
| `metrics/diagnostics/failure-diagnostics.*` | 已恢复并接入任务失败路径 | 恢复并适配当前记录类型 | 已迁移；九文件目录、旧版表头及 ISL queue Drop 映射通过门禁 |
| `metrics/diagnostics/flow-drop-reason-diagnostics.*` | 已恢复并接入直传/任务失败路径 | 恢复 | 已迁移；FlowMonitor reason 与逐流五元组交叉校验通过 |
| 无 | `metrics/run-output-writer.*` | 分层迁移完成后删除 | 已删除；两个过渡 routing 输出经字段审计后移除 |

## routing

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `routing/algorithm/*` | 同路径，已适配 ns-3.48 | 保留路径并逐文件行为审计 | 已审计；五种模式、HRW 黄金分数与 capacity 完整路径均有 C++ 门禁 |
| `routing/common/*` | 同路径，已适配 ns-3.48 | 保留 legacy hash/候选合同 | 已审计；21/37-byte 编码、canonical candidate 与 seed 行为固定 |
| `routing/ns3/*` | 同路径，已适配 ns-3.48 | 保留 current API 适配 | 已审计；IPv4 adapter、route epoch 和动态候选变化已验证 |
| `routing/state/*` | 同路径，已适配 ns-3.48 | 保留并验证确定性 | 已审计；size/capacity reservation、释放和重准入已验证 |
| `routing/routing-policy-factory.*` | 已恢复，next-hop/path 公共调用均经过工厂 | 保留 legacy 接口并扩展完整路径策略入口 | 已迁移；真实 IPv4/transfer 调用路径与独立工厂测试通过 |

## task

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `task/compute-profile.*` | 同路径 | 保留路径并验证 JSON 合同 | 已审计；current 与 legacy 速率、排序和查找均验证 |
| `task/compute-service.*` | 同路径 | 保留并验证 FCFS | 已审计；同 ns tie-break、无空隙非抢占 FCFS 与向上取整固定 |
| `task/compute-task.*` | 同路径 | 保留状态机语义 | 已审计；六状态、五次转换和时间戳不变量固定 |
| `task/task-coordinator.*` | 同路径 | 保留任务闭环 | 已审计；五种路由、单任务、FCFS、异构和 legacy 黄金闭环通过 |
| `task/task-trace.*` | 同路径 | 保留 canonical 输入 | 已审计；输入数组顺序不影响 task/transfer ID 输出 |
| 无 | `task/*.schema.json` | 保留为数据合同 | 已确定 |

## traffic

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `traffic/network-transfer.*` | 已恢复并由平台入口调用 | 保留秒兼容入口，内部统一纳秒 | 已迁移；summary/verbose/silent 和 collector 调用链通过 |
| `traffic/network-transfer-application.*` | 同路径 | 保留并审计 ns-3.48 socket 行为 | 已审计；首跳/路径瓶颈 pacing 与完成回调由五模式引擎测试覆盖 |
| `traffic/network-transfer-config.*` | 同路径 | 保留数据合同 | 已审计；closed-world JSON、canonical 端口/五元组和三档分包固定 |
| `traffic/network-transfer-engine.*` | 同路径 | 保留 current 适配 | 已审计；声明/任务触发、capacity 等待重准入和资源释放通过 |
| `traffic/network-transfer-receiver.*` | 同路径 | 保留 current 适配 | 已审计；按 transfer 聚合、完成时间和 UDP drop 采集接口通过 |
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
| `tools/analysis/topology_interval/*` | v0.3 trace 降采样、边状态与 ECMP 分析已恢复 | 恢复适用分析；C++ route gate 随阶段 5 接回 | 已完成；Python trace/edge/ECMP 分析与 C++ routing 门禁共同覆盖 |
| `tools/validation/*` | 已恢复 legacy 检查器 | 恢复并对齐 metrics 输出 | 已完成；task failure、DropReason 与 ECMP 检查器直接验证当前分层 metrics 输出 |
| `tools/visualization/orbit/*` | 已恢复为 v0.3 trace/XYZ 消费者 | 恢复为切片消费者，不计算轨道 | 已完成；旧 Hypatia PNG 不迁移，显示间隔采用上一切片 |

## tests

| legacy | current main | 最终动作 | 结果 |
|---|---|---|---|
| `tests/fixtures/topology/snapshots/*` | legacy 文件已按原路径和哈希恢复 | 恢复黄金 fixtures | 已迁移，规则 cadence 由 current 动态 fixture 回归 |
| `tests/fixtures/topology/compute-profiles/*` | legacy 文件已按原路径和哈希恢复 | 恢复 | 已迁移，旧速率黄金值进入 parity test |
| `tests/fixtures/traffic/transfers/*` | legacy 文件已按原路径和哈希恢复 | 保留并运行黄金兼容测试 | 已完成；12-transfer 动态 parity 与五模式 engine 回归通过 |
| `tests/fixtures/traffic/tasks/*` | legacy 文件已按原路径和哈希恢复 | 保留并运行黄金兼容测试 | 已完成；单任务、FCFS、异构和 canonical parity 通过 |
| `tests/support/*` | 已恢复并适配 `ns3` 根标记 | 恢复 | 已迁移 |
| legacy Python generation/analysis/visualization tests | 适用于 v0.3 的测试已恢复 | 随 tools 恢复 | 已完成；interval、renderer、GIF、生成、检查和逐文件审计均纳入统一发现入口 |
| legacy smoke/regression scripts | 已恢复命名分层脚本及两个 `run-all.sh` | 恢复分层脚本并保留统一入口 | 已完成；5 个 smoke、2 个 regression 责任脚本和统一编排入口均可独立执行 |
| current C++ unit executables | 新增 | 保留并按最终接口适配 | 已完成；26 个项目自有 executable 覆盖配置、拓扑、路由、流量、任务和 metrics |

## 文档与 CI

| 项目 | current main | 最终动作 | 结果 |
|---|---|---|---|
| `AGENTS.md` | v0.3 para/constellation 规则 | 改为 v0.3 para/constellation 边界 | 已迁移 |
| `docs/specs/platform-v0.2.md` | 已标记为 v0.2 历史规格 | 标记为已废止 | 已迁移 |
| `docs/plans/ns3-48-migration.md` | 已标记为 v0.2 历史计划 | 标记为已废止 | 已迁移 |
| `docs/ns3-48-migration-status.md` | 已标记为 v0.2 历史状态 | 标记为 v0.2 历史状态 | 已迁移 |
| `.github/workflows/per_commit.yml` | push/PR 自动运行 | 重命名为 `phase_gate.yml`，仅 `workflow_dispatch` | 已迁移 |

## 最终逐文件审计

阶段 7 已使用以下命令导出两条树，并把所有未在本矩阵中原路径保留的 legacy 文件
写入逐文件记录：

```bash
git ls-tree -r --name-only legacy/ns-3.33 -- contrib/satcompute
git ls-tree -r --name-only main -- contrib/satcompute
```

审计基线共有 322 个 legacy 文件：252 个按原路径保留，70 个 legacy-only 文件
全部在 [`ns3-33-to-48-file-audit.tsv`](ns3-33-to-48-file-audit.tsv) 中给出
`replaced/removed` 结论、仍存在的证据路径和理由。最终 current 树共 388 个文件，
其中 136 个 current-only 文件均归入 v0.3 明确职责；通用 `model/` 已移除。
`test_legacy_file_audit.py` 机器检查清单数量、排序、路径消失和替代证据存在性。
因此不存在“无结论”的 legacy 文件，也没有以目录重构为理由删除可观察行为。
