# 指标与失败诊断

`metrics/` 在正式网络仿真结束时汇总 FlowMonitor、任务、传输、路由和运行时状态。
它只读取已经发生的仿真事件，不参与拓扑更新或路由选择；`topologyOnly=1` 不创建
这些指标文件。

## 代码结构

```text
metrics/
├── metrics.h / metrics.cc                         统一编排、交叉校验和旧文件清理
├── core/
│   ├── flow-metrics.h / flow-metrics.cc           FlowMonitor 汇总与逐流明细
│   ├── transfer-metrics.h / transfer-metrics.cc   逻辑传输汇总
│   ├── task-metrics.h / task-metrics.cc           任务事件、任务汇总和算力节点汇总
│   └── run-summary.h / run-summary.cc              单次运行汇总
├── routing/
│   ├── ecmp-route-recorder.h / .cc                收集所有逐流选路事件
│   ├── ecmp-metrics.h / .cc                       写出逐流选路证据
│   ├── size-aware-metrics.h / .cc                 字节预留事件与结束状态
│   └── capacity-aware-metrics.h / .cc             路径带宽预留结束状态
└── diagnostics/
    ├── failure-diagnostics.h / .cc                未完成任务、队列和 socket 诊断
    └── flow-drop-reason-diagnostics.h / .cc       FlowMonitor 丢包原因归因
```

`MetricsRecorder::Record()` 是唯一的总入口。它先冻结各运行时数据源，再验证这些
数据是否互相一致，最后按当前工作负载、路由模式和诊断模式选择输出文件。

## 常规输出

所有文件都写入 `outputDirectory`。CSV 的记录顺序由上游对象的稳定 ID 或事件顺序
确定；JSON 只记录本次运行结果，不作为下一次运行的配置输入。

| 出现条件 | 文件 | 内容 |
|---|---|---|
| 每次正式仿真 | `network-flow-metrics.csv` | FlowMonitor 的总包数、总字节、时延、抖动、吞吐率和丢包率 |
| 每次正式仿真 | `network-flow-details.csv` | 每个 IPv4 五元组的 FlowMonitor 明细，以及可识别的 transfer ID |
| 每次正式仿真 | `ecmp-route-events.csv` | 逐流路由选择事件；没有相关事件时保留表头 |
| 每次正式仿真 | `run-summary.json` | 仿真/墙钟时间、实际路由和网络参数、拓扑更新与路由计算次数、完成状态及各层总量 |
| 任务模式 | `transfer-summary.csv` | 每个输入/结果传输的声明字节、分包、发送、接收和完成时间 |
| 任务模式 | `task-events.csv` | 任务状态转换事件 |
| 任务模式 | `task-summary.csv` | 每个任务的输入、计算、结果和端到端完成情况 |
| 任务模式 | `compute-node-summary.csv` | 各算力节点的任务数、忙碌时间和利用率 |

`run-summary.json` 同时保留便于脚本读取的顶层计数和按 `transfer`、`task` 分组的
汇总。它记录实际使用的任务文件路径和关键运行参数，但不复制一份平台配置。

## 路由模式输出

| 路由模式 | 额外文件 | 含义 |
|---|---|---|
| `global-size-aware-hrw` | `size-aware-reservation-events.csv`、`size-aware-summary.json` | 每次候选分配/复用/释放及最终字节预留状态 |
| `global-capacity-aware-hrw` | 上述两个 size-aware 文件，再加 `capacity-aware-summary.json` | 流注册表证据，以及结束时活动路径、定向链路预留速率和等待传输数 |

capacity-aware 也使用 `FlowRouteRegistry` 管理 flow 生命周期，所以会生成
size-aware 文件；这里的文件名表示注册表使用字节预留事件格式，并不表示
capacity-aware 退化成 size-aware 选路。

完整运行结束时，注册表必须没有活动 flow、候选分配或残留字节；capacity-aware
还必须没有活动路径、链路带宽预留或等待传输。违反这些条件会直接使运行失败，
而不是写出看似成功的结果。

## 失败诊断

只有同时满足以下条件时才创建 `diagnostics/failure/`：

1. `diagnosticMode=failure`；
2. 当前为任务模式；
3. 仿真结束时至少一个任务未完成。

目录固定包含九个文件：

| 文件 | 内容 |
|---|---|
| `incomplete-tasks.csv` | 未完成任务及最后状态 |
| `incomplete-transfers.csv` | 未完成传输和缺失字节/数据包 |
| `isl-queue-drops.csv` | 每次 ISL 队列丢包事件 |
| `isl-queue-drop-summary.csv` | 按定向链路汇总的队列丢包 |
| `udp-socket-drops.csv` | UDP 接收 socket 丢包事件 |
| `udp-socket-drop-summary.csv` | 按接收端汇总的 socket 丢包 |
| `flow-link-concentration.csv` | transfer 在定向链路上的流量集中度与丢包 |
| `flow-drop-reasons.csv` | FlowMonitor 每条 flow 的显式 DropReason 与未归因丢失 |
| `diagnostic-summary.json` | 以上诊断的总量和交叉统计 |

`flow-drop-reasons.csv` 把 ns-3 的 0--8 DropReason 映射为稳定名称；当
FlowMonitor 的 `lostPackets` 大于显式原因总数时，差值以
`UNATTRIBUTED_TIMEOUT` 单独记录，且不虚构未知的丢失字节数。

## 一致性与目录复用

写出前会执行以下关键检查：

- `transfer-summary.csv` 的发送/接收应用字节必须与 UDP 应用层计数一致；
- task、transfer 的完成数必须和运行状态一致；
- 完整运行不得残留 size-aware/capacity-aware 预留状态；
- FlowMonitor 丢包总数、显式 DropReason 和未归因数量必须能够闭合。

复用同一个 `outputDirectory` 时，平台只删除它自己认识的陈旧指标文件。九个失败
诊断文件会从历史根目录、`diagnostics/` 和当前 `diagnostics/failure/` 精确清理，
空目录随后移除；用户放入的未知文件不会被递归删除。已经停用的
`routing-summary.json` 和 `routing-reservation-events.csv` 也只按精确文件名清理。

相关覆盖见 [测试说明](../tests/README.md)中的 task、capacity-aware、diagnostics
smoke 与完整 workload regression；DropReason 的离线一致性检查见
[validation 工具](../tools/validation/README.md)。
