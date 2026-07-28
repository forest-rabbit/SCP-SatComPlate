# Pre-N2 动态大小感知 HRW ECMP 验证

## 1. 范围与版本

本轮在纯 HRW 分支 `feature/pre-n2-stable-ecmp` 的
`ec81c5ade5e263a2c97dd989720cd84a025aa717` 之上开发，未修改 main。
完整 75% 仿真对应的实现 Head 为：

```text
fe174fd8f1e345b15b283730d536bb37e0787f81
```

之后的 `875470b` 只扩展外部 Python 检查器，不改变 ns-3 仿真实现。
截至本报告，分支尚未推送，未创建新的 PR，也未合并；远程 CI 尚未运行。

## 2. 冻结合同

`global-size-aware-hrw` 保留原 HRW 排名，只比较 HRW 前两名对应的物理
下一跳 `(node, gateway, output interface)` 当前预留字节，选择负载较小者，
相等时选择 HRW 第一名。assignment 仍保存完整候选身份。

- 预留量使用 transfer 声明字节，不读取设备队列或 FqCoDel backlog；
- 同一 flow 在已选候选仍有效时保持 sticky；
- 只有已选候选消失时释放并重选，候选恢复不迁回旧 flow；
- sender 成功提交最后一个 UDP payload 后释放该 transfer 的全部逐节点
  assignment；
- 未登记或已经停止发送的尾包退化为纯 HRW，不重新建立 reservation；
- 不改变 UDP、first-hop serialization、分包、队列或 stock
  `Ipv4GlobalRouting`。

## 3. 本地回归

以下检查全部通过：

- `./waf configure --disable-examples --disable-tests --enable-modules=satcompute`
  和 `./waf build`；
- 110 s、零负载 smoke：66 颗卫星、132 条 ISL、12 份完整快照，依次应用
  0–110 s 的 11 次更新；
- 旧 `global-hash-per-flow` 静态/动态黄金结果与重复运行确定性；
- 纯 HRW 的独立分数重算、候选顺序无关、增删候选最小迁移、seed 变化和
  重复运行确定性；
- 大小感知静态、动态 fixture 的逐事件 reservation 账本及重复运行；
- task stress checker 的状态、字节、包、时间和故障归因合同。

N1 三流冲突重放结果：

| 模式 | QueueDisc 丢包 | `36→37` 上的目标 flow |
|---|---:|---:|
| 旧 Hash | 13 | 3 |
| 纯 HRW | 0 | 2 |
| 大小感知 HRW | 0 | 1 |

38 条 1-byte source-port 占位 flow 均在 0 ns 完成并释放；0.1 s 的三条
目标 flow 开始前 reservation 为零。

66 星中型场景包含 60 个集中到达任务、120 条 transfer、3 GB 输入和
243,028 个派生 UDP 包：

| 指标 | 纯 HRW | 大小感知 HRW |
|---|---:|---:|
| 完成任务 | 51/60 | 53/60 |
| QueueDisc 丢包 | 20 | 10 |
| 受害 transfer | 9 | 7 |
| mean 完成时延 | 1.988268 s | 1.971456 s |
| p95 完成时延 | 3.450865 s | 3.450865 s |
| max 完成时延 | 4.325325 s | 4.325325 s |
| 墙钟 | 1:46.72 | 1:48.00 |
| 峰值 RSS | 71,828 KB | 71,136 KB |

大小感知运行产生 12 次 HRW 第二候选选择，结束时 active flow、
assignment 和 reserved bytes 均为零。

## 4. 冻结 75% 场景

输入 SHA-256：

```text
TaskTrace       b2fc74f11b2074c7f29772464cf53877b6946f9905677f8c43f21c5740708237
workload summary 2a82521eec2f2ee2ac11666480df02344153da521cd7c2e5a8bed27bdcf43eb3
```

冻结参数：

