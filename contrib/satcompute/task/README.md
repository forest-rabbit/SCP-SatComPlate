# 任务与算力模型

平台使用两个相互独立的 JSON 输入：ComputeProfile 描述静态算力，TaskTrace
描述任务到达。两者都采用 closed-world 合同，不接受未知字段，也不包含
`schema_version`。

ComputeProfile 的根对象只包含 `compute_nodes`。每个计算节点显式给出稳定
`node_id` 和正整数 `compute_rate_work_units_per_second`，平台按 `node_id`
排序，因此数组顺序不影响结果。

TaskTrace 的根对象只包含 `tasks`。每项显式给出：

- `task_id`、源卫星、计算卫星和结果卫星 ID；
- `input_bytes`、`compute_work_units` 和 `output_bytes`；
- 整数纳秒 `arrival_time_ns`。

平台不执行真实业务算法，计算结束后的结果传输大小严格使用该任务的
`output_bytes`，不会根据输入大小或计算量再次推导。

任务状态固定为：

```text
PENDING -> INPUT_TRANSFERRING -> QUEUED -> RUNNING
        -> RESULT_TRANSFERRING -> COMPLETED
```

每个计算节点是单服务台、非抢占 FCFS 队列，排序键为
`(queue_enter_time_ns, task_id)`。服务时间使用整数计算：

```text
ceil(compute_work_units * 1,000,000,000
     / compute_rate_work_units_per_second) ns
```

任务 ID `T` 在运行时派生输入传输 ID `2*T-1` 和结果传输 ID `2*T`。端口、
地址、分包和路由同样属于运行时状态，不写入任务 JSON。
