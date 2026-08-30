# 故障输入

故障输入分为两类，不能合并成完整 scenario JSON：

- Fault Model 配置只在 `faultMode=generate` 中读取，负责风险模型和生成参数；
- Fault Trace 是本轮已经确定的风险/故障 episode，generate 写出、replay 读取。

组合规则为：

| `faultMode` | `faultModelConfig` | `faultTrace` |
|---|---|---|
| `none` | 必须为空 | 必须为空 |
| `generate` | 必须是现有配置文件 | 必须是输出文件路径 |
| `replay` | 必须为空 | 必须是现有 v1/v2 trace |

`topologyOnly=1` 只能与 `faultMode=none` 一起使用，因为 topology-only 描述无故障的
自然轨道和候选拓扑。

## Fault Model 配置

根对象为严格 closed-world schema；缺字段、未知字段、非有限数值或非法组合都会在
仿真开始前失败。当前可直接使用
[`n4b-f1-calibrated.json`](n4b-f1-calibrated.json)：它启用 F1，关闭尚未接入在线
生成器的 F2/F3。

### 根字段

| 字段 | 单位/范围 | 含义 |
|---|---|---|
| `schema_version` | 固定 `1` | Fault Model 配置 schema |
| `check_interval_ns` | 正整数 ns | F1/F2 风险更新和计算故障采样周期；当前标定值为 1 s |
| `recoverable_compute_duration_ns` | 正整数 ns | F1/F2 compute 故障的系统恢复时间；不是热模型推导值 |
| `self_state` | object | F1 自身状态模型 |
| `radiation` | object | F2 辐射暴露模型配置；下一阶段启用 |
| `debris` | object | F3 永久整星故障配置；后续阶段启用 |

### `self_state.temperature`

| 字段 | 单位/范围 | 含义 |
|---|---|---|
| `base_c` | ℃ | 空闲状态趋近的基础温度 |
| `saturation_c` | ℃ | 持续计算时趋近的热平衡温度 |
| `risk_c` | ℃ | 温度风险曲线起点 |
| `critical_c` | ℃ | 确定性保护停机温度 |
| `heating_tau_s` | 正数 s | 指数升温时间常数，不是“升温完成时间” |
| `cooling_tau_s` | 正数 s | 指数降温时间常数，不是“降温完成时间” |
| `growth_factor` | 正数 | `risk_c..critical_c` 内指数风险曲线形状 |

必须满足：

```text
base_c < risk_c < critical_c < saturation_c
```

### `self_state.energy`

| 字段 | 单位/范围 | 含义 |
|---|---|---|
| `enabled` | bool | 是否把能源压力作为 F1 小权重修正 |
| `initial_dod` | `[0,1]` | 初始放电深度 |
| `risk_dod` | `[0,1]` | 能源压力起点 |
| `critical_dod` | `[0,1]` | 能源压力归一化上界 |
| `battery_wh` | 正数 Wh | 电池容量 |
| `incremental_compute_power_w` | 非负 W | 忙碌计算相对空闲的增量功率 |
| `correction_weight` | `[0,1]` | 能源压力进入综合风险的权重 |

必须满足 `initial_dod <= risk_dod < critical_dod`。DoD 使用 W、Wh 和秒进行单位
换算；本阶段不模拟充电，空闲时不会重置 DoD。

### `self_state` 其余字段

| 字段 | 范围 | 含义 |
|---|---|---|
| `enabled` | bool | 是否启用 F1 在线生成；启用时必须提供 ComputeProfile 和 TaskTrace |
| `risk_threshold` | `[0,1]` | 综合风险达到该值时开启节点风险 episode |
| `max_failure_intensity_per_s` | 非负 `s^-1` | `lambda_F1 = max * R_F1` 的最大强度 |

`risk_threshold` 是风险通知阈值，不是每秒故障概率。每个检查区间实际使用：

```text
q_F1 = 1 - exp(-lambda_F1 * dt)
```

达到 `critical_c` 时 `q_F1=1`。当前 `0.005 s^-1` 来自 66 星、1000 秒、30 个固定
run 的功能标定，只用于当前实验尺度；证据见
[`docs/calibration/n4b-f1`](../../../../docs/calibration/n4b-f1/README.md)。

### `radiation` 与 `debris`

F2 字段预先冻结为经纬度矩形、区域内有效故障强度、风险阈值和离开区域是否重置
连续暴露；F3 字段预先冻结为 `fixed_k`/`poisson` 模式、固定数量和单星强度。它们
当前必须保持 `enabled=false`，在线含义与最终参数将在对应阶段完成并更新本文档。

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
      "failure_probability": 0.0,
      "warning_lead_time_ns": 12000000000,
      "risk_duration_ns": null,
      "duration_ns": 10000000000
    },
    {
      "fault_id": 2,
      "node_id": 11,
      "fault_type": "compute",
      "fault_occurred": false,
      "notice_time_ns": 44000000000,
      "start_time_ns": null,
      "failure_probability": 0.0,
      "warning_lead_time_ns": null,
      "risk_duration_ns": 10000000000,
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
