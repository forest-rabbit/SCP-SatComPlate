# 指标与失败诊断

当前指标目录保持 ns-3.33 版的分层：`core/` 负责网络、传输、任务和运行汇总，
`routing/` 负责各路由模式证据，`diagnostics/` 只负责显式启用的失败诊断。
平台输出均来自真实运行时数据源，不生成占位统计。

每次运行至少写出 `run-summary.json`、`network-flow-metrics.csv`、
`network-flow-details.csv` 和 `ecmp-route-events.csv`。存在网络工作负载时增加
`transfer-summary.csv`；任务模式增加 `task-events.csv`、`task-summary.csv` 和
`compute-node-summary.csv`。size-aware 与 capacity-aware 模式分别增加其
legacy 路由指标。

`run-summary.json` 保留 ns-3.33 扁平字段，同时记录 resolved 配置哈希、拓扑来源、
切片应用次数和路由重算次数等 ns-3.48 可复现证据。CSV 使用稳定顺序；计数和字节
总量在写出前与 UDP 应用层记录交叉校验。

失败证据统一放在 `diagnostics/failure/`：

- `diagnosticMode=failure` 的 NetworkTransfer 运行只写
  `flow-drop-reasons.csv`；
- 任务未完成时，除丢包原因外还写未完成任务/传输、ISL 队列 Drop、UDP socket
  Drop、流量链路集中度及 `diagnostic-summary.json`，共九个文件；
- 完整任务运行不保留 failure 目录。

复用同一输出目录时，仅清理根目录、旧 `diagnostics/` 路径和
`diagnostics/failure/` 中九个已知诊断文件；未知文件不会被递归删除。
已移除的过渡输出 `routing-summary.json` 和 `routing-reservation-events.csv`
也会按精确文件名清理，其字段已分别由 run、size-aware 和 capacity-aware 输出覆盖。
