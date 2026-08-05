# 故障模块

`fault/` 负责确定性故障输入、可用性覆盖和运行期批处理。reader 只做解析与校验；
`FaultController` 再把已验证的 compute/satellite trace 调度到精确 ns-3 时刻，不按
概率抽样。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `fault-definition.h/.cc` | `compute`/`satellite` 类型、字段记录和派生预警/恢复时刻 |
| `fault-trace.h/.cc` | closed-world JSON、节点/算力引用校验、区间冲突检测和排序 |
| `fault-state.h/.cc` | 每颗卫星的 satellite/communication/compute 可用性与活动故障集合 |
| `fault-controller.h/.cc` | timestamp 分组、事件排序，以及任务、传输与有效拓扑联动 |

`failure_probability` 只是预警时暴露给未来决策器的风险估计。只要事件已写入 trace，
它就一定在 `start_time_ns` 发生，平台不会再次按概率抽样。`duration_ns=null` 表示持续
到仿真结束；有限恢复时刻允许等于或晚于仿真终点，但必须通过有符号纳秒溢出检查。

同一节点上的任意 `compute`/`satellite` 区间不得重叠。区间采用开始包含、恢复不包含
的语义，因此一个故障可以恰好在前一个故障的恢复时刻开始。reader 最终按
`fault_id` 排序，使 JSON 数组顺序不影响后续事件身份。

## 计算故障执行

控制器把同一时刻的事件合成一个批次，并固定按以下优先级、再按 `fault_id` 升序
处理：

```text
NOTICE -> RECOVERY -> START
```

批次得到最终 `FaultState` 后只调用一次任务层。compute START 只令目标节点
`compute_available=false`：不关闭 ISL、不改变卫星坐标、不修改 active edge，也不
触发路由重算。有限 RECOVERY 只重新允许后来到达的任务使用节点，不复活旧任务。

故障开始时，目标节点上 `INPUT_TRANSFERRING`、`QUEUED`、`RUNNING` 的任务进入
`FAILED/COMPUTE_NODE_FAILURE`；输入或尚未启动的结果传输按阶段取消。已经进入
`RESULT_TRANSFERRING` 的任务继续通信，`COMPLETED` 不受影响。故障期间到达的任务
立即失败，并取消两条尚未启动的传输。

## 整星故障执行

自然轨道和故障覆盖分层，候选身份与圆轨道公式不因故障改变：

```text
natural_active = distance <= maxIslDistance
effective_active = natural_active
                   && communication_available[source]
                   && communication_available[destination]
```

satellite START 先令三类 availability 都为 false，并按任务当前阶段终止计算和端点
transfer；随后原子关闭最终故障集合关联的 ISL。有效边集合变化时立即重算 IPv4、
推进一次 route epoch，并在路由稳定后通知 capacity-aware engine。一个 timestamp
批次只应用一次有效拓扑，因此最多重算一次；若同批次含多个整星事件，
`route_recomputed=true` 归属于该批次最后一个确定性状态事件。

整星只作为中间转发节点时，普通路由立即使用新表；capacity-aware sender 暂停、
释放旧完整路径，并以同一 transfer ID 在新图中重准入。故障卫星是活动 transfer
源/目的时分别进入 `FAILED/SOURCE_SATELLITE_FAILED` 或
`FAILED/DESTINATION_SATELLITE_FAILED`；父任务失败后未启动的 transfer 进入
`CANCELLED/TASK_FAILED`。

有限 RECOVERY 在精确恢复时刻重新读取原生 mobility 坐标并计算 natural topology，
只恢复当时仍在距离门限内的候选。卫星在整个故障期间继续沿轨道运动，恢复不会复活
旧 `FAILED/CANCELLED` 对象。

`FaultRuntimeEventRecord` 保留 notice/start/recovery 后的三类可用性、受影响任务和
传输数及路由重算证据。正式运行把这些记录按控制器事件顺序写入
`fault-events.csv`；`fault-summary.json` 汇总故障类型、事件、仿真结束时活动故障、
失败任务、FAILED/CANCELLED transfer 和故障引起的路由重算次数。未提供
`faultTrace` 时不生成这两个文件。

完整输入字段见 [`input/fault/README.md`](../input/fault/README.md)，输出字段职责见
[`metrics/README.md`](../metrics/README.md)。