```text
66 satellites / 66 compute nodes
2 Gbit/s ISL
64,000,000-byte device queue
default FqCoDel
1000 s simulation
seed 1
size-aware transfer chunks
MTU 65,535 bytes
receiver buffer 131,072 bytes
1,500 tasks / 3,000 transfers
81,750,000,000 INPUT bytes
27,513,294,080 RESULT bytes
109,263,294,080 total application bytes
6,584,966 derived UDP packets
```

除 `routingMode=global-size-aware-hrw` 外，大小感知运行与旧 Hash 基线
保持一致。

## 5. 75% 结果

| 指标 | 冻结旧 Hash 基线 | 大小感知 HRW |
|---|---:|---:|
| 运行状态 | PARTIAL | COMPLETE |
| 完成任务 | 1493/1500 | 1500/1500 |
| 完成 transfer | 2988/3000 | 3000/3000 |
| FlowMonitor tx/rx/lost | 6578160/6578106/54 | 6584966/6584966/0 |
| QueueDisc 丢包 | 54 | 0 |
| device queue 丢包 | 0 | 0 |
| UDP socket 丢包 | 0 | 0 |
| 未归因 loss | 0 | 0 |
| 受害 transfer | 7 | 0 |
| mean 完成时延 | 2.029024493 s | 2.034385849 s |
| p95 完成时延 | 3.746318268 s | 3.757470686 s |
| max 完成时延 | 11.485186250 s | 11.485186250 s |
| 墙钟 | 52:48.08 | 52:30.67 |
| 峰值 RSS | 109,020 KB | 113,460 KB |

新模式完成了基线中未完成的 7 个任务，因此两列时延对应的完成任务集合并不
完全相同。mean 增加约 5.36 ms，p95 增加约 11.15 ms，max 不变；本轮合同
不要求以丢弃任务为代价降低已完成任务时延。

路由与 reservation 统计：

```text
registered flows                         3000
route events                            13072
SINGLE_CANDIDATE route events            6860
SIZE_AWARE_HRW_PRIMARY route events      4837
SIZE_AWARE_HRW_SECONDARY route events      31
HRW_FALLBACK_INACTIVE route events       1344
ASSIGN reservation events                9220
RELEASE_SENDER_FINISHED events           9220
peak total reserved bytes          8573884900
peak physical-next-hop reserved    1049409714
final active/assignment/reserved       0/0/0
```

`peak total reserved bytes` 是各节点上活动 assignment 的声明字节之和，
同一 transfer 可在多个逐跳节点上计入；它不是实际设备队列占用或预分配
内存。18,440 条 reservation 事件已逐行重放，所有 before/after 变化及
最终归零均一致。

## 6. 验收结论

计划门槛与结果：

| 门槛 | 结果 |
|---|---|
| `RUN_VALID` 且 checker 合同通过 | 通过 |
| 完成任务不少于 1493 | 1500，超过门槛 |
| QueueDisc 丢包少于 54 | 0，超过门槛 |
| 受害 transfer 不增加 | 0，超过门槛 |
| 无新增 device queue/UDP/unattributed 丢包 | 均为 0 |
| reservation 最终正确清理 | `0/0/0` |
| 旧 Hash 与纯 HRW 黄金行为不变 | 通过 |

因此本地实现和收益门槛均通过。大型 TaskTrace、运行输出和计时文件继续只
保留在 `/tmp`，不提交仓库。进入 PR 前仍需代码审查、推送分支并取得远程
CI 结果；本报告本身不声称 CI 已绿色。

全量结果可与小型/中型输出一起复核：

```bash
python3 contrib/satcompute/tools/check-size-aware-output.py \
  --static-hrw=<pure-hrw-output> \
  --static-first=<size-static-a> \
  --static-second=<size-static-b> \
  --dynamic-first=<size-dynamic-a> \
  --dynamic-second=<size-dynamic-b> \
  --medium-hrw=<medium-hrw-output> \
  --medium-size=<medium-size-output> \
  --full-baseline=<frozen-hash-output> \
  --full-size=<full-size-aware-output>
```
