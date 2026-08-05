# ns-3.33 → ns-3.48 指标迁移映射

本表是阶段 6 的输出合同。目标不是只保留相似统计值，而是恢复 legacy 的文件名、
目录、表头和字段语义；ns-3.48 新增的可复现证据采用加法兼容，不覆盖旧字段。

| 输出 | 分层所有者 | ns-3.48 迁移动作 | 状态 |
|---|---|---|---|
| `network-flow-metrics.csv` | `core/flow-metrics` | 使用真实 IPv4 FlowMonitor 恢复聚合网络指标 | 已接入 |
| `network-flow-details.csv` | `core/flow-metrics` | 用 UDP 五元组关联 transfer 与 FlowId | 已接入 |
| `transfer-summary.csv` | `core/transfer-metrics` | 恢复 legacy 19 列；内部状态不混入稳定 CSV | 已接入 |
| `task-events.csv` | `core/task-metrics` | 恢复任务状态时间线 | 已接入 |
| `task-summary.csv` | `core/task-metrics` | 恢复输入、排队、计算、结果传输时延 | 已接入 |
| `compute-node-summary.csv` | `core/task-metrics` | 用 resolved 纳秒时长计算利用率 | 已接入 |
| `run-summary.json` | `core/run-summary` | 恢复 legacy 扁平字段，同时保留下述新增证据 | 已接入 |
| `ecmp-route-events.csv` | `routing/ecmp-metrics` | 恢复逐流选路事件 | 已接入 |
| `size-aware-reservation-events.csv` | `routing/size-aware-metrics` | 恢复 reservation 生命周期 | 已接入 |
| `size-aware-summary.json` | `routing/size-aware-metrics` | 恢复 size-aware 聚合 | 已接入 |
| `capacity-aware-summary.json` | `routing/capacity-aware-metrics` | 恢复完整路径容量聚合 | 已接入 |
| `diagnostics/failure/incomplete-tasks.csv` | `diagnostics/failure-diagnostics` | 恢复未完成任务对象 | 已接入 |
| `diagnostics/failure/incomplete-transfers.csv` | `diagnostics/failure-diagnostics` | 恢复未完成传输对象 | 已接入 |
| `diagnostics/failure/isl-queue-drops.csv` | `diagnostics/failure-diagnostics` | 恢复逐次 ISL 队列丢包 | 已接入 |
| `diagnostics/failure/isl-queue-drop-summary.csv` | `diagnostics/failure-diagnostics` | 恢复逐有向链路丢包聚合 | 已接入 |
| `diagnostics/failure/udp-socket-drops.csv` | `diagnostics/failure-diagnostics` | 恢复接收 socket 丢包事件 | 已接入 |
| `diagnostics/failure/udp-socket-drop-summary.csv` | `diagnostics/failure-diagnostics` | 恢复逐接收端丢包聚合 | 已接入 |
| `diagnostics/failure/flow-link-concentration.csv` | `diagnostics/failure-diagnostics` | 恢复流量与拥塞链路关联 | 已接入 |
| `diagnostics/failure/flow-drop-reasons.csv` | `diagnostics/flow-drop-reason-diagnostics` | 恢复 FlowMonitor drop reason 明细 | 已接入 |
| `diagnostics/failure/diagnostic-summary.json` | `diagnostics/failure-diagnostics` | 恢复失败诊断汇总 | 已接入 |

`run-summary.json` 必须同时保留以下 ns-3.48 新增证据：`run_name`、
`config_schema_version`、`effective_config.path`、`effective_config.sha256`、
`simulation_duration_ns`、`wall_clock_ns`、`topology_source`、
`applied_topology_slice_count` 和 `route_computation_count`。`effective-config.json`、
轨道坐标切片及其 manifest 继续由各自模块负责，不归 metrics 重复生成。

任务 6.4 已删除 `run-output-writer.*`，改由 `MetricsRecorder` 统一编排。
过渡期的 `routing-summary.json` 和 `routing-reservation-events.csv` 不保留：前者字段
已由 `run-summary.json`、`size-aware-summary.json` 与
`capacity-aware-summary.json` 覆盖，后者与
`size-aware-reservation-events.csv` 的事件字段等价。复用输出目录时仍会精确清理
这两个旧文件，避免把历史结果误认为本次证据。
