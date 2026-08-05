# 任务与算力模块

`task/` 把两个真实 UDP 传输和一次确定性计算服务连接成完整任务。它读取独立的
ComputeProfile 与 TaskTrace，不根据网络拓扑生成任务，也不执行真实业务算法。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `compute-profile.h/.cc` | 读取、校验并按稳定卫星 ID 排序静态算力 |
| `task-trace.h/.cc` | 读取、校验并按 task ID 排序任务，派生两条 transfer ID |
| `compute-task.h/.cc` | 线性任务状态机、时间戳和转换不变量 |
| `compute-service.h/.cc` | 每颗计算卫星上的单服务台、非抢占 FCFS queue |
| `task-coordinator.h/.cc` | 调度任务到达，连接输入传输、计算和结果传输回调 |

JSON 的完整字段合同和正式输入文件见
[`input/topology/`](../input/topology/README.md) 与
[`input/traffic/`](../input/traffic/README.md)。两条 CLI 路径必须成对提供：

```bash
--computeProfile=<compute-profile.json> --taskTrace=<task-trace.json>
```

## 任务生命周期

每个任务严格经过五次状态转换：

```text
PENDING
  -> INPUT_TRANSFERRING
  -> QUEUED
  -> RUNNING
  -> RESULT_TRANSFERRING
  -> COMPLETED
```

任一非终态还可以进入 `FAILED`，并固定记录失败时刻与原因。`FAILED` 和
`COMPLETED` 都不可恢复；当前阶段不创建迁移、备份或新的执行 attempt。

流程如下：

1. 在 `arrival_time_ns` 启动源卫星到计算卫星的输入传输；
2. receiver 收齐 `input_bytes` 后进入计算节点队列；
3. FCFS service 调度任务并运行精确整数纳秒服务时间；
4. 计算完成后，从计算卫星向结果卫星发送 `output_bytes`；
5. receiver 收齐结果后标记任务完成。

所有转换时间必须单调不减，并且必须等于当前 ns-3 仿真时刻。完整任务应产生恰好
五条 `TaskEventRecord`。

## FCFS 计算模型

每个 ComputeProfile 节点创建一个 `ComputeService`。队列排序键是：

```text
(queue_enter_time_ns, task_id)
```

因此同一纳秒进入同一节点的任务按 task ID 确定性排序。服务不可抢占，一个节点
同一时刻最多运行一个任务。服务时间使用向上取整：

```text
service_time_ns = ceil(
    compute_work_units * 1,000,000,000
    / compute_rate_work_units_per_second
)
```

实现使用 128-bit 中间值检查乘法和纳秒范围，正计算量的最短服务时间为 1 ns。
节点利用率、busy time 和最大队长由运行时 service 统计，而不是输入估算。

`ComputeService` 还提供计算可用性开关，以及精确取消 running task、移除 queued
task 的幂等接口。被取消的运行任务不会触发原 completion event，也不会计入正常
完成数或成功计算 busy time；节点恢复后只调度队列中仍合法的任务。故障输入与这些
接口的调度连接在后续 N4A 小步完成。

## 结果大小与传输 ID

平台不知道算法的压缩率或输出形状，因此不会用输入大小或计算量推导结果大小。
计算完成后的实际结果传输严格使用 TaskTrace 的 `output_bytes`。

任务 ID `T` 派生：

```text
input_transfer_id  = 2*T - 1
result_transfer_id = 2*T
```

这要求 `task_id` 位于 `1..UINT64_MAX/2`。输入数组顺序不会影响派生 ID；reader 会
按 task ID canonical 排序。

## 完成与部分完成

`TaskCoordinator::IsComplete()` 同时要求所有任务进入 `COMPLETED` 且全部传输由
receiver 完整接收。仿真结束时：

- `taskCompletionPolicy=strict`：先写出指标，再以退出码 3 报告部分完成；
- `taskCompletionPolicy=report`：写出相同部分结果，但进程返回 0；
- `diagnosticMode=failure`：为部分完成额外写出失败证据。

## 对应测试与输出

- `tests/unit/task-input-test.cc`：closed-world 输入、canonical 排序和派生 ID；
- `tests/unit/compute-service-test.cc`：服务时间、FCFS 和同刻 tie-break；
- `tests/unit/fault-lifecycle-test.cc`：任务失败终态、传输终止和资源释放；
- `tests/integration/smoke/run-task-smoke.sh`：单任务完整闭环；
- `tests/integration/regression/run-full-workload-regression.sh`：确定性、完成策略、诊断
  和 20 任务正式示例；
- `metrics/README.md`：`task-events.csv`、`task-summary.csv` 与
  `compute-node-summary.csv`。
