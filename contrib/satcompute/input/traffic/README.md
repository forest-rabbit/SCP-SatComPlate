# NetworkTransfer 与 TaskTrace JSON 输入

SatCompute 当前支持的 JSON 业务输入和测试夹具按用途分开：

```text
workload/  NetworkTransfer 正式和本地压力输入
../../tests/fixtures/traffic/transfers/  NetworkTransfer 小型回归输入
../../tests/fixtures/task/               ComputeProfile 与 TaskTrace 小型回归输入
```

程序只读取命令行显式指定的输入，不会自动加载本目录下的其他 JSON。JSON
记录描述源、目的、应用字节数和到达时间；分包模式、MTU 和 UDP 端口由运行
参数及程序确定性派生。NetworkTransfer 不设置人工应用发送速率，每包按当前
选定首跳的链路序列化时间调度。

旧版 traffic-matrix 倍率、TCP OnOff 和 legacy UDP 聚合能力已作为批准的
范围收缩退出项目，不迁移到 JSON。这里的 CSV 退出只涉及 SatCompute 自有
业务输入，不影响 ns-3 上游 CSV helper，也不改变 JSON 拓扑快照格式。

## NetworkTransfer workload

- `workload-5000-varied.json`：5000 条不同大小的规模输入；CI 只做快速合同解析，
  不执行完整压力仿真；
- `mixed-large-local.json`：10 条 128 MiB–1 GiB 的本地完整压力输入，不在每次
  CI 中运行。

## NetworkTransfer test fixtures

以下文件集中位于
[`tests/fixtures/traffic/transfers/`](../../tests/fixtures/traffic/transfers/)：

- `canonical-order-a.json`、`canonical-order-b.json`：验证记录顺序规范化和余数
  包；
- `engine-basic.json`：验证基础 UDP 分包、收包和完成状态；
- `capacity-pending.json`：验证 capacity-aware 等待准入；
- `platform-partial.json`：验证 strict/report 的 partial 运行；
- `canonical-order-a.json`、`canonical-order-b.json`：验证记录顺序规范化；
- `invalid-*.json`：验证 closed-world、唯一 ID、稳定端点与停止时间约束。

这些文件由 `--transferTrace=<file>` 读取，每条记录直接声明一次网络传输。其
closed-world 字段为 `transfer_id`、`source_node_id`、
`destination_node_id`、`size_bytes` 和 `arrival_time_ns`。

## TaskTrace

任务到达属于 traffic side，通过 `--taskTrace=<file>` 显式读取：

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

[`tests/fixtures/task/`](../../tests/fixtures/task/) 包含单任务、FCFS、异构算力、
TaskTrace/ComputeProfile 换序输入和 closed-world 失败用例。
静态算力不是流量，单独位于
[`../topology/resources/`](../topology/resources/)。
`--computeProfile` 与 `--taskTrace` 必须同时指定，并与 `--transferTrace`
互斥。

快速输入进入项目回归；完整本地压力输入只按需运行。只有当某项既有行为被
明确废弃且已有替代验证时，才应同时删除其输入和检查代码。
