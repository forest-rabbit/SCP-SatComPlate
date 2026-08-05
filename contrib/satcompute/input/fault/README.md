# FaultTrace 输入

`--faultTrace=<path>` 可选读取一份确定性故障 JSON；默认空路径表示无故障。文件根
对象只允许 `faults`，数组可为空，每个元素必须精确包含以下七个字段：

```json
{
  "faults": [
    {
      "fault_id": 1,
      "node_id": 30,
      "fault_type": "compute",
      "start_time_ns": 20000000000,
      "notice_time_ns": 15000000000,
      "failure_probability": 0.82,
      "duration_ns": 5000000000
    },
    {
      "fault_id": 2,
      "node_id": 17,
      "fault_type": "satellite",
      "start_time_ns": 60000000000,
      "notice_time_ns": null,
      "failure_probability": null,
      "duration_ns": null
    }
  ]
}
```

| 字段 | 合同 |
|---|---|
| `fault_id` | 正 `uint64`，文件内唯一；canonical 输出按它升序 |
| `node_id` | 当前星座中的稳定卫星 ID，不是全局 `Node::GetId()` |
| `fault_type` | 只允许 `compute` 或 `satellite` |
| `start_time_ns` | `0 <= value < simulationDurationNs`，事件必然在该时刻生效 |
| `notice_time_ns` | `null` 或 `0..start_time_ns` 的绝对时刻 |
| `failure_probability` | 与 notice 同时为 null/非 null；非空时为有限 `[0,1]` 风险元数据 |
| `duration_ns` | `null` 或正整数；`start + duration` 不得溢出 `int64` |

`compute` 故障的节点还必须存在于本次 ComputeProfile；`satellite` 故障可引用任意
稳定卫星 ID。同一节点的任何故障区间不得重叠，恰好在前一恢复时刻开始除外。

平台在精确纳秒调度 notice、start 和仿真窗口内的 recovery。当前 `compute` 类型已
执行：通信和路由保持不变，尚未越过计算阶段的目标任务失败，有限恢复只接纳后续
任务。`satellite` 类型已完成字段与区间校验，但整星执行尚未接入，运行阶段会明确
拒绝；不会静默忽略或降级。`topologyOnly=1` 不接受 `faultTrace`，因为切片生成仍
描述无故障的自然拓扑。
