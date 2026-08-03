# N2B 75% 代表性压力验证（66 / 351 / 720 星）

## 1. 结论

本报告保留两轮 75% 压力验证的完整历史。第一轮在
`991211c4fd90d4d76451a107095c5e91a79ec41d` 上使用
`global-size-aware-hrw`：66 星和 351 星分别完成 1495/1500 和
1489/1500 个任务，未完成对象均由 FqCoDel `QUEUE_DISC` 丢包造成；720 星
当时只完成 preflight 和小型 smoke，没有启动正式运行。

第二轮在容量感知路由完成动态重准入和模块化重构后，使用
`global-capacity-aware-hrw` 重新运行同一逻辑工作负载。66、351 和 720 星
均完成 1500/1500 个任务、3000/3000 条 transfer 和 6,584,966/6,584,966
个 UDP 数据报，所有已采集丢包类别均为 0，外部 checker 均返回
`RUN_VALID / CONTINUE`。

该结果证明完整路径容量准入在本次固定 75% 三规模矩阵中消除了已观察到的
竞争丢包，但不是任意星座、任意负载或故障条件下的零丢包保证。

## 2. 版本与环境

- 集成分支：`feature/n2-integration`
- Size-aware 基线运行 SHA：`991211c4fd90d4d76451a107095c5e91a79ec41d`
- Capacity-aware 66 星运行 SHA：`889c53e2b1a1d1e16392ebbb1c58fe0d362dd368`
- Capacity-aware 351/720 星运行 SHA：
  `69e845bb2ee6958a781ef330a8ed2c9f688fa72d`
