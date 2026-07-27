# SatCompute N1.6 本地实施与压力验证审查报告

> 日期：2026-07-27
>
> 分支：`feature/n1-task-compute`
>
> 正式 review base：`71f23ef20bd0e342096e1c25c0a3e6e452d2abdc`
>
> 本报告验证的代码 HEAD：`2e7b5e3`
>
> 当前状态：`READY_FOR_GPT_AUTHOR_REVIEW`
>
> Draft PR 状态：`NOT_READY_TO_OPEN_DRAFT_PR`

本报告是 N1.6 压力验证的唯一审查报告。大型 TaskTrace、FlowMonitor
输出、CSV 指标和性能日志全部保留在 `/tmp`，没有提交仓库。本轮没有运行
GitHub CI、没有创建 PR，也没有实现可靠重传。

## 1. 结论

冻结的 50% workload 在 22 个计算节点、64 MB/设备队列下完整仿真到
1000 s，结果为：

```text
999/1000 tasks completed
1999/2000 transfers completed
FlowMonitor tx/rx/lost = 4453074/4453049/25 packets
ISL device-queue drops = 0 packets
strict exit code = 1
```

唯一未完成项是 task 906 的 RESULT transfer 1812。sender 已发送完整
289,600,000 bytes/4,525 packets，receiver 收到
288,000,000 bytes/4,500 packets，缺少 1,600,000 bytes/25 packets。
因此可以确认 transfer 未完成是网络数据报未抵达直接造成的，不是任务仍在
计算、sender 未发完或仿真被外部中断。

由于设备队列 Drop trace 没有记录到这 25 个包，22 节点结果本身无法定位
具体丢弃层。按审查方案触发的 66 计算节点对照保持 workload 数据量、
source/result、到达时间、ECMP seed、链路、队列和仿真时间不变，只确定性
重映射 `compute_node_id`，结果为：

```text
1000/1000 tasks completed
2000/2000 transfers completed
FlowMonitor tx/rx/lost = 4453074/4453074/0 packets
exit code = 0
generic checker = PASS
```

本轮将 22 节点判定为 `CONTRIBUTING_FACTOR`：计算端点集中是这个冻结
workload/seed 的重要诱因，但现有证据不足以把它称为唯一根因。后续综合
压力基准建议使用 66 个计算节点；22 节点配置保留为“计算端点受限/集中”
对照。

## 2. 本轮诊断提交

```text
3c29433 fix: record incomplete task and transfer diagnostics
bd89358 test: add per-link queue drop and flow concentration diagnostics
2e7b5e3 fix: accept FlowMonitor-only failure diagnostics
```

`3c29433` 将严格失败改为“先落盘、后返回非零”，并增加：

- 全部未完成 task/transfer 枚举；
- partial sender/receiver bytes 与 packets；
- 失败运行中的 FlowMonitor 逐流指标；
- `diagnostic-summary.json`；
- 状态聚合和严格退出码。

`bd89358` 增加：

- 每个有向 ISL PointToPointNetDevice 队列的 Drop trace；
- 队列 Drop 事件和有向链路聚合；
- ECMP flow/path/link planned-load 聚合；
- top dropped/planned links；
- 计算节点相邻链路标记。

`2e7b5e3` 修正失败检查器的约束：正式压力结果允许
“FlowMonitor loss > 0、device queue drop = 0”；4 星最小 fixture 使用
`--require-queue-drop`，继续强制验证队列 Drop 的采集和有向链路映射。

失败运行新增文件：

```text
incomplete-tasks.csv
incomplete-transfers.csv
isl-queue-drops.csv
isl-queue-drop-summary.csv
flow-link-concentration.csv
diagnostic-summary.json
```

成功运行的既有输出合同不变。

## 3. 冻结 50% 配置

22 节点主测试：

```text
TaskTrace SHA-256:
78c90424c11216ef8cfb83d7c5d3e2bcf699af09d0ab5c4271d5d99ef54445bc

tasks: 1000
INPUT bytes: 54500000000
OUTPUT bytes: 17735650347
compute work units: 2390026806
large tail: 10 x 1 GB, 20 x 500 MB
arrival window: 1.318157677s..599.410171627s
topology: 66 satellites, 132 undirected ISLs
bandwidth: 2000000 kbps = 2 Gbit/s per ISL
compute nodes: 22, each 1500000 work units/s
simulation duration: 1000s
queue: 64000000 bytes per directed device
routing: global-hash-per-flow
ECMP seed: 1
packetization: size-aware
MTU: 65535 bytes
```

