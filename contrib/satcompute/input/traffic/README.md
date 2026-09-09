# TaskTrace 输入

`input/traffic/workload/` 只保存任务到达，不保存独立网络传输。程序仅在同时指定
`--computeProfile` 与 `--taskTrace` 时加载任务。

## JSON 合同

根对象只允许非空数组 `tasks`：

```json
{
  "tasks": [
    {
      "task_id": 1,
      "source_node_id": 0,
      "compute_node_id": 3,
      "result_node_id": 0,
      "input_bytes": 4096,
      "output_bytes": 2050,
      "compute_work_units": 1000000,
      "arrival_time_ns": 100000000
    }
  ]
}
```

| 字段 | 类型 | 约束 |
|---|---|---|
| `task_id` | `uint64` | 唯一，范围 `1..UINT64_MAX/2` |
| `source_node_id` | `uint32` | 当前星座中的卫星，且不同于计算节点 |
| `compute_node_id` | `uint32` | 当前星座且存在于 ComputeProfile，且不同于结果节点 |
| `result_node_id` | `uint32` | 当前星座中的卫星；允许与源节点相同 |
| `input_bytes` | `uint64` | 必须大于 0 |
| `output_bytes` | `uint64` | 必须大于 0；计算完成后实际发送的结果大小 |
| `compute_work_units` | `uint64` | 必须大于 0 |
| `arrival_time_ns` | 非负整数 | 必须严格早于 `simulationDuration` |
| `task_profile` | 可选字符串 | `dense-image`、`sparse-inference`、`compression`、`llm`；缺省为内部 `UNSPECIFIED`，不接受显式 null/未知类别 |

TaskTrace 是精确事件数据，因此到达时刻直接使用整数纳秒；平台级仿真时长和周期仍
以秒传入 CLI。文件不接受未知字段，也不包含 schema/version/hash。reader 会按
`task_id` 排序，数组顺序不影响运行。

N4C 的 C800 全部显式给出 `task_profile`，使用全 66 星 100,000 WU/s 的
[正式输入](../examples/leo-66-1300s-n4c-g3-truncnormal-v3/task-trace.json)。旧文件缺省类别时保持原算力口径；
首次计算才建立的绝对 deadline 不存入输入 JSON，属于运行时状态。

## 正式 workload

| 文件 | 任务数 | 总输入字节 | 配套 ComputeProfile |
|---|---:|---:|---|
| `workload/stress-40.json` | 40 | 4,000,000 | 22 节点 profile |
| `workload/size-aware-60.json` | 60 | 3,000,000,000 | 66 节点 all profile |
| `../examples/leo-66-100s-20tasks/task-trace.json` | 20 | 20,000,000 | 22 节点 profile |

完整可运行组合见
[`input/examples/leo-66-100s-20tasks/`](../examples/leo-66-100s-20tasks/README.md)。
小型合法/非法输入仅作为测试 fixture，位于 `tests/fixtures/task/`。

任务状态、FCFS 计算和派生 transfer ID 见 [`task/README.md`](../../task/README.md)。
