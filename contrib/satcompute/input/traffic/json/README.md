# NetworkTransfer 与 TaskTrace JSON 输入

程序只读取命令行显式指定的输入，不会自动加载本目录下的其他 JSON。
`workload/` 和 `test/` 保存独立 NetworkTransfer，`task/` 保存任务到达。

## NetworkTransfer workload

- `workload-5000-varied.json`：5000 条不同大小的规模输入，同时作为 CI 的规模
  回归；
- `mixed-large-local.json`：10 条 128 MiB–1 GiB 的本地完整压力输入，不在每次
  CI 中运行。

## NetworkTransfer test

- `canonical-order-a.json`、`canonical-order-b.json`：验证记录顺序规范化和余数
  包；
- `diamond-4-static-transfers.json`：验证静态 ECMP 双路径与重复确定性；
- `diamond-4-dynamic-transfers.json`：验证链路变化后的 `2 → 1 → 2` 路由候选；
- `diamond-4-hrw-dynamic-transfers.json`：四条 flow 跨越候选不变、删除和恢复
  的四个 route epoch，验证 HRW 最小迁移语义；
- `size-aware-static-transfers.json`：八条同时到达、大小不同的 flow，验证
  HRW top-2 物理下一跳预留和发送完成释放；
- `size-aware-dynamic-transfers.json`：验证 size-aware sticky、候选失效重选、
  恢复后旧 flow 不迁回和新 flow 使用恢复候选；
- `mixed-large-ci.json`：验证至少 10 条大流量以及全部 size-aware 分包档位；
- `fqcodel-bottleneck-transfers.json`：两个高速入口汇入低速出口，确定性触发
  默认 FqCoDel `QUEUE_DISC` 丢弃。
- `n1-75-fqcodel-replay.json`：三条 N1 目标流及 38 条 1-byte source-port
  占位流，用于比较旧 hash、纯 HRW 与大小感知 HRW；占位流不参与竞争窗口。

这些文件由 `--transferTrace=<file>` 读取，每条记录直接声明一次网络传输。其
closed-world 字段为 `transfer_id`、`source_node_id`、
`destination_node_id`、`size_bytes` 和 `arrival_time_ns`。

## TaskTrace

任务到达属于 traffic side，统一放在 `task/`，通过 `--taskTrace=<file>` 读取：

```json
{
  "schema_version": "0.1",
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

根对象只允许 `schema_version` 和 `tasks`；每项只允许示例中的八个字段。
ID、字节数、计算量和到达时间都是整数；数据量与计算量必须大于 0。
`source_node_id`、`compute_node_id` 和 `result_node_id` 必须引用拓扑卫星，
其中 `compute_node_id` 还必须引用本次 `--computeProfile` 中的节点。任务按
`task_id` canonical sort，所以数组排列不影响执行和结构化输出。

`task/test/` 包含单任务 ECMP、三任务 FCFS、异构算力、TaskTrace 换序输入，
以及不进入每次 CI 的 60-task size-aware 中型碰撞输入及生成摘要。
静态算力不是流量，单独位于
[`../../topology/json/resources/`](../../topology/json/resources/)。
`--computeProfile` 与 `--taskTrace` 必须同时指定，并与 `--transferTrace` 或正的
`--offeredLoad` 互斥。

进入 N1 后继续保留这些 N0 输入和
`tools/validation/check-ecmp-output.py`，用于确认任务计算与调度没有破坏
N0 网络传输基线；`tools/validation/check-task-output.py` 验证完整任务闭环。
快速用例继续进入每次
CI，完整本地压力输入按需运行。只有当某项 N0 行为被明确废弃且已有替代验证时，
才应同时删除其输入和检查代码。
