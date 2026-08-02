# N2 严格 Capacity-aware HRW 66 星压力验证

## 1. 结论

本轮在 66 星、66 个计算节点和同一份冻结 75% workload 上，将
`global-size-aware-hrw` 替换为严格的
`global-capacity-aware-hrw`。唯一一次 1000 s 正式运行正常退出，完成
1500/1500 个任务和 3000/3000 条 transfer，6,584,966 个 UDP 数据报全部
接收，所有已采集丢包类别均为 0。

该结果证明严格的整路径容量准入能够消除本场景原有的竞争丢包，但不证明它是
动态拓扑下的最终路由方案。运行期间 51 份快照的 121 条无向 ISL 和带宽均
保持不变；没有链路故障、节点故障、checkpoint、backup 或任务迁移。

## 2. 版本与输入合同

```text
simulation implementation SHA:
  83fc6120a6b50d63e2ce5db172fa122aa6634b66

generic checker closure commit:
  78bc3d1

routingMode:             global-capacity-aware-hrw
pacingMode:              path-bottleneck-serialization
ecmpHashSeed:            1
transferChunkMode:       size-aware
simulationDuration:      1000 s
snapshot interval:       20 s
satellites:              66
compute nodes:           66
undirected ISLs:         121
link bandwidth:          2,000,000 Kbps = 2 Gbps
one-way delay:           8,000 us
islMtuBytes:             65,535
islQueueBytes:           64,000,000 per directed queue
receiverRcvBufBytes:     131,072
taskCompletionPolicy:    report
diagnosticMode:          failure
```

核心物理输入哈希与先前 66 星压力基线一致：

| 输入 | SHA-256 |
|---|---|
| topology aggregate | `145c6360589a8c8d720a2be577ea7c58202059936e7a640dbe3296905dd3acf0` |
| ComputeProfile | `c1ddfcec15489077a968b237c419568ab6f55fe7144280aa3782f0f4a1637a7e` |
| TaskTrace | `b2fc74f11b2074c7f29772464cf53877b6946f9905677f8c43f21c5740708237` |
| workload summary | `2a82521eec2f2ee2ac11666480df02344153da521cd7c2e5a8bed27bdcf43eb3` |

当前版本生成的 scenario aggregate 为
`5b68de68de64a7e1a1beba8b02aa78a282d465109f0b5441c0914fbdbd22ff5e`。
scenario manifest 包含代码和工具 provenance，因此它不作为跨实现版本的物理
拓扑等价依据；拓扑数据、ComputeProfile 和 workload 哈希才是本轮对照合同。

## 3. Preflight 与小型门禁

Preflight 精确复现：

```text
tasks:                    1,500
INPUT bytes:             81,750,000,000
RESULT bytes:            27,513,294,080
total application bytes: 109,263,294,080
derived UDP datagrams:        6,584,966
estimated packet-hops:       31,774,327
duration lower bound:           653.907 s
directed queues:                      242
theoretical queue cap:             14.424 GiB, not preallocated
```

正式运行前，从冻结 TaskTrace 选择最早到达的 10 个真实任务，在跨越 20 s
拓扑更新时间的 25 s 场景中完成 10/10 tasks、20/20 transfers，
47,145/47,145 个包全部接收且零丢包；容量预留账本最终归零。

## 4. 正式结果

| 指标 | 结果 |
|---|---:|
| run status | `COMPLETE` |
| completed tasks | 1500/1500（100%） |
| completed transfers | 3000/3000 |
| sent / received application bytes | 109,263,294,080 / 109,263,294,080 |
| FlowMonitor tx / rx / lost | 6,584,966 / 6,584,966 / 0 |
| QueueDisc / device queue / UDP / unattributed drops | 0 / 0 / 0 / 0 |
| mean task completion delay | 2.024577267 s |
| median / p95 / p99 | 1.852272160 / 3.747923600 / 6.498724387 s |
| max task completion delay | 11.216028134 s |
| last task arrival | 599.609838294 s |
| last task completion | 602.150041765 s |
| drain margin | 397.849958235 s |
| transfers with admission wait | 23/3000 |
| maximum admission wait | 3.193558127 s |
| wall clock | 56:13.99 |
| peak RSS | 117,844 KB |
| process exit | 0 |

预留审计：

```text
registered flows:                    3000
reservation events:                43545
assign events:                     14488
sticky-reuse events:               14569
transfer-completed releases:       14488
candidate-invalid releases:            0
active flows at end:                   0
assignments at end:                    0
reserved bytes at end:                 0
```

通用 task checker 在识别
`path-bottleneck-serialization` 后返回：

```text
RUN_VALID: status=COMPLETE completed=1500/1500
completion_rate=100% threshold=90% decision=CONTINUE
```

43,545 条 reservation event 也完成逐行账本重放，before/after 值、候选聚合
和最终归零均一致。成功运行没有保留 `diagnostics/failure/`。

## 5. 与原 66 星 Size-aware 基线比较

| 指标 | Size-aware HRW | 严格 Capacity-aware HRW |
|---|---:|---:|
| completed tasks | 1495/1500 | 1500/1500 |
| completed transfers | 2990/3000 | 3000/3000 |
| lost packets | 174 | 0 |
| mean completed-task delay | 2.016012 s | 2.024577267 s |
| max completed-task delay | 11.323713 s | 11.216028134 s |
| elapsed | 56:13.04 | 56:13.99 |
| peak RSS | 112,688 KB | 117,844 KB |

两列平均时延对应的完成任务集合不同，不能把约 8.6 ms 的差异解释为严格的
逐任务性能回归。新模式补全了基线中未完成的 5 个任务，同时墙钟基本不变。

## 6. 解释边界

- 每个模式只有一次确定性正式运行，没有多 seed 或统计置信区间；
- 这是 1500-task 的代表性高负载验证，不是容量上限测试；
- 351 星和 720 星没有启动；
- 当前严格模式为活动 flow 固定完整路径，并将候选失效视为不支持的合同错误；
- 本结果适合作为静态零丢包对照，不直接证明动态故障适应性；
- 场景、TaskTrace、原始 CSV/JSON 指标和日志均保留在 `/tmp`，未提交仓库。
