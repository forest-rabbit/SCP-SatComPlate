# 故障轨迹输入与输出

`input/fault/` 只说明平台可写出、可重放的 Fault Trace 数据合同，不存放故障模型
配置。F1/F2/F3 的内部参数集中在 [`fault-para.cc`](../../fault/fault-para.cc)，与
`para.cc` 一样属于编译期默认参数；修改后需要重新编译。任务输入仍由
`tools/generation/generate-task-workload.py` 生成，任务只提供 F1 所需的忙闲条件或
F2 故障发生时的执行验证对象，不会预先决定随机故障是否发生。

运行模式与轨迹路径的组合规则为：

| `faultMode` | `faultTrace` |
|---|---|
| `none` | 必须为空 |
| `generate` | 输出文件路径；在线判定并真实执行故障后写出 v2 trace |
| `replay` | 已存在的 v1/v2 trace；不再抽样 |

`--faultEnableF1/2/3` 是每次运行的来源开关，默认分别为 true/false/false；模型内部
数值仍只位于 `fault-para.cc`。当前 `generate` 支持 F1-only、F2-only 或二者同时
启用，它们都必须同时提供 ComputeProfile 与 TaskTrace；F1/F2 使用独立随机流分别
判定，同刻命中合并为一次 compute START，trace 概率为 `q_comp`。F3 尚未接入。
`replay` 不读取这些来源开关，也不重新计算模型；是否提供任务输入取决于 trace 中的
节点和要验证的执行结果。

`topologyOnly=1` 只能与 `faultMode=none` 一起使用，因为 topology-only 描述无故障的
自然轨道和候选拓扑。

## Fault Trace v2

generate 只写 schema v2。根对象和每条记录都必须包含完整字段，即使值为 `null`：

```json
{
  "schema_version": 2,
  "faults": [
    {
      "fault_id": 1,
      "node_id": 0,
      "fault_type": "compute",
      "fault_occurred": true,
      "notice_time_ns": 44000000000,
      "start_time_ns": 56000000000,
      "failure_probability": 0.003119061056128215,
      "warning_lead_time_ns": 12000000000,
      "risk_duration_ns": null,
      "duration_ns": 8000000000
    },
    {
      "fault_id": 2,
      "node_id": 33,
      "fault_type": "compute",
      "fault_occurred": false,
      "notice_time_ns": 44000000000,
      "start_time_ns": null,
      "failure_probability": 0.003119061056128215,
      "warning_lead_time_ns": null,
      "risk_duration_ns": 2000000000,
      "duration_ns": null
    }
  ]
}
```

### 通用字段

| 字段 | 合同 |
|---|---|
| `fault_id` | 正 `uint64`，文件内唯一 |
| `node_id` | 当前星座的稳定卫星 ID，不是全局 `Node::GetId()` |
| `fault_type` | `compute` 或 `satellite` |
| `fault_occurred` | 是否真正产生 START |
| `notice_time_ns` | 风险首次达到阈值的绝对时刻，或 `null` |
| `start_time_ns` | 实际故障开始的绝对时刻，或 `null` |
| `failure_probability` | NOTICE 或无预警 START 当时的单步联合概率，或 `null` |
| `warning_lead_time_ns` | `start-notice`，只用于有预警实际故障 |
| `risk_duration_ns` | `clear-notice`，只用于风险-only episode |
| `duration_ns` | 可恢复 compute 故障持续时间；永久整星故障为 `null` |

只允许四种记录组合：

| 记录 | notice | start | probability | lead | risk duration | duration |
|---|---|---|---|---|---|---|
| 风险-only compute | 有 | null | 有 | null | 正数 | null |
| 有预警 compute 故障 | 有 | 有 | 有 | `start-notice` | null | 正数 |
| 无预警 compute 故障 | null | 有 | 有 | null | null | 正数 |
| 永久 satellite 故障 | null | 有 | null | null | null | null |

同一节点实际发生的故障区间不得重叠，恰好在上一恢复时刻开始除外。writer 按
`anchor_time -> node_id -> fault_id` canonical 排序；anchor 优先使用 notice，否则
使用 start。相同配置、seed、run 和任务输入重复 generate，应产生逐字节相同 trace。

风险与故障统一写入这一份文件，不存在独立 `risk-trace.json`。replay 不再根据
`failure_probability` 抽样；`fault_occurred=true` 的记录一定执行，false 的记录只
重放 NOTICE/NOTICE_CLEAR。

## v1 兼容

N4A 的七字段 trace 没有 `schema_version`，仍可由 replay 读取。它等价于所有记录
`fault_occurred=true`，并在读取时派生 lead time。新输出始终使用 v2，不再写 v1。