- PR #18 merge SHA：`991211c4fd90d4d76451a107095c5e91a79ec41d`
- PR #18 合并后 Full Regression：
  [success](https://github.com/forest-rabbit/SatCompute/actions/runs/30631733111)
- 构建：ns-3 debug profile，`--disable-examples --disable-tests
  --enable-modules=satcompute`
- 主机：AMD Ryzen 7 5800H，8 cores / 16 logical CPUs，15 GiB RAM，
  4 GiB swap，WSL2
- 正式运行次数：每个实际执行的规模/路由组合一次，`ecmpHashSeed=1`

`889c53e` 与 `69e845b` 之间只有压力报告文档差异，没有生产代码或 `wscript`
差异。第二轮原始输入和输出位于：

```text
/tmp/satcompute-n2-routing-refactor-66-pressure-889c53e
/tmp/satcompute-n2-capacity-pressure-69e845b
```

仓库只保存哈希、运行合同和经 checker 核验的紧凑结论；原始动态场景、完整
TaskTrace 和 CSV/JSON 输出不提交。

## 3. 冻结合同

两轮共同参数：

```text
ecmpHashSeed=1
transferChunkMode=size-aware
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

路由差异严格限定为：

| 轮次 | routingMode | pacingMode | 完整路径容量准入 |
|---|---|---|---|
| 历史基线 | `global-size-aware-hrw` | `first-hop-serialization` | 否 |
| 本次复验 | `global-capacity-aware-hrw` | `path-bottleneck-serialization` | 是 |

| 规模 | 星座 | 计算节点 | 实际到达范围 | 仿真 | 快照 | 无向 ISL |
|---|---:|---:|---:|---:|---:|---:|
| 66 | 6 × 11 | 66 | 1.018272–599.609838 s | 1000 s | 51 | 121 |
| 351 | 27 × 13 | 117 | 0.612598–339.952008 s | 600 s | 31 | 689 |
| 720 | 18 × 40 | 240 | 0.310607–164.916552 s | 300 s | 16 | 1400 |

66 星使用 66/66 全计算部署；351 星计算节点的每轨分布为
`[4,4,5] × 9`，720 星为 `[13,13,14] × 6`。三种规模的任务 ID、输入/输出
大小、计算工作量和到达顺序一致，只重新映射 source、compute、result 节点并
按冻结窗口缩放到达时间。因此这是平台跨规模压力验证，不是固定计算节点比例
下的纯性能曲线。

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
| 66 | `38af4d0daeee9e1b593ee0d655e09611962c224443d91a9ecec4aa5aca3f47aa` | `145c6360589a8c8d720a2be577ea7c58202059936e7a640dbe3296905dd3acf0` | `c1ddfcec15489077a968b237c419568ab6f55fe7144280aa3782f0f4a1637a7e` |
| 351 | `d835d044cd2dd462f8a438fb5e059681d3291edcf21d459157600919dc516d88` | `9e323dd39d020029a4bbd51956e5698740ec7260cfce5ed811d1215eba86a46d` | `f980307fd6ac313581e0a7eae8ca48de6123951ef21040d89ca320399527bfa0` |
| 720 | `c0e12742de16503f71aa4d74a1d0a31b9d406f161c2ea550e9901cd1bb099ad1` | `abbd214166150c71e9b22f9c3f1452ef845c10a0c87a8320a9e3487c556f405d` | `47045e8eb7acfdddb1d580561c081cf5544a8e9f280b006868fc8103701c8544` |

| 规模 | TaskTrace | workload summary |
|---|---|---|
| 66 | `b2fc74f11b2074c7f29772464cf53877b6946f9905677f8c43f21c5740708237` | `2a82521eec2f2ee2ac11666480df02344153da521cd7c2e5a8bed27bdcf43eb3` |
| 351 | `28f06cfd5da3cffda7d2116683b9ee2960fe80879fbe4e6c5eb9709f15d4678b` | `f47a3b2b14220690a633ce1b71d55aebae14f1000a8470a5318a5d52227acb59` |
| 720 | `8a04b5e0538d66ee6745fca25703b827dad9c5497289a5675a64009df9df91a6` | `94aadcf72e4e14457d058bd9d54afba7fada05b02fc0dbbbb5ae3ceb1b201c95` |

三种 TaskTrace 和 summary 哈希均精确复现第一轮压力输入。scenario aggregate
包含代码 provenance，因此随运行 SHA 改变；topology aggregate、ComputeProfile
和 workload 哈希才是两轮物理输入等价的核心证据。

## 5. Preflight 与小型门禁

| 规模 | 预计 packet-hops | duration lower bound | 有向队列 | 理论队列总上限 |
|---|---:|---:|---:|---:|
| 66 | 31,774,327 | 653.907 s | 242 | 14.424 GiB |
| 351 | 80,748,310 | 374.826 s | 1378 | 82.135 GiB |
| 720 | 104,703,477 | 189.490 s | 2800 | 166.893 GiB |

这些队列上限不会在启动时一次性预分配。本次 Capacity-aware 正式运行前，
三种规模均通过 scenario checker、preflight 和跨越 20 s 拓扑更新时间的
10-task smoke；每个 smoke 都完成 10/10 tasks、20/20 transfers，所有包完整
接收且零丢包。66 星 gate 使用冻结 workload 中最早到达的 10 个任务；351/720
使用同一份 545,000,000-byte 小型 workload，因此小型包数不作为三规模性能
比较指标。

## 6. Size-aware 历史基线

第一轮 `global-size-aware-hrw` 的正式结果如下；720 星当时没有启动正式运行：

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

第一轮 N2 66 星 Size-aware 运行与更早的 N1
`global-size-aware-hrw` 1500/1500 结果使用相同 TaskTrace、带宽、时延、
队列、MTU、seed、计算节点数和仿真时长，但不是同一张网络图：

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

## 8. Capacity-aware 三规模正式结果

### 8.1 完成、字节与丢包

| 指标 | 66 星 | 351 星 | 720 星 |
|---|---:|---:|---:|
| 状态 | COMPLETE | COMPLETE | COMPLETE |
| 完成任务 | 1500/1500 | 1500/1500 | 1500/1500 |
| 完成 transfer | 3000/3000 | 3000/3000 | 3000/3000 |
| 发送/接收应用字节 | 109,263,294,080 / 109,263,294,080 | 109,263,294,080 / 109,263,294,080 | 109,263,294,080 / 109,263,294,080 |
| FlowMonitor tx/rx/lost | 6,584,966 / 6,584,966 / 0 | 6,584,966 / 6,584,966 / 0 | 6,584,966 / 6,584,966 / 0 |
| QueueDisc/device/UDP/unattributed Drop | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| checker | RUN_VALID / CONTINUE | RUN_VALID / CONTINUE | RUN_VALID / CONTINUE |

三次进程退出码均为 0。成功运行没有保留 `diagnostics/failure/`。

### 8.2 任务端到端时延

端到端时延从任务到达入口卫星开始，到 RESULT 被结果卫星完整接收为止，包含
INPUT 传输、计算排队、计算服务和 RESULT 传输。

| 指标 | 66 星 | 351 星 | 720 星 |
|---|---:|---:|---:|
| mean | 2.024577267 s | 2.141963728 s | 2.206884178 s |
| median | 1.853765724 s | 1.969179752 s | 2.043812781 s |
| p95 | 3.747923600 s | 3.850292607 s | 3.934535808 s |
| p99 | 6.498724387 s | 6.798317134 s | 6.752360467 s |
| max | 11.216028134 s | 13.423847533 s | 11.348126054 s |

平均时延分解：

| 阶段 | 66 星 | 351 星 | 720 星 |
|---|---:|---:|---:|
| INPUT 传输 | 0.260762647 s | 0.321531397 s | 0.354287234 s |
| 计算排队 | 0.053314185 s | 0.050380394 s | 0.052559940 s |
| 计算服务 | 1.593336492 s | 1.593336492 s | 1.593336492 s |
| RESULT 传输 | 0.117163943 s | 0.176715444 s | 0.206700512 s |

### 8.3 排空、准入和计算状态

| 指标 | 66 星 | 351 星 | 720 星 |
|---|---:|---:|---:|
| 最后任务到达 | 599.609838294 s | 339.952007846 s | 164.916551921 s |
| 最后任务完成 | 602.150041765 s | 343.959296987 s | 172.601215656 s |
| 仿真结束前排空余量 | 397.849958235 s | 256.040703013 s | 127.398784344 s |
| 等待准入的 transfer | 23/3000 | 42/3000 | 46/3000 |
| 最大准入等待 | 3.193558127 s | 2.666106096 s | 3.045026624 s |
| 每计算节点任务数 | 22–23 | 12–13 | 6–7 |
| 平均计算利用率 | 3.6212% | 3.4046% | 3.3195% |
| 最大计算利用率 | 4.6467% | 4.5073% | 5.5812% |
| 最大计算队列长度 | 3 | 2 | 1 |

三次运行结束时均满足：

```text
active flows = 0
next-hop assignments = 0
reserved bytes = 0
active capacity paths = 0
reserved directed links = 0
reserved rate = 0
pending transfers = 0
```

| 预留事件 | 66 星 | 351 星 | 720 星 |
|---|---:|---:|---:|
| reservation events | 43,545 | 109,868 | 144,125 |
| assign events | 14,488 | 36,525 | 47,951 |
| sticky reuse events | 14,569 | 36,818 | 48,223 |
| candidate-invalid release | 0 | 0 | 0 |
| route-invalidated release | 0 | 0 | 0 |

压力场景的快照没有使活动路径失效，因此动态暂停、释放和重新准入合同仍由独立
的动态路径恢复 fixture 证明，不能由本表的 0 次失效事件替代。

### 8.4 运行成本

| 指标 | 66 星 | 351 星 | 720 星 |
|---|---:|---:|---:|
| `/usr/bin/time` elapsed | 57:38.30 | 2:22:21 | 3:50:57 |
| user/system CPU | 3457.53 / 0.62 s | 8540.02 / 0.93 s | 13855.15 / 1.99 s |
| peak RSS | 117,948 KB | 413,928 KB | 1,169,020 KB |
| process exit | 0 | 0 | 0 |

### 8.5 与 Size-aware 历史基线对照

| 规模与指标 | Size-aware HRW | Capacity-aware HRW |
|---|---:|---:|
| 66 完成任务 | 1495/1500 | 1500/1500 |
| 66 lost packets | 174 | 0 |
| 66 mean/max 时延 | 2.016012 / 11.323713 s | 2.024577 / 11.216028 s |
| 66 elapsed / peak RSS | 56:13.04 / 112,688 KB | 57:38.30 / 117,948 KB |
| 351 完成任务 | 1489/1500 | 1500/1500 |
| 351 lost packets | 1,139 | 0 |
| 351 mean/max 时延 | 2.126280 / 13.543023 s | 2.141964 / 13.423848 s |
| 351 elapsed / peak RSS | 2:25:28 / 393,988 KB | 2:22:21 / 413,928 KB |

两种模式的平均时延对应不同的完成任务集合，不能把数毫秒级差异解释为严格的
逐任务性能回归。720 星没有 Size-aware 正式基线，因此只报告本次真实结果，
不构造对照值。

## 9. 解释边界与阶段结论

这是 1500-task 的 75% 代表性高负载验证，不是容量上限测试。66 星采用全计算
部署，351/720 星采用三分之一计算节点并压缩到达窗口；三者不能解释为相同
计算部署比例下的纯规模性能曲线。

本轮还具有以下边界：

- 固定 8 ms 时延、20 s 快照、无动态断链和无故障；
- 没有 checkpoint、backup、recovery 或任务迁移；
- 每个规模只有一次确定性正式运行，没有多 seed 和统计置信区间；
- `RUN_VALID / CONTINUE` 表示输出合同有效且超过 90% 门槛，不表示必须达到
  100% 任务完成。

本阶段结论是：`global-capacity-aware-hrw` 已在定向竞争 fixture、动态路径恢复
fixture 和 66/351/720 星 75% 正式矩阵中形成一致证据链，可以作为后续 N2
故障实验的当前路由基线。原 Size-aware 结果继续作为历史对照保留，不因本次
复验而改写。
