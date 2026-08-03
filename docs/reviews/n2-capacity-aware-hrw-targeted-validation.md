# N2 容量感知 HRW：针对性验证

## 1. 结论

本轮新增 opt-in `global-capacity-aware-hrw`，保留 N1 的
`global-size-aware-hrw` 作为不变基线。新模式仍使用 ns-3 全局路由产生的
等价最短路径，但把逐跳局部选择提升为完整路径选择，并增加路径剩余带宽
预留、路径瓶颈速率 pacing 和容量不足时的延迟注入。

小型汇聚瓶颈对照中，旧模式完成 `0/2` transfer 并产生 9075 个
`QUEUE_DISC` Drop；新模式两次确定性重放均完成 `2/2` transfer，
`17144/17144` 个 UDP datagram 全部接收且零丢包。独立 diamond 场景同时
使用两条空闲 ECMP 路径，证明实现没有把所有 flow 全局串行化。

这是针对性功能验证，不是 66/351 星 75% workload 的重新压力运行，也不把
小型场景的零丢包外推为任意负载保证。

## 2. 冻结行为

对每条待启动 flow，控制器只遍历当前目的地址的等价最短下一跳：

```text
residual(link) = configured_data_rate(link) - active_admitted_rate(link)
path_rate      = min(residual(link) for link in path)
```

- 选择 `path_rate` 最大的完整路径，相同时保留确定性 HRW 顺序；
- 为路径上的每条有向 ISL 预留 `path_rate`；
- 预先固定每个可选择节点的完整 route candidate；
- 发送端以 `path_rate` 计算包含 UDP/IP/PPP 头的包间隔；
- 没有正剩余带宽的路径时保留原 arrival time，并按到达顺序等待；
- 目的节点完整接收 flow 后才释放路径，随后立即重试等待 flow。

反向链路独立计量；不共享有向 ISL 的 flow 可以并行。模式没有读取瞬时
FqCoDel backlog，也没有修改 ns-3 SPF、`Ipv4GlobalRouting` 上游代码或其他
路由模式的选择逻辑。

## 3. 汇聚瓶颈对照

使用仓库既有 `fqcodel-bottleneck` fixture：两个 1 Gbps 入口分别从卫星 0、1
汇聚到卫星 2，再共享 `2→3` 的 10 Mbps 有向瓶颈。两个 12,000,000-byte
transfer 都在 0.1 s 到达，固定 payload 为 1400 bytes，仿真 25 s。

| 指标 | `global-size-aware-hrw` | `global-capacity-aware-hrw` |
|---|---:|---:|
| 完成 transfer | 0/2 | 2/2 |
| tx / rx datagram | 17144 / 8069 | 17144 / 17144 |
| FlowMonitor lost | 9075 | 0 |
| `QUEUE_DISC` Drop | 9075 | 0 |
| device queue / UDP socket / unattributed | 0 / 0 / 0 | 0 / 0 / 0 |
| pacing | first-hop serialization | path-bottleneck serialization |

新模式中 transfer 1 的首包仍在 `0.100000000 s` 注入，并在
`9.905939440 s` 完整到达。transfer 2 的首包恰在该时刻注入，最终在
`19.711878880 s` 完整到达。这直接验证了等待发生在应用注入前，而不是依靠
队列丢包或延长仿真时间掩盖失败。

两次新模式运行的以下文件逐字节一致：

```text
network-flow-details.csv
network-flow-metrics.csv
transfer-summary.csv
ecmp-route-events.csv
size-aware-reservation-events.csv
size-aware-summary.json
diagnostics/failure/flow-drop-reasons.csv
```

`run-summary.json` 除真实 wall-clock 字段外一致。

## 4. ECMP 并行性

既有四节点 diamond fixture 提供两条等价的两跳路径。8 条 flow 全部在 0.1 s
到达；前两条 flow 在同一时刻注入，并选择两个不同 source output interface。
场景最终完成 `8/8` transfer、`60/60` datagram、零丢包，证明准入粒度是有向
链路容量而不是全局互斥锁。

同一 diamond 上的单任务 fixture 也完成 `1/1` task 和 `2/2` INPUT/RESULT
transfer，7 个 datagram 全部接收。该门禁覆盖了 TaskCoordinator 回调、计算后
RESULT 再准入以及接收完成时释放路径的完整生命周期。

另一个 2 s 运行在 1 s 应用相同边集合的重排快照。两条已准入 flow 跨越
route epoch 0→1 后继续使用 `CAPACITY_AWARE_STICKY`，两条尚未准入 flow 以
FlowMonitor ID 0、零发送记录保留，运行稳定报告 `PARTIAL` 而不是因缺少
five-tuple 指标中止。

## 5. 回归与复现

本地通过：

```bash
./waf build
contrib/satcompute/tests/integration/smoke/run-routing-smoke.sh
contrib/satcompute/tests/integration/smoke/run-capacity-aware-smoke.sh
```

现有 routing smoke 继续通过静态 Hash、动态 Hash、稳定 HRW 和 size-aware HRW
的确定性合同。新 runner 已加入 Fast Smoke 和 Full Regression，在 PR 上提供
持续门禁。

## 6. 明确边界

- 只在 ns-3 已计算的等价最短路径内选择，不是 KSP，也不使用 `+1/+2` hop；
- 首版仅承诺 flow 活动期间 ISL 边集合和带宽不变；相同边集合的 route epoch
  更新可继续复用路径，候选失效则明确中止；
- 没有 ACK/NACK、重传、动态速率提升、max-min fairness 或全局流量工程；
- 只有本针对性 fixture 得到零丢包结论，尚未重新运行 66/351 星 75% workload；
- 压力基线报告仍由独立 Draft PR #19 保存，本分支不改写其历史事实。
