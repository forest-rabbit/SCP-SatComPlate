# TaskTrace 输入

`workload/` 只存放任务输入，不存放独立网络传输。程序仅在同时指定
`--computeProfile` 与 `--taskTrace` 时读取任务。

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

根对象只允许 `tasks`，每项只允许示例中的八个字段。三个业务量必须为正整数，
到达时间必须早于仿真结束。`output_bytes` 是计算完成后实际发送的结果大小。

- `workload/stress-40.json`：与 22 节点算力配置配套的 40 任务输入；
- `workload/size-aware-60.json`：与全节点算力配置配套的 60 任务输入。

小型输入校验和集成测试数据位于 `tests/fixtures/task/`。
