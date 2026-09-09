# 故障事件输出合同

`input/fault/` 只说明平台写出的 Fault Trace 数据合同，不存放故障模型
配置。F1/F2/F3 的内部参数集中在 [`fault-para.cc`](../../fault/fault-para.cc)，与
`para.cc` 一样属于编译期默认参数；修改后需要重新编译。任务输入仍由
`tools/generation/generate-task-workload.py` 生成，任务只提供 F1 所需的忙闲条件或
F2 故障发生时的执行验证对象，不会预先决定随机故障是否发生。

运行模式与轨迹路径的组合规则为：

| `faultMode` | `faultTrace` |
|---|---|
| `none` | 必须为空 |
| `generate` | 输出文件路径；在线判定并真实执行故障后写出 v2 trace |

`--faultEnableF1/2/3` 默认分别为 true/false/false；模型内部数值仍只位于
`fault-para.cc`。当前 `generate` 支持 F1-only、F2-only 或二者同时
启用，它们都必须同时提供 ComputeProfile 与 TaskTrace；F1/F2 使用独立随机流分别
判定，同刻命中合并为一次 compute START，trace 概率为 `q_comp`。F3 可单独运行且
不要求任务输入，也可与 F1/F2 同时启用；它产生无预警、无恢复的永久 satellite
START，并在同节点同刻优先。

生产没有故障文件输入；重复实验使用相同参数和 seed/run 再次 generate。

`topologyOnly=1` 只能与 `faultMode=none` 一起使用，因为 topology-only 描述无故障的
自然轨道和候选拓扑。

## 运行期标识与 JSON 映射

只记录实际 START，恢复由 start+duration 派生，当前只保留 START/RECOVERY 两种事件。
F1 恢复时间按 START 温度动态计算，F2 固定 8 秒，同刻两者命中取最大值；F3 可截短
活动 compute 区间并永久关闭卫星。临时停机保留 QUEUED，恢复不复活 FAILED。

Fault Trace 仍是 v2 输出容器，但旧 NOTICE、风险-only、预警提前量和风险持续时间字段
已移除；没有生产 reader，因此不提供旧输出回放兼容层。每条记录包含以下 13 个字段：

| 字段 | 合同 |
|---|---|
| fault_id / node_id | 正唯一故障 ID / 稳定卫星 ID |
| fault_type | compute 或 satellite |
| fault_occurred | 始终 true |
| start_time_ns | 实际 START 绝对 ns，非负且早于仿真终点 |
| failure_probability | START 当次抽样的 q_comp；F3 为 null |
| duration_ns | 正 compute 停机 ns；永久 F3 为 null |
| p_f1 / p_f2 | START 当次的两个来源概率；F3 为 null |
| f1_occurred / f2_occurred | 同次独立抽样的命中标志；F3 均 false |
| temperature_c | F1 启用时的 START 温度，否则 null |
| continuous_busy_s | F1 启用时已连续 busy 的秒数，否则 null |

同一节点实际故障区间不重叠，可首尾相接。writer 按 start_time_ns、node_id、fault_id
排序。相同输入、参数和 seed/run 的重复 generate 应逐字节一致。独立概率审计 CSV
不是 Fault Trace，也不是平台输入。完整物理模型与生命周期见 [fault README](../../fault/README.md)。
