# 故障模块

`fault/` 负责确定性故障输入、可用性覆盖和运行期批处理。reader 只做解析与校验；
`FaultController` 再把已验证的 compute trace 调度到精确 ns-3 时刻，不按概率抽样。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `fault-definition.h/.cc` | `compute`/`satellite` 类型、字段记录和派生预警/恢复时刻 |
| `fault-trace.h/.cc` | closed-world JSON、节点/算力引用校验、区间冲突检测和排序 |
| `fault-state.h/.cc` | 每颗卫星的 satellite/communication/compute 可用性与活动故障集合 |
| `fault-controller.h/.cc` | timestamp 分组、事件排序、状态变更和 TaskCoordinator 联动 |

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

`FaultRuntimeEventRecord` 保留 notice/start/recovery 后的三类可用性、受影响任务和
传输数，以及 `route_recomputed=false` 证据。持久化 `fault-events.csv` 和汇总指标
在 N4A 的指标收口小步统一接入。

当前执行器只接受 `compute`。`satellite` 已可解析且 `FaultState` 已定义覆盖语义，
但在整星链路关闭、原子路由更新与活动传输处理接入前，控制器会明确拒绝运行，避免
把整星故障静默降级为仅计算故障。

完整输入字段见 [`input/fault/README.md`](../input/fault/README.md)。
