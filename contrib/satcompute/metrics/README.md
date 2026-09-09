# 指标与失败诊断

`metrics/` 在正式网络仿真结束时汇总 FlowMonitor、任务、传输、路由和运行时状态。
它只读取已经发生的仿真事件，不参与拓扑更新或路由选择；`topologyOnly=1` 不创建
这些指标文件。

## 代码结构

G3 的 `fault-task-impact.csv` 在 generate 任务运行结束时输出，不依赖概率 audit。
一行记录一个 `(fault_id, task_id, impact_type)`，同任务遭遇不同停机分别保留。
`fault_type` 是该账本的来源标签 F1/F2/F1+F2/F3；旧 fault-events 的 compute/satellite
资源类型不变，追加 `fault_source`。同次两来源命中只执行一次停机，联合 victim 按 task 去重。
`fault_time_ns` 为 START，`impact_time_ns` 为实际观察（例如停机后新到达）；
`task_state_before_fault` 对尚未到达任务为 NOT_ARRIVED，对未在 START 采集的其他状态
为 NOT_CAPTURED，不能用后来的状态冒充历史。`task_state_before_impact` 保存观察前状态。
仅 RUNNING 记录有效 WU 进度，使用真实速率乘已执行时间的 128-bit 整数计算；
未开始的 progress/WU/deadline 使用无效标记。最终结果在结束时关联，未终结为 TRUNCATED。
QUEUED_DELAYED 表示停机期间无法调度，不声称比 none 固定多等 8 s；额外等待需对照。
`recoverable_outage_duration_ns` 保留 START 时已知的计划停机时长；若之后被 F3 抢占，
实际区间以最终 Fault Trace / RECOVERY 事件为准，不把未来抢占时刻倒灌进因果快照。