Preflight：

```text
INPUT packets: 3254828
RESULT packets: 1198246
total packets: 4453074
estimated packet hops: 18942110
recommended duration lower bound: 687.431s
264 device queues x 64000000 bytes
aggregate theoretical queue capacity: 15.736 GiB, not preallocated
```

主测试复用了冻结输入，没有重新随机生成，也没有改变任何 workload 参数。

## 4. 本地验证

本轮按作者要求只做本地验证，没有运行 GitHub CI。

在 `2e7b5e3` 上通过：

- `./waf build`；
- N0 110 s smoke：66 星、132 ISL、0～110 s 共 12 个静态快照；
- 单任务 ECMP；
- non-preemptive FCFS；
- heterogeneous R/2R；
- TaskTrace 数组顺序确定性；
- ComputeProfile 数组顺序确定性；
- 40-task 生成 workload：40/40 tasks、80/80 transfers、零丢包；
- 4 星故意失败 fixture：严格退出 1，记录 1 个有向队列 Drop；
- 22 节点正式失败输出：FlowMonitor-only 丢包证据检查通过；
- 66 节点对照：通用成功检查器通过。

当前检查器还重新验收了既有输出：

```text
Level 2-500:  PASS, 500 tasks / 1000 transfers
Level 2-1000: PASS, 1000 tasks / 2000 transfers
Level 2-2000: PASS, 2000 tasks / 4000 transfers
Level 3:      PASS, 500 tasks / 1000 transfers
Level 4 25%: PASS, 500 tasks / 1000 transfers
```

## 5. 既有成功压力证据

| 场景 | 仿真时间 | tasks/transfers | INPUT bytes | OUTPUT bytes | 包数 | wall-clock | peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| Level 1 | 20 s | 40/80 | 4,000,000 | 3,332,332 | 7,199 | 3.78 s | 未单独记录 |
| Level 2-500 | 100 s | 500/1,000 | 4,096,000 | 23,433,252 | 27,433 | 12.77 s | 64,528 KB |
| Level 2-1000 | 200 s | 1,000/2,000 | 8,192,000 | 46,180,167 | 54,173 | 25.43 s | 82,304 KB |
| Level 2-2000 | 400 s | 2,000/4,000 | 16,384,000 | 94,077,643 | 110,016 | 52.06 s | 116,640 KB |
| Level 3 compute | 240 s | 500/1,000 | 4,096,000 | 25,065,859 | 29,012 | 13.93 s | 64,060 KB |
| Level 4 25% | 1000 s | 500/1,000 | 27,250,000,000 | 8,473,331,744 | 2,283,410 | 17:02.59 | 67,984 KB |

这些成功场景都满足 task/transfer 完成、FlowMonitor 零丢包和检查器
PASS。25% 场景在 32 MB/设备队列下通过；8 MB 和 16 MB 均失败。

## 6. 22 节点 50% 主测试

运行位置：

```text
/tmp/satcompute-n1.6/level4-50/bd89358-diagnostics/queue-64000000/
```

性能和退出：

```text
wall-clock: 34:54.30
user time: 2094.37s
peak RSS: 88232 KB
simulation duration reached: 1000s
exit status: 1
```

完整性：

```text
tasks: 1000 total, 999 completed, 1 RESULT_TRANSFERRING
transfers: 2000 total, 1999 completed, 1 STARTED
all other task states: 0
all compute jobs completed: 1000
maximum compute queue length: 3
maximum full-run compute utilization: 8.0503%
```

FlowMonitor：

```text
tx packets: 4453074
rx packets: 4453049
lost packets: 25
tx bytes: 72360336419
rx bytes: 72358735719
loss ratio: 0.00056141%
```

FlowMonitor 的 `lostPackets` 包括超过默认 10 s 未再被观察到的包；它本身
不是丢弃层定位。这里 receiver 同时精确缺少相同的 25 个包，因此可以确认
这些数据报截至 1000 s 没有抵达目的应用。

### 唯一未完成任务与 transfer

```text
task_id: 906
state: RESULT_TRANSFERRING
task source/compute/result: 18/15/28
input transfer: 1811, completed
result transfer: 1812, STARTED
RESULT start: 424.529159580s
```

```text
transfer_id: 1812
source/destination: 15 -> 28
declared bytes: 289600000
payload bytes per packet: 64000
derived/sent packets: 4525/4525
received packets: 4500
sent/received bytes: 289600000/288000000
missing bytes: 1600000
missing packet lower bound: 25
last sender packet: 425.687846460s
completion time: absent
```

