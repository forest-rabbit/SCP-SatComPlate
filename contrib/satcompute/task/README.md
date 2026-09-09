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

N4C 的 `task_profile` 只接受 `dense-image`、`sparse-inference`、`compression`、`llm`；
旧输入缺省时明确记为 `UNSPECIFIED`，不猜测类别。正式 C800 使用统一100,000 WU/s；
旧功能示例的 ComputeProfile 不改。输入和 S/W/K/RESULT、rho/sigma/H 合同见
[正式任务生成器](../tools/generation/README.md)；当前默认是已冻结的1300秒场景。

`computeDeadlineFactor` 默认1.3，有限且至少为1。预算为参考服务时间乘倍率后向上取整到ns；
正式四类参考速率为100,000 WU/s，legacy任务沿用其输入节点速率。
绝对deadline只在首次RUNNING时建立，不包含初始INPUT/排队或RESULT传输，不重置。
超时未算完立即以 `COMPUTE_DEADLINE_EXCEEDED` 终止，释放计算占用；同ns完成优先。
任务成功必须同时按时算完并完整送达RESULT，已算完的RESULT不受compute deadline影响。
超时不使卫星故障、不改变FCFS排序；busy time包含失败前已执行及仿真截断前的计算时间。

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
完成数，但取消前实际执行时间仍计入 busy time；节点恢复后只调度队列中仍合法的任务。

compute 故障开始时，`TaskCoordinator` 按当前阶段处理目标节点任务：

| 当前状态 | 处理 |
|---|---|
| `PENDING` / 停机期间新到达 | 整星端点存活时照常启动 INPUT |
| `INPUT_TRANSFERRING` | INPUT 继续，收齐后可在停机期间入队 |
| `QUEUED` | 保留原 FCFS 队列，恢复后继续调度，不建立首次计算 deadline |
| `RUNNING` | 取消 completion event，任务失败，保留已完成 INPUT，取消 RESULT |
| `RESULT_TRANSFERRING` / `COMPLETED` | 计算阶段已越过，不受 compute 故障影响 |

F1/F2 是临时计算服务停机，假设输入数据和队列保留；恢复后继续原 FCFS 调度。
已被打断的 RUNNING 任务仍为 `FAILED`，不自动恢复、重计算、迁移或创建新 attempt。
停机造成的排队受阻单独记录，不直接等同于额外增加整个停机时长；初始等待不消耗
compute deadline，结束时未完成仍属于截断。F3 永久整星失效不适用队列保留规则。

整星故障还检查任务当前仍需要的三个端点：

- INPUT 未完成时，source/compute 故障令活动 INPUT 分别以 source/destination 原因
  `FAILED`；result 故障令不再需要的 INPUT `CANCELLED`；
- INPUT 完成后，原 source 不再是必要资源，单独故障不影响 QUEUED/RUNNING/RESULT；
- compute 卫星故障会终止 QUEUED/RUNNING，RESULT 已开始时则使其 source 失败；
- result 卫星故障会终止尚未完成的任务，活动 RESULT 以 destination 原因失败；
- 已完成任务不受影响，故障期间到达的任务立即失败，恢复后只有新任务可运行。

同一时刻多个整星端点同时故障时，活动网络阶段优先保留真实 source/destination
失败证据；任务和 transfer 仍只终止一次。

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
- `tests/unit/compute-fault-execution-test.cc`：compute 预警/开始/恢复、各任务阶段、
  同刻排序和两次运行确定性；
- `tests/unit/satellite-fault-execution-test.cc`：整星端点语义、恢复与中间路径重准入；
- `tests/integration/smoke/run-task-smoke.sh`：单任务完整闭环；
- `tests/integration/regression/run-full-workload-regression.sh`：确定性、完成策略、诊断
  和 20 任务正式示例；
- `tests/integration/regression/run-fault-lifecycle-regression.sh`：故障终态输出、重复
  运行确定性与无故障回归；
- `metrics/README.md`：`task-events.csv`、`task-summary.csv` 与
  `compute-node-summary.csv`；其中 task summary 明确记录 `final_state`、
  `failure_reason` 和 `failure_time_ns`。
