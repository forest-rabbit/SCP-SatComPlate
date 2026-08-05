# 任务内部传输模块

`traffic/` 只执行任务的输入传输与结果传输，不提供独立 NetworkTransfer workload，
也没有 `--transferTrace`。所有 transfer plan 都由 `TaskCoordinator` 从 TaskTrace
确定性派生。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `network-transfer-config.h/.cc` | transfer 运行记录、稳定五元组和分包策略 |
| `network-transfer-application.h/.cc` | UDP sender、逐包发送与 serialization pacing |
| `network-transfer-receiver.h/.cc` | 按稳定四元组聚合接收字节、完成回调和 socket Drop |
| `network-transfer-engine.h/.cc` | plan 注册、应用生命周期、capacity 准入与拓扑更新 |
| `network-transfer-records.h/.cc` | transfer 状态、终止原因及指标层消费的记录 |

## 稳定 flow

plan 按 transfer ID 升序注册。每个目的卫星复用一个 UDP receiver，固定目的端口为
9000；同一源卫星的 source port 从 10000 起按 transfer ID 顺序递增。flow key 为：

```text
(source_service_ipv4, destination_service_ipv4, UDP,
 source_port, destination_port)
```

每个 transfer 的声明大小必须为正，源和目的卫星必须不同。输入 transfer 在任务
到达时启动；结果 transfer 的启动时刻由计算完成事件决定。

## 分包

`fixed` 模式始终使用 `transferPayloadBytes`。`size-aware` 根据整条 transfer 的声明
大小选择 payload：

| transfer 大小 | payload |
|---:|---:|
| `<= 1 MiB` | 1024 byte |
| `> 1 MiB` 且 `<= 64 MiB` | 8192 byte |
| `> 64 MiB` | 64000 byte |

packet 数量为 `ceil(size_bytes / payload_bytes)`，最后一包只承载剩余字节。IPv4/UDP
头共 28 byte，因此 payload 加 28 必须不超过 `islMtuBytes`。

## pacing 与路由

sender 每发送一包后，根据该包的 payload、UDP/IPv4/PPP 头和当前速率安排下一次
发送：

```text
serialization_time = wire_bits / pacing_rate
```

- 普通模式使用当前选定第一跳 PointToPoint device 的 data rate；
- capacity-aware 使用 `min(first_hop_rate, admitted_path_bottleneck_rate)`；
- capacity 路径在拓扑更新后失效时，sender 立即暂停，释放旧路径并在重新准入后
  使用新瓶颈速率继续发送。

UDP 模型不虚构 ACK、重传或可靠恢复。sender 完成只表示全部 payload 已交给 socket；
transfer 完成必须由 receiver 收齐声明字节。链路/队列丢包可能使任务在仿真终点仍
未完成，此时由完成策略和失败诊断如实报告。

## reservation 生命周期

- size-aware 在活动 flow 的每个节点保存声明字节 assignment，sender 完成发送时
  释放；
- capacity-aware 在完整 ECMP 最短路径的每条有向边预留 admitted rate，receiver
  完整接收或路径失效时释放；
- 没有正剩余容量的 capacity-aware transfer 保持 pending，并按确定性顺序重试。

## 故障安全终态

内部状态明确区分 `REGISTERED`、`WAITING_ADMISSION`、`ACTIVE`、
`PAUSED_ROUTE`、`SENDER_FINISHED`，以及 `COMPLETED`、`FAILED`、
`CANCELLED` 三种终态。本阶段没有 `SUPERSEDED`；该状态只应在后续确实创建备份或
恢复实例时加入。

`FinalizeTransferIfActive()` 是唯一终止入口。首次调用会停止 sender、清除正常完成
回调、隔离接收端未完成数据、移除 pending admission，并幂等释放完整路径、逐跳
assignment 和路由缓存；重复调用返回 `false`，不二次释放。已发送/接收字节、迟到
包计数、容量等待时间、终止时刻和原因仍保留为实验历史。终止后的迟到包只计为
stale，不会重新完成旧 transfer，也不会影响同一 receiver 上的其他 transfer。

算法和公式见 [`routing/README.md`](../routing/README.md)。

## 对应测试与输出

- `tests/integration/smoke/run-task-smoke.sh` 检查两次传输的任务闭环；
- `tests/integration/smoke/run-capacity-aware-smoke.sh` 检查完整路径准入与释放；
- `tests/integration/smoke/run-diagnostics-smoke.sh` 检查真实 UDP/queue Drop；
- `tests/unit/fault-lifecycle-test.cc` 直接检查终止幂等性和三类 reservation 清理；
- `transfer-summary.csv` 记录声明大小、分包、发送/接收字节和完成时间，详见
  [`metrics/README.md`](../metrics/README.md)。