同一 five-tuple 的 FlowMonitor 记录也是
`tx/rx/lost = 4525/4500/25`。sender、receiver 和 FlowMonitor 三套计数
一致。

## 7. 有向队列与 ECMP 集中度

22 节点正式运行：

```text
ISL directed links: 264
queue drop packets/bytes: 0/0
dropped directed links: 0
top dropped links: none
compute-adjacent drop share: N/A because total drop bytes = 0
```

4 星最小失败 fixture 已验证相同采集代码能够记录并映射队列 Drop。因此
正式运行中的 0 表示没有观察到 PointToPointNetDevice DropTail 队列
Drop，不能把 FlowMonitor 的 25 个 lost packets 直接写成“64 MB 队列
溢出”。

planned-load 集中度：

```text
total planned application hop-bytes: 298441870038
top-1 share: 1.357453%
top-5 share: 5.743627%
top-10 share: 10.265636%
compute-adjacent directed links: 176/264 = 66.666667%
compute-adjacent planned bytes: 66.734626%
compute-adjacent links in top-10: 6/10
```

计算相邻链路的 planned-byte 占比几乎等于它们的链路数量占比，不能仅凭
全局 planned load 证明计算节点周边存在普遍热点。

top-5 planned links：

| rank | directed link | planned bytes | transfers | >500 MB transfers | compute-adjacent |
|---:|---|---:|---:|---:|---|
| 1 | 24→23 | 4,051,208,136 | 40 | 8 | yes |
| 2 | 25→24 | 3,768,071,452 | 42 | 7 | yes |
| 3 | 26→27 | 3,199,813,296 | 42 | 8 | yes |
| 4 | 23→22 | 3,087,452,086 | 37 | 5 | no |
| 5 | 27→28 | 3,034,842,548 | 49 | 8 | yes |

transfer 1812 的固定 ECMP 路径：

```text
15 -> 26 -> 27 -> 28
```

对应链路排名：

```text
15->26: rank 158, 873652580 planned bytes
26->27: rank 3,   3199813296 planned bytes
27->28: rank 5,   3034842548 planned bytes
```

### pacing 竞争证据

成功的 transfer 1725（38→30，99,856,586 bytes）和失败的 transfer
1812 都选择了 `27→28`。它们的 sender 时间窗为：

```text
transfer 1725: 424.429758330s..424.829305530s
transfer 1812: 424.529159580s..425.687846460s
source-send overlap: 0.300145950s
```

两条 flow 都按各自首跳的 2 Gbit/s 线速独立 pacing，共享的 `27→28`
只有 2 Gbit/s。代码使用 payload 加 UDP/IP/PPP 头计算发送间隔，不是简单
以 payload 假定带宽。

因此 `PACING_CONCLUSION = SUPPORTED`：独立 line-rate pacing 和 ECMP
汇聚是瞬时竞争的重要诱因。由于没有对应的 device-queue Drop 事件，不能
声称已精确定位 transfer 1812 的 25 个包在 `27→28` 被丢弃。

## 8. 66 计算节点条件对照

本地对照输入位置：

```text
/tmp/satcompute-n1.6/level4-50/66-compute-remap-v1/
```

原 TaskTrace 未被修改。新输入只改变 `compute_node_id`：

```text
method:
candidate = (task_id - 1) % 66
while candidate equals source or result:
  candidate = (candidate + 1) % 66

changed compute assignments: 985/1000
used compute nodes: 66
tasks per compute node: 13..17
compute rate per node: 1500000 work units/s
```

逐任务验证确认以下字段完全不变：

```text
task_id
source_node_id
result_node_id
input_bytes
output_bytes
compute_work_units
arrival_time_ns
```

对照 TaskTrace SHA-256：

```text
53480581b44d401367df44c2211ce600518e56676a2c621b242f53e95a412a66
```

Preflight：

```text
tasks: 1000
INPUT/OUTPUT bytes: 54500000000/17735650347
derived packets: 4453074
estimated packet hops: 18730916
recommended duration lower bound: 637.996s
```

运行位置：

```text
/tmp/satcompute-n1.6/level4-50/66-compute-remap-v1/
  bd89358-diagnostics/queue-64000000/
```

结果：

```text
wall-clock: 34:45.49
user time: 2085.44s
peak RSS: 88116 KB
exit status: 0
tasks: 1000/1000 completed
transfers: 2000/2000 completed
application bytes sent/received: 72235650347/72235650347
FlowMonitor tx/rx/lost: 4453074/4453074/0
max compute queue length: 1
max full-run compute utilization: 3.1067%
generic checker: PASS
```

