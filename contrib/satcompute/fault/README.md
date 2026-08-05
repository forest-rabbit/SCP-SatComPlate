# 故障模块

`fault/` 负责确定性故障输入与后续运行期故障协调。当前 N4A 输入阶段只完成解析、
校验和 canonical 排序，不在 reader 中调度事件或修改拓扑、路由、任务与计算状态。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `fault-definition.h/.cc` | `compute`/`satellite` 类型、字段记录和派生预警/恢复时刻 |
| `fault-trace.h/.cc` | closed-world JSON、节点/算力引用校验、区间冲突检测和排序 |

`failure_probability` 只是预警时暴露给未来决策器的风险估计。只要事件已写入 trace，
它就一定在 `start_time_ns` 发生，平台不会再次按概率抽样。`duration_ns=null` 表示持续
到仿真结束；有限恢复时刻允许等于或晚于仿真终点，但必须通过有符号纳秒溢出检查。

同一节点上的任意 `compute`/`satellite` 区间不得重叠。区间采用开始包含、恢复不包含
的语义，因此一个故障可以恰好在前一个故障的恢复时刻开始。reader 最终按
`fault_id` 排序，使 JSON 数组顺序不影响后续事件身份。

完整输入字段见 [`input/fault/README.md`](../input/fault/README.md)。故障状态、事件
批处理和执行控制器在后续 N4A 小步接入。