```text
metrics/
├── metrics.h / metrics.cc                         统一编排、交叉校验和旧文件清理
├── core/
│   ├── fault-metrics.h / fault-metrics.cc         故障事件、因果预测与故障运行汇总
│   ├── flow-metrics.h / flow-metrics.cc           FlowMonitor 汇总与逐流明细
│   ├── link-window.h / link-window.cc             定向链路的时间积分与跨窗分摊
│   ├── link-metrics-recorder.h / .cc              可选设备跟踪、窗口及全程链路输出
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

`MetricsRecorder::Record()` 是运行结束后常规汇总的入口。它先冻结各运行时数据源，
再验证这些数据是否互相一致，最后按当前工作负载、路由模式和诊断模式选择输出文件。
可选的 `LinkMetricsRecorder` 在运行期间流式写出窗口，在常规汇总前完成链路收尾，
不把全部窗口缓存在内存中。

## 常规输出

所有文件都写入 `outputDirectory`。CSV 的记录顺序由上游对象的稳定 ID 或事件顺序
确定；JSON 只记录本次运行结果，不作为下一次运行的配置输入。

| 出现条件 | 文件 | 内容 |
|---|---|---|
| 每次正式仿真 | `network-flow-metrics.csv` | FlowMonitor 的总包数、总字节、时延、抖动、吞吐率和丢包率 |
| 每次正式仿真 | `network-flow-details.csv` | 每个 IPv4 五元组的 FlowMonitor 明细，以及可识别的 transfer ID |
| 每次正式仿真 | `ecmp-route-events.csv` | 逐流路由选择事件；没有相关事件时保留表头 |
| 每次正式仿真 | `run-summary.json` | 仿真/墙钟时间、实际路由和网络参数、拓扑更新与路由计算次数、完成状态及各层总量 |
| 任务模式 | `transfer-summary.csv` | 每个输入/结果传输的声明字节、分包、发送/接收、终态、原因和终止时间 |
| 任务模式 | `task-events.csv` | 任务状态转换事件 |
| 任务模式 | `task-summary.csv` | 每个任务的输入、计算、结果、最终状态、失败原因和失败时间 |
| 任务模式 | `compute-node-summary.csv` | 各算力节点的任务数、忙碌时间和利用率 |
| 提供 `faultTrace` | `fault-events.csv` | canonical START/RECOVERY 顺序、事件后可用性、影响数和路由证据 |
| 提供 `faultTrace` | `fault-summary.json` | 故障类型/事件/活动故障、失败任务、FAILED/CANCELLED transfer 与故障路由重算计数 |
| generate 任务模式 | `fault-task-impact.csv` | 已发生故障的逐任务直接/间接影响、真实进度及最终结果 |
| `faultProbabilityAudit=1` 的 generate | `fault-predictions.csv` | 全部可计算节点上 RUNNING 任务的逐检查点 F1/F2/联合因果概率和任务进度 |
| `faultProbabilityAudit=1` 的 generate | `fault-prediction-summary.json` | 预测记录和涉及任务的数量 |
| `faultProbabilityAudit=1` 的 generate | `fault-model-probabilities.csv` | 随机抽样前由真实在线 F1/F2 状态计算的同结构概率真值，仅用于验证 |
| `faultProbabilityAudit=1` 的 generate | `fault-model-state.csv` | 逐节点检查时刻的忙闲、温度、F1/F2 风险、原生经纬度和实际采样资格；停机期间仍更新状态 |

`run-summary.json` 同时保留便于脚本读取的顶层计数和按 `transfer`、`task` 分组的
汇总。它记录实际使用的任务文件路径和关键运行参数，但不复制一份平台配置。

## 任务与算力字段

`task-summary.csv` 的 N4C 字段为 `task_profile`、`baseline_compute_time_ns`、
`compute_deadline_budget_ns`、`compute_deadline_time_ns`、`compute_deadline_met`、
`result_delivered`、`task_success` 和 `compute_stage_elapsed_time_ns`。
绝对 deadline 在首次开始计算前为 `-1`；布尔列为 `0/1`。
成功必须同时满足按时计算完成和完整 RESULT 送达，不能只看计算完成时间。
elapsed 记录计算开始到完成、失败或仿真截断的已过时间；初始等待不占 deadline。

`compute-node-summary.csv` 另列 `task_count/total_work_units/total_queue_wait_ns`、
`cancelled_running_tasks/removed_queued_tasks`。前两项是分配需求；busy time 是实际占用，
包含失败和截断前已执行的时间，不因失败抹除资源消耗。

## 可选链路窗口统计

`--linkMetrics=1 --linkMetricsInterval=1` 启用每秒定向链路统计。默认关闭，不连接
采集回调、不创建以下三个文件；关闭后复用目录时只清理这三个已知文件。
`topologyOnly=1` 不能启用此功能。实现为 `core/link-window.*`（纯时间积分）和
`core/link-metrics-recorder.*`（原生设备跟踪及流式输出）。

| 文件 | 粒度 |
|---|---|
| `link-window-metrics.csv` | 每条候选定向链路、每个窗口，包括空闲及不可用链路 |
| `network-link-window-metrics.csv` | 每个窗口的全网汇总及最繁忙链路利用率 |
| `link-summary.csv` | 每条定向链路全程汇总及最大窗口利用率 |

窗口为 `[start,end)`，结束时不足一个窗口按实际长度计算。统计器不调度新的仿真
事件；在下一次业务/拓扑事件之前关闭已过去的窗口，仿真结束后补齐空闲窗口。
瞬时采集基于 `PhyTxBegin`，不统计进入设备队列前提交的字节；入队失败只记入丢包。

关键列的含义：

- `window_start_s/window_end_s`：窗口边界，秒；`source_node_id/destination_node_id`
  使用外部卫星 ID，`output_interface` 是源节点 IPv4 输出接口，两方向分别统计。
- `tx_busy_time_s`：实际帧序列化占用时间，不含传播时延及帧间隔；跨窗帧按时间分摊。
  `utilization_percent = tx_busy_time_s / 窗口时长 * 100`。
- `available_time_s`：逻辑链路可用时间；`available_tx_busy_time_s` 是可用期内的
  序列化时间。`available_utilization_percent` 用这两个量相除；全窗不可用时留空，
  不把断链误当作空闲容量。故障后设备仍可能发送旧队列帧，故物理占用和可用占用分列。
- `tx_started_bytes/tx_started_packets`：发送开始落在本窗的完整帧计数（含 PPP 头）。
  它们不能直接计算跨窗利用率；`serialized_bits` 才是按发送时间分摊的比特量，
  `mean_link_throughput_bps = serialized_bits / 窗口秒数`。仿真末尾尚未发完的帧
  只计已发生的序列化部分。与 IP FlowMonitor 的头部和逐跳口径不同。
- `mean_link_capacity_bps`：配置带宽的时间平均；`mean_available_capacity_bps` 还
  乘链路可用时间比例，断链期间贡献为零。
- `mean_reserved_rate_bps/peak_reserved_rate_bps`：capacity-aware 预留速率的时间
  平均和窗口峰值，不是物理占用。非 capacity-aware 为零，不代表另一种准入保证。
- `mean_queue_bytes/max_queue_bytes`：设备队列字节数的时间平均/峰值，排除正在发送
  的帧；`drop_packets/drop_bytes` 为设备队列丢包。

全网 `mean_utilization_percent` 用可用链路时间加权，包含有效但空闲的链路；全部
不可用时留空。`sum_link_throughput_bps` 是各跳链路吞吐量之和，不是端到端吞吐量。
正常压力基线每条链路均为 10 Gbps。窗口最大值只表示该窗口平均，不能宣称是瞬时峰值。
实际空闲与路由可准入容量必须分别分析，不能直接把空闲率换算为新增备份可保证的带宽。

## 故障输出

`fault-events.csv` 每行对应一个已经执行的事件，列为：

```text
simulation_time_ns, fault_id, node_id, fault_type, event_type,
start_time_ns, duration_ns, failure_probability,
satellite_available_after, communication_available_after,
compute_available_after, affected_task_count, affected_transfer_count,
route_recomputed, fault_source, p_f1, p_f2, temperature_c, continuous_busy_s
```

可选输入为 null 时对应 CSV 单元格为空，布尔值固定写作 `true/false`。一个整星
timestamp 批次最多令一行 `route_recomputed=true`，因此逐行求和就是故障引起的
路由重算次数。

`fault_source` 在 START 行标记 F1/F2/F1+F2/F3；概率、温度和连续 busy 秒数是
该故障 START 抽样时的元数据，RECOVERY 行复用这些值，不表示恢复时状态。
F1 duration 由 START 温度派生，F2 固定 8 秒；同刻双来源取最大值，F3 可截短活动停机。

`fault-summary.json` 固定汇总 `fault_count`、两类 fault count、START/RECOVERY 两类 event count、
`active_fault_count_at_end`、`failed_task_count`、`failed_transfer_count`、
`cancelled_transfer_count` 和 `route_recomputation_count_due_to_fault`。失败与取消计数
来自仿真终点的稳定终态，不把仍在运行的对象误记为故障终态。

### 计算故障预测输出

以下概率文件以及 `fault-model-state.csv` 均属于显式审计输出。`faultProbabilityAudit` 默认 `false`；关闭
时平台不创建预测器，并从复用的 `outputDir` 中删除陈旧概率审计文件。

`fault-predictions.csv` 每行对应当前可计算节点上一个正在运行任务的因果
预测，列为：

```text
simulation_time_ns, node_id, task_id, task_compute_start_time_ns, task_service_time_ns,
task_elapsed_time_ns, remaining_compute_time_ns,
expected_compute_completion_time_ns, completion_ratio,
f1_step_failure_probability, f2_step_failure_probability,
combined_step_failure_probability, horizon_step_count,
failure_before_finish_probability
```

所有字段均来自预测时刻已经可见的任务快照和无随机数 F1/F2 影子模型。
三个单步字段满足：

```text
q_comp,k = 1 - (1 - q_F1,k) * (1 - q_F2,k)
P_fail_before_finish = 1 - product(k, 1 - q_comp,k)
```

CSV 中的三个单步字段对应当前 `k=0`；累计概率还包含任务预计完成前的未来检查点。
未来 F1 按任务在无故障条件下继续忙碌推进，未来 F2 使用 ns-3.48 原生轨道的按时刻
ECEF 坐标，因此 `q_comp` 随温度、能源和空间区域变化，不是冻结常数。

`fault-prediction-summary.json` 固定包含：

- `prediction_count`；
- `task_count`。

平台不再把一次随机结果写成 `true/false` 预测标签，也不计算 Brier score。真实故障
仍保存在 Fault Trace、`fault-events.csv` 和任务终态中；后续备份实验可按稳定 ID 和
时间关联，不把结果反向塞入预测器指标。

generate 额外写出的 `fault-model-probabilities.csv` 与 `fault-predictions.csv` 使用
同一列结构，但前者读取真实在线模型状态并在 F1/F2 随机抽样前计算，后者读取独立的
无随机数审计状态。两份 CSV 都只用于离线验证，不是故障输入或在线查询前提。
[`compare-fault-probabilities.py`](../tools/validation/compare-fault-probabilities.py)
按 `(simulation_time_ns,node_id,task_id)` 将 generate 真值与独立审计预测逐行匹配，
检查其余上下文，并分别给出 `q_F1`、`q_F2`、`q_comp` 和
`P_fail_before_finish` 的 MAE、RMSE 与最大绝对误差。

## 路由模式输出

| 路由模式 | 额外文件 | 含义 |
|---|---|---|
| `global-size-aware-hrw` | `size-aware-reservation-events.csv`、`size-aware-summary.json` | 每次候选分配/复用/释放及最终字节预留状态 |
| `global-capacity-aware-hrw` | 上述两个 size-aware 文件，再加 `capacity-aware-summary.json` | 流注册表证据，以及结束时活动路径、定向链路预留速率和等待传输数 |

capacity-aware 也使用 `FlowRouteRegistry` 管理 flow 生命周期，所以会生成
size-aware 文件；这里的文件名表示注册表使用字节预留事件格式，并不表示
capacity-aware 退化成 size-aware 选路。

所有 transfer 均已进入 `COMPLETED`、`FAILED` 或 `CANCELLED` 后，注册表必须没有
活动 flow、候选分配或残留字节；capacity-aware 还必须没有活动路径、链路带宽
预留或等待传输。这条检查同样适用于故障导致的 `PARTIAL` 运行，违反时直接失败，
而不是写出看似正确的终态。

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
- 全部 transfer 进入终态后不得残留 size-aware/capacity-aware 预留状态；
- FlowMonitor 丢包总数、显式 DropReason 和未归因数量必须能够闭合。

复用同一个 `outputDirectory` 时，平台只删除它自己认识的陈旧指标文件。九个失败
诊断文件会从历史根目录、`diagnostics/` 和当前 `diagnostics/failure/` 精确清理，
空目录随后移除；用户放入的未知文件不会被递归删除。已经停用的
`routing-summary.json` 和 `routing-reservation-events.csv` 也只按精确文件名清理。
无 `faultTrace` 时不会生成故障专用文件；若复用一个曾执行故障的输出目录，只精确
删除 `fault-events.csv`、`fault-summary.json`、`fault-predictions.csv`、
`fault-prediction-summary.json` 与 `fault-model-probabilities.csv`，不会触碰用户文件。

相关覆盖见 [测试说明](../tests/README.md)中的 task、capacity-aware、diagnostics
smoke、完整 workload regression 与 fault lifecycle regression；DropReason 的离线一致性检查见
[validation 工具](../tools/validation/README.md)。