对照的 estimated packet hops 比 22 节点少 211,194，约 1.12%。这是计算
端点重映射产生的路径变化，属于对照变量的自然结果，解释时不能忽略。

## 9. 22/66 节点判定

```text
22-node conclusion: CONTRIBUTING_FACTOR
66-node comparison: RUN, PASS
```

依据：

- 22 节点固定配置只差 25 个包便无法可靠完成；
- 66 节点对照在相同总数据、到达、链路、队列和 ECMP seed 下零丢包通过；
- 22 节点计算侧本身已完成全部 1000 个任务，计算队列很浅；
- 改变计算端点分布会改变 INPUT/RESULT 路径及并发汇聚关系；
- 22 节点全局 compute-adjacent planned-load 比例并未显著超出其链路比例；
- 正式运行没有取得可定位的 device-queue Drop。

所以当前证据支持“计算端点集中是重要诱因”，但不能证明“22 个节点导致
计算容量不足”，也不能证明它是唯一根因。

后续约定：

```text
66 compute nodes:
  后续综合压力基准

22 compute nodes:
  计算端点受限/集中对照
```

单个固定 remap 的通过不代表任意 66 节点 workload 都可靠。可靠 UDP
完成仍然没有协议保证。

## 10. 当前能力边界

| 场景 | 状态 |
|---|---|
| 25% / 22 nodes / 32 MB | PASS，500/500，零丢包 |
| 50% / 22 nodes / 64 MB | FAIL，999/1000，lost 25 |
| 50% / 66 nodes / 64 MB | PASS，1000/1000，零丢包 |
| 75% | NOT RUN |
| 109 GB / 2000 tasks preflight | PASS |
| 109 GB / 2000 tasks actual | NOT RUN |

109 GB preflight：

```text
INPUT bytes: 109000000000
OUTPUT bytes: 35877906094
tasks: 2000
total packets: 8724340
estimated packet hops: 37406704
recommended duration lower bound: 767.840s
```

它只证明输入、端口、packetization、计算容量和时长下界合同通过，不代表
109 GB 实际仿真通过。

## 11. Draft PR 决策

技术诊断门槛已满足：

```text
[x] 失败运行先落盘后严格失败
[x] 全部未完成 task/transfer 可枚举
[x] partial sent/received bytes 和 packets 可读取
[x] FlowMonitor loss 可在失败场景落盘
[x] 有向 ISL queue Drop 采集和映射经最小 fixture 验证
[x] flow/link concentration 已分析
[x] 冻结 50%/22 节点/64 MB 已重跑
[x] 条件触发的 66 节点对照已完成
[x] 既有 N1 成功检查和 25% 输出继续通过
[x] 没有实现可靠重传或 checkpoint/failure
```

当前仍标记：

```text
Draft PR readiness: NOT_READY_TO_OPEN_DRAFT_PR
Recommended next step: GPT/author review
Recommended merge path: REQUEST_AUTHOR_DECISION
```

原因是作者要求先完成本轮审查和可能的修改，再决定是否创建 Draft PR。
本报告不自行创建 PR，也不自行合并。

## 12. 本轮没有实现

```text
UDP ACK/NACK
timeout/selective retransmission
TCP replacement
global congestion control
congestion-aware pacing
checkpoint
backup
failure
recovery
RTO/RPO
TaskProfileCatalog
task_profile_id
```

没有修改 `src/internet`、`src/point-to-point` 或 TaskTrace schema。可靠
NetworkTransfer 和竞争感知 pacing 应作为后续独立阶段设计，不能混入
N1.6 诊断提交。

## 13. 给 GPT/作者的审查重点

1. 是否接受 `CONTRIBUTING_FACTOR`，并把 66 计算节点设为后续综合压力
   基准；
2. 是否接受 22 节点 50% 失败作为当前 UDP 无可靠传输边界，而不是要求
   N1.6 内实现重传；
3. 是否需要在后续阶段补充 FlowMonitor drop-reason 或 NetDevice
   Mac/Phy drop 诊断，以定位本次未被 queue trace 捕获的 25 个包；
4. 是否在本轮审查通过后创建 Draft PR；
5. N1.7 优先设计可靠 NetworkTransfer，还是先设计竞争感知 pacing。

停止：是。等待 GPT/作者审查；不创建 PR，不合并。
