# N2 75% 代表性压力验证（66 / 351 星）

## 1. 结论

本轮在 `991211c4fd90d4d76451a107095c5e91a79ec41d` 上使用同一组
1500-task、109,263,294,080-byte 应用层工作负载，完成了 66 星和 351 星
两次正式运行。两次进程均正常退出，外部 checker 均返回
`RUN_VALID / CONTINUE`，任务完成率分别为 99.6667% 和 99.2667%。

两次运行均为 `PARTIAL`：未完成对象来自 FqCoDel `QUEUE_DISC` 丢包，
没有 device queue Drop、UDP socket Drop、未归因丢包、崩溃或指标合同错误。
因此结果满足既定的 90% 继续门槛，但不应描述为零丢包或全部任务完成。

720 星只完成场景生成、preflight 和 10-task smoke；根据后续人工决定，未启动
1500-task 正式运行。本报告因此是两种已运行规模的真实结论，不声称完成原计划
中的三规模正式矩阵。

## 2. 版本与环境

- 基线分支：`feature/n2-integration`
- 基线与运行 SHA：`991211c4fd90d4d76451a107095c5e91a79ec41d`
- PR #18 merge SHA：`991211c4fd90d4d76451a107095c5e91a79ec41d`
- PR #18 合并后 Full Regression：
  [success](https://github.com/forest-rabbit/SatCompute/actions/runs/30631733111)
- 构建：ns-3 debug profile，`--disable-examples --disable-tests
  --enable-modules=satcompute`
- 主机：AMD Ryzen 7 5800H，8 cores / 16 logical CPUs，15 GiB RAM，
  4 GiB swap，WSL2
- 正式运行次数：每个已运行规模一次，`ecmpHashSeed=1`

本轮没有修改生产 C++、`wscript`、fixture 或运行参数。生成场景、工作负载和
输出均写入 `/tmp/satcompute-n2-pressure-75.8na1P4`，未写入仓库或外部实验
目录。该临时目录随后按 `/tmp` 生命周期清理；本报告只保留运行前已冻结的
哈希、命令合同和经 checker 核验的紧凑结论。

## 3. 冻结合同

共同参数：

```text
routingMode=global-size-aware-hrw
ecmpHashSeed=1
transferChunkMode=size-aware
pacingMode=first-hop-serialization
link_bandwidth=2,000,000 Kbps (2 Gbps)
delay=8,000 us one-way
islMtuBytes=65,535
islQueueBytes=64,000,000 per directed device queue
receiverRcvBufBytes=131,072
taskCompletionPolicy=report
diagnosticMode=failure
snapshot interval=20 s
topology=plus-grid, seam disabled, fixed delay
```

| 规模 | 星座 | 计算节点 | 实际到达范围 | 仿真 | 快照 | 无向 ISL |
|---|---:|---:|---:|---:|---:|---:|
| 66 | 6 × 11 | 66 | 1.018272–599.609838 s | 1000 s | 51 | 121 |
| 351 | 27 × 13 | 117 | 0.612598–339.952008 s | 600 s | 31 | 689 |
| 720 | 18 × 40 | 240 | 0.310607–164.916552 s | 300 s | 16 | 1400 |

351 星计算节点的每轨分布为 `[4,4,5] × 9`；720 星为
`[13,13,14] × 6`。三种规模的任务 ID、输入/输出大小、计算工作量和到达顺序
一致；到达窗口按规模重新生成。使用现有 workload generator 后，source
节点任务数范围分别为 21–25、3–6 和 1–3，没有额外引入自定义映射器来强制
每节点计数差不超过 1。这一点限制了严格的节点级公平对比，但不改变本轮
平台压力验证的输入总量。

共同工作负载：

```text
tasks                         1,500
INPUT bytes              81,750,000,000
RESULT bytes             27,513,294,080
total application bytes 109,263,294,080
derived UDP datagrams         6,584,966
1 GB / 500 MB tails               15 / 30
generator seed                    20260726
rules version                     n1.6-v1
```

## 4. 输入哈希

| 规模 | scenario aggregate | topology aggregate | ComputeProfile |
|---|---|---|---|
| 66 | `2c09c071466472fb097a8592a8f4551c90ae6a3956d903a095101de7f3be3088` | `145c6360589a8c8d720a2be577ea7c58202059936e7a640dbe3296905dd3acf0` | `c1ddfcec15489077a968b237c419568ab6f55fe7144280aa3782f0f4a1637a7e` |
| 351 | `55c308b75e70dc4ef3c34914430e1c15963fc535e6aa1e5929a90d24bf9ce3a6` | `9e323dd39d020029a4bbd51956e5698740ec7260cfce5ed811d1215eba86a46d` | `f980307fd6ac313581e0a7eae8ca48de6123951ef21040d89ca320399527bfa0` |
| 720 | `f2362dae101ca2f3edb068d217c4f19f487a33aa2e17169333dfb1a97bdc9e2f` | `abbd214166150c71e9b22f9c3f1452ef845c10a0c87a8320a9e3487c556f405d` | `47045e8eb7acfdddb1d580561c081cf5544a8e9f280b006868fc8103701c8544` |

| 规模 | TaskTrace | workload summary |
|---|---|---|
| 66 | `b2fc74f11b2074c7f29772464cf53877b6946f9905677f8c43f21c5740708237` | `2a82521eec2f2ee2ac11666480df02344153da521cd7c2e5a8bed27bdcf43eb3` |
| 351 | `28f06cfd5da3cffda7d2116683b9ee2960fe80879fbe4e6c5eb9709f15d4678b` | `f47a3b2b14220690a633ce1b71d55aebae14f1000a8470a5318a5d52227acb59` |
| 720 | `8a04b5e0538d66ee6745fca25703b827dad9c5497289a5675a64009df9df91a6` | `94aadcf72e4e14457d058bd9d54afba7fada05b02fc0dbbbb5ae3ceb1b201c95` |

66 星 TaskTrace 和 summary 哈希精确复现 N1 冻结的 75% corpus。

## 5. Preflight 与小型门禁

| 规模 | 预计 packet-hops | duration lower bound | 有向队列 | 理论队列总上限 |
|---|---:|---:|---:|---:|
| 66 | 31,774,327 | 653.907 s | 242 | 14.424 GiB |
| 351 | 80,748,310 | 374.826 s | 1378 | 82.135 GiB |
| 720 | 104,703,477 | 189.490 s | 2800 | 166.893 GiB |

这些队列上限不会在启动时一次性预分配。三种规模均通过 scenario checker、
preflight 和跨越 20 s 拓扑更新时间的 10-task smoke；每个 smoke 都完成
10/10 tasks、20/20 transfers，13,549 个包全部接收且零丢包。

## 6. 正式结果

| 指标 | 66 星 | 351 星 |
|---|---:|---:|
| 状态 | PARTIAL | PARTIAL |
| 完成任务 | 1495/1500（99.6667%） | 1489/1500（99.2667%） |
| 完成 transfer | 2990/3000 | 2978/3000 |
| FlowMonitor tx | 6,569,034 | 6,543,049 |
| FlowMonitor rx | 6,568,860 | 6,541,910 |
| FlowMonitor lost | 174 | 1139 |
| `QUEUE_DISC` Drop | 174 | 1139 |
| device queue / UDP socket / unattributed Drop | 0 / 0 / 0 | 0 / 0 / 0 |
| sent application bytes | 108,937,742,102 | 107,722,619,372 |
| received application bytes | 108,926,606,102 | 107,649,723,372 |
| mean completed-task delay | 2.016012 s | 2.126280 s |
| max completed-task delay | 11.323713 s | 13.543023 s |
| ns-3 wall clock | 3372.146 s | 8727.068 s |
| `/usr/bin/time` elapsed | 56:13.04 | 2:25:28 |
| peak RSS | 112,688 KB | 393,988 KB |
| process exit | 0 | 0 |

Checker 结论：

```text
66:
PASS: incomplete run preserved report diagnostics
      (5 tasks, 10 transfers, 0 directed-queue drops, 0 UDP socket drops)
RUN_VALID: status=PARTIAL completed=1495/1500
           completion_rate=99.6666666667% threshold=90% decision=CONTINUE

351:
PASS: incomplete run preserved report diagnostics
      (11 tasks, 22 transfers, 0 directed-queue drops, 0 UDP socket drops)
RUN_VALID: status=PARTIAL completed=1489/1500
           completion_rate=99.2666666667% threshold=90% decision=CONTINUE
```

## 7. 66 星与 N1 100% 结果的差异

本次 66 星运行与先前 `global-size-aware-hrw` 的 1500/1500 结果使用相同
TaskTrace、带宽、时延、队列、MTU、seed、计算节点数和仿真时长，但不是同一
张网络图：

- N1 `xw-66sat-static-2g` 有 132 条无向 ISL，所有节点度数均为 4；
- 本次 Walker-Star、`seam=false` 有 121 条无向 ISL，44 个节点度数为 4，
  22 个节点度数为 3；
- 本次图正好是 N1 图删除 11 条 seam ISL 后的子图；
- 预计 packet-hops 从 28,707,289 增至 31,774,327，增加约 10.7%。

51 份 66 星快照的边集合完全相同，运行中没有链路断开或重连，size-aware
记录也没有 candidate-invalid release。因此本次 5 个未完成任务最直接的事实
是 5 条 INPUT flow 共发生 174 个 FqCoDel Drop，而不是计算排队、动态断链或
仿真提前终止。现有流量感知 ECMP 能缓解路径集中，但不保证任意图上的 UDP
零丢包。

## 8. 解释边界与后续决策

这是 1500-task 的 75% 代表性高负载验证，不是容量上限测试。66 星采用全计算
部署，351 星采用三分之一计算节点并压缩到达窗口；两者不能解释为相同计算
部署比例下的纯规模性能曲线。

本轮还具有以下边界：

- 固定 8 ms 时延、20 s 快照、无动态断链和无故障；
- 没有 checkpoint、backup、recovery 或任务迁移；
- 每个规模只有一次确定性正式运行，没有多 seed 和统计置信区间；
- 720 星没有正式结果，不能外推其完成率、内存或墙钟成本；
- `RUN_VALID / CONTINUE` 表示输出合同有效且超过 90% 门槛，不表示必须达到
  100% 任务完成。

两次正式结果都支持继续研究路由侧的拥塞缓解。后续容量感知模式应作为新的
opt-in 路由模式，与 `global-size-aware-hrw` 做相同 fixture / workload 对照，
不覆盖本报告中的基线事实。
