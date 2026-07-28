# SatCompute N1 本地实施与压力收尾审查报告

> 日期：2026-07-27
>
> N1 关闭日期：2026-07-28
>
> PR1 合并提交：`34d76b97ad2e0027c882452bcc2f641758a1522e`
>
> PR2 正式压力测试代码 HEAD：`f1b6f4eff3e9dccc828c3a6faa38806bf4036344`
>
> PR2 最终审查 HEAD：`66f8d770fa24326282b3518e438355ac72e2471c`
>
> PR2 合并提交：`32d70374cf0f93396845e5f067fb9beb560fc6d9`
>
> 当前状态：`N1_CLOSED`
>
> 已合并 PR：[#1](https://github.com/forest-rabbit/SatCompute/pull/1)、
> [#2](https://github.com/forest-rabbit/SatCompute/pull/2)

本报告是 N1 压力验证的唯一审查报告，前半部分保留 PR1 的诊断过程和
22/66 计算节点对照，后半部分记录 PR2 的正式 66 节点压力矩阵。大型
TaskTrace、FlowMonitor 输出、CSV 指标和性能日志全部保留在 `/tmp`，没有
提交仓库。PR2 没有实现可靠重传或竞争感知 pacing，只增加结束判定策略、
通用压力结果检查和最终能力边界。

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

在 `500c901` 上使用相同冻结输入再次运行 22 节点场景，精确复现
`999/1000`、FlowMonitor lost 25，但新增 UDP socket Drop 探针记录为 0。
人为把接收缓冲区缩小到 1000 bytes 的最小 fixture 则记录
FlowMonitor `4/4/0`、application rx 0、UDP socket Drop 4。两种计数特征
不同，因此现有证据不支持 UDP socket 接收缓冲区溢出是 transfer 1812
失败的直接原因。精确丢弃层不再作为 N1 阻塞项继续追查。

本阶段同时冻结压力测试判定：高压力运行允许部分任务未完成。
`PARTIAL_COMPLETION` 不等于无效实验；只要状态、字节、时间戳和汇总合同
一致，结果就应作为平台能力边界如实报告。

## 2. 本轮诊断提交

```text
3c29433 fix: record incomplete task and transfer diagnostics
bd89358 test: add per-link queue drop and flow concentration diagnostics
2e7b5e3 fix: accept FlowMonitor-only failure diagnostics
49e33f7 fix: make failure diagnostics opt-in
6a3c7f3..fdb5c99 refactor: split metrics writers by responsibility
b0c3621 feat: configure UDP receiver buffer
6d9f9fc fix: trace UDP receiver buffer drops
500c901 feat: write UDP socket drop diagnostics
```

`3c29433` 将严格失败改为“先落盘、后返回非零”，并增加：

- 全部未完成 task/transfer 枚举；
- partial sender/receiver bytes 与 packets；
- 失败运行中的 FlowMonitor 逐流指标；
- `diagnostics/failure/diagnostic-summary.json`；
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

`49e33f7..fdb5c99` 将失败诊断设为显式 opt-in，并把原先职责过重的
`metrics.cc` 拆为 flow、transfer、task、run-summary 和 failure-diagnostics
writer。该重构不调度 Simulator 事件，也不修改任务、计算、路由或传输状态。

`b0c3621..500c901` 增加默认值为 131072 bytes 的
`receiverRcvBufBytes` 实验参数，并在诊断模式下直接连接 UDP socket
`Drop` trace。1000-byte 最小 fixture 同时验证事件、聚合和检查器；正式
22 节点复测记录 UDP socket Drop 0，因此该参数保留为诊断控制项，不作为
强制完成压力任务的调优手段。

失败运行新增文件：

```text
diagnostics/failure/incomplete-tasks.csv
diagnostics/failure/incomplete-transfers.csv
diagnostics/failure/isl-queue-drops.csv
diagnostics/failure/isl-queue-drop-summary.csv
diagnostics/failure/udp-socket-drops.csv
diagnostics/failure/udp-socket-drop-summary.csv
diagnostics/failure/flow-link-concentration.csv
diagnostics/failure/diagnostic-summary.json
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

在 `500c901` 上通过：

- `./waf build`；
- N0 110 s smoke：66 星、132 ISL、0～110 s 共 12 个静态快照；
- 单任务 ECMP；
- non-preemptive FCFS；
- heterogeneous R/2R；
- TaskTrace 数组顺序确定性；
- ComputeProfile 数组顺序确定性；
- 40-task 生成 workload：40/40 tasks、80/80 transfers、零丢包；
- 4 星小设备队列 fixture：严格退出 1，记录 1 个有向队列 Drop、
  UDP socket Drop 0；
- 4 星 1000-byte 接收缓冲 fixture：严格退出 1，记录 UDP socket Drop 4、
  有向队列 Drop 0；
- 诊断关闭时清理全部 8 个旧失败诊断文件；
- 8 MiB 接收缓冲不改变小型成功用例的确定性输出，0-byte 参数被拒绝；
- 22 节点正式失败复测：999/1000，FlowMonitor lost 25、
  UDP socket Drop 0、ISL queue Drop 0，严格检查器通过；
- 66 节点对照：通用成功检查器通过。

GitHub Draft PR #1 的 `500c901` head 也通过
[`SatCompute deterministic smoke`](https://github.com/forest-rabbit/SatCompute/actions/runs/30237602991)。
审查报告更新后必须以新的最终 head CI 为合并门槛。

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
/tmp/satcompute-n1.6-buffer-a-128k-500c901/
```

性能和退出：

```text
wall-clock: 35:32.28
user time: 2132.32s
peak RSS: 88352 KB
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
UDP socket drop packets/bytes: 0/0
ISL queue drop packets/bytes: 0/0
```

FlowMonitor 的 `lostPackets` 包括超过默认 10 s 未再被观察到的包；它本身
不是丢弃层定位。这里 receiver 同时精确缺少相同的 25 个包，因此可以确认
这些数据报截至 1000 s 没有抵达目的应用。由于 FlowMonitor 与 application
都收到 4500 包，而 UDP socket Drop 为 0，这 25 个包没有呈现目的端 UDP
接收缓冲区溢出的计数特征。

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

### 并发发送背景

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

这些时间窗证明存在共享链路上的并发发送，但没有对应的 device-queue、
UDP socket Drop 事件，不能把 25 个包精确定位到 `27→28`，也不能据此把
竞争感知 pacing 设为 N1 的必要修复。N1 保留当前逐流首跳序列化语义，不
实现 max-min 公平、全局拥塞控制或新的 pacing 模型。

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
完成仍然没有协议保证，但部分完成是合法的压力结果，不再要求通过调参使
任意压力输入达到 100%。

## 10. PR1 冻结与 PR2 范围

PR1 已完成审查、CI、标记和合并：

```text
PR: #1
final head: fb4ccd2971b9043d157b1c095c1fa1e9a3d5d609
annotated tag: n1-pr1-final
merge commit: 34d76b97ad2e0027c882452bcc2f641758a1522e
CI: https://github.com/forest-rabbit/SatCompute/actions/runs/30240546813
CI conclusion: success
```

PR2 从该 merge commit 创建，只增加压力收尾所需的结束策略、检查合同、
66 计算节点配置和审查证据：

```text
7e9085c feat: add report-only task completion policy
f1b6f4e test: validate report-mode stress results
```

`taskCompletionPolicy=strict|report` 只控制任务指标落盘后的退出码。
`strict` 对未全部完成的任务返回非零；`report` 对相同状态正常退出。
两种策略写出相同的任务、传输、网络和诊断事实，均不改变 Simulator
停止时间、TaskCoordinator 状态机、FCFS、计算服务、分包、pacing、
ECMP 或收包完成条件。

`run-summary.json` 使用 `run_status=COMPLETE|PARTIAL` 表示任务完成事实。
外部检查器的 `stress` 入口同时验收两种状态，并将两个判断分开：

```text
RUN_VALID:
  状态前缀、时间戳、静态字段、FCFS、字节、包、端口、FlowMonitor、
  compute/run/diagnostic 聚合合同全部一致

CONTINUE:
  RUN_VALID 且 task completion rate >= 90%
```

因此 `PARTIAL` 不自动等于失败，`COMPLETE` 也不能绕过结构合同检查。

## 11. 66 节点正式压力配置

三组正式测试使用同一组平台参数：

| 项目 | 冻结值 |
|---|---|
| topology | 66 satellites / 132 undirected ISLs / static snapshot |
| JSON `link_bandwidth` | 2,000,000 Kbps，即每条 ISL 2 Gbit/s |
| compute nodes | 0–65，共 66 个 |
| compute rate | 每节点 1,500,000 work units/s |
| arrivals | uniform，1–600 s |
| simulation duration | 1000 s |
| routing | `global-hash-per-flow`，seed 1 |
| packetization | `size-aware` |
| ISL MTU | 65,535 bytes |
| ISL queue | 每个有向设备 64,000,000 bytes |
| UDP receive buffer | 131,072 bytes |
| completion policy | `report` |
| diagnostics | `failure` |

66 节点 ComputeProfile 已作为小型可复现配置提交：

```text
input/topology/json/resources/workload/
  xw-66sat-static-2g-all-compute-profile.json
SHA-256:
96e46227ac94241c11f53ef899379f160280da5ee9bd4ddc3829d1008d28dcc0
```

64 MB 是每个有向 PointToPointNetDevice 的队列上限。264 个队列的理论
总上限为 16,896,000,000 bytes，但 ns-3 不会在启动时预分配这 16.896 GB；
本轮峰值 RSS 仅为 129,548 KB。

TaskTrace 由 `generate-task-workload.py` v1.0.0 直接基于上述 66 节点
ComputeProfile 生成，不再使用 PR1 的手工 compute remap。共同生成参数为：

```text
seed: 20260726
rules version: n1.6-v1
arrival mode: uniform
arrival window: 1000000000..600000000000 ns
maximum tail: 20 x 1,000,000,000 bytes
maximum tail: 40 x 500,000,000 bytes
non-tail range: 1,048,576..300,000,000 bytes
scenario scale: 5000 / 7500 / 10000 basis points
```

大型 TaskTrace、summary、preflight 和仿真输出只保留在：

```text
/tmp/satcompute-n1-closure/<scenario>/
  f1b6f4eff3e9dccc828c3a6faa38806bf4036344/
```

## 12. 正式输入与 preflight

| 场景 | tasks | INPUT bytes | OUTPUT bytes | 1 GB / 500 MB tails |
|---|---:|---:|---:|---:|
| 50% | 1,000 | 54,500,000,000 | 17,735,650,347 | 10 / 20 |
| 75% | 1,500 | 81,750,000,000 | 27,513,294,080 | 15 / 30 |
| 109 GB | 2,000 | 109,000,000,000 | 35,877,906,094 | 20 / 40 |

输入哈希：

| 场景 | TaskTrace SHA-256 | workload-summary SHA-256 |
|---|---|---|
| 50% | `e3cfa407dba1066c6771df0a66c2490c91340777ba54e4694b5de0dcd7a89c99` | `1b3ebd587db8f944b3ded95217f5e484e319fa346e6a58cbb1c68445c743fa80` |
| 75% | `b2fc74f11b2074c7f29772464cf53877b6946f9905677f8c43f21c5740708237` | `2a82521eec2f2ee2ac11666480df02344153da521cd7c2e5a8bed27bdcf43eb3` |
| 109 GB | `6e795f0a841d4e2fffd522f97dc40d2662ad572af69855cde191d7b1060a5e84` | `c5f52efa028023e15e8e4f25cc62f47cf75a614cdc5fdf7e6dbe25e365826d6c` |

Preflight 均通过：

| 场景 | INPUT packets | RESULT packets | total packets | packet hops | duration lower bound |
|---|---:|---:|---:|---:|---:|
| 50% | 3,254,828 | 1,198,246 | 4,453,074 | 19,153,763 | 637.770 s |
| 75% | 4,754,797 | 1,830,169 | 6,584,966 | 28,707,289 | 653.907 s |
| 109 GB | 6,277,117 | 2,447,223 | 8,724,340 | 37,455,654 | 665.637 s |

这里的 packet 数是 TaskTrace 中全部 INPUT 和 RESULT transfer 完成时的
派生应用数据报数。PARTIAL 运行中尚未启动的 RESULT transfer 不会出现在
实际 FlowMonitor Tx 中，所以预估总包数可以高于实际发送数。

## 13. 50% / 75% / 109 GB 正式结果

三个场景均正常仿真到 1000 s、进程退出码为 0、checker 合同通过：

| 场景 | run status | completed tasks | transfers completed | FlowMonitor tx/rx/lost | checker |
|---|---|---:|---:|---:|---|
| 50% | COMPLETE | 1000/1000（100%） | 2000/2000 | 4,453,074 / 4,453,074 / 0 | RUN_VALID / CONTINUE |
| 75% | PARTIAL | 1493/1500（99.5333%） | 2988/3000 | 6,578,160 / 6,578,106 / 54 | RUN_VALID / CONTINUE |
| 109 GB | PARTIAL | 1992/2000（99.6%） | 3986/4000 | 8,708,834 / 8,708,405 / 429 | RUN_VALID / CONTINUE |

完成任务的端到端延迟：

| 场景 | mean | p95 | max |
|---|---:|---:|---:|
| 50% | 1.994960 s | 3.695895 s | 10.903344 s |
| 75% | 2.029024 s | 3.746318 s | 11.485186 s |
| 109 GB | 2.049453 s | 3.787960 s | 11.058328 s |

计算侧和本机资源：

| 场景 | max compute queue | max compute utilization | wall-clock | peak RSS |
|---|---:|---:|---:|---:|
| 50% | 1 | 3.0842% | 35:32.94 | 88,208 KB |
| 75% | 3 | 4.6467% | 53:48.05 | 109,244 KB |
| 109 GB | 2 | 5.8395% | 1:10:41 | 129,548 KB |

75% 的 7 个未完成任务：

```text
INPUT_TRANSFERRING: 201, 644, 652, 733, 1246
RESULT_TRANSFERRING: 99, 1253
incomplete transfers: 12
ISL directed-queue drops: 0
UDP socket drops: 0
```

109 GB 的 8 个未完成任务：

```text
INPUT_TRANSFERRING: 469, 623, 1143, 1218, 1261, 1629
RESULT_TRANSFERRING: 969, 1899
incomplete transfers: 14
ISL directed-queue drops: 0
UDP socket drops: 0
```

两个 PARTIAL 场景都没有 `QUEUED` 或 `RUNNING` 任务，计算队列很浅、
利用率低；未完成项停留在 INPUT/RESULT 传输阶段。该事实只说明本轮边界
主要表现为少量传输尾部，不能据此把 FlowMonitor `lostPackets` 精确归因
到某个未观测丢弃层。

## 14. 当前能力边界与 N1 判定

| 场景 | 状态 |
|---|---|
| 25% / 22 compute / 32 MB | PASS，500/500，零丢包 |
| 50% / 22 compute / 64 MB | RUN_VALID / PARTIAL，999/1000，lost 25 |
| 50% / 66 compute / 64 MB | RUN_VALID / COMPLETE，1000/1000，零丢包 |
| 75% / 66 compute / 64 MB | RUN_VALID / PARTIAL，1493/1500，99.5333% |
| 109 GB / 66 compute / 64 MB | RUN_VALID / PARTIAL，1992/2000，99.6% |

N1 的压力目标不是强制全部任务完成，而是让平台在高压力下稳定执行、如实
保留未完成状态并输出相互一致的指标。三组正式场景全部满足核心合同，且
完成率全部高于冻结的 90% 继续门槛。因此本报告判定：

```text
PR2 stress matrix: PASS
N1 capability boundary: ESTABLISHED
N1 status: CLOSED
```

PR2 已在作者/GPT 审查通过后合并；对应 smoke workflow
[运行 #30275220956](https://github.com/forest-rabbit/SatCompute/actions/runs/30275220956)
结论为 `success`。N1 里程碑由 annotated tag `n1-complete` 正式冻结。

## 15. 本轮没有实现

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
NetworkTransfer、竞争感知 pacing、checkpoint 和动态拓扑均不属于 N1
压力收尾；351/720 星与 Hypatia 推迟到独立 N2 阶段。

## 16. Pre-Hypatia FqCoDel / ECMP 诊断补充

> 诊断日期：2026-07-28
>
> 诊断基线：`n1-complete`
>
> 诊断分支：`investigate/n1-fqcodel-ecmp`

本节是 N1 关闭后的非阻塞诊断补充，不修改本报告第 14 节的 N1 判定。

### 16.1 两级排队模型

`Ipv4AddressHelper::Assign()` 会为支持 `NetDeviceQueueInterface` 且尚未配置
QueueDisc 的单队列 PointToPointNetDevice 安装默认
`FqCoDelQueueDisc`。因此当前 ISL 同时存在：

```text
traffic-control FqCoDel QueueDisc
→ PointToPointNetDevice DropTail queue
```

`islQueueBytes` 只设置后者；前者继续使用 ns-3.33 默认值，包括
`MaxSize=10240p`、`Target=5ms` 和 `Interval=100ms`。FlowMonitor
DropReason 3 `QUEUE` 表示 NetDevice queue，DropReason 4
`QUEUE_DISC` 表示 traffic-control QueueDisc。

诊断分支新增独立 `diagnostics/failure/flow-drop-reasons.csv` 和运行级
JSON 汇总，不修改现有 `network-flow-metrics.csv` 或
`network-flow-details.csv` schema。
未对应显式 DropReason 的 `lostPackets` 单列为
`UNATTRIBUTED_TIMEOUT`，不会被误归因到队列。

### 16.2 75% seed 1 全量复现与直接归因

由于原 `/tmp` 产物已不存在，本轮按冻结参数重新生成 75% 输入。TaskTrace
和 workload summary SHA-256 分别为：

```text
b2fc74f11b2074c7f29772464cf53877b6946f9905677f8c43f21c5740708237
2a82521eec2f2ee2ac11666480df02344153da521cd7c2e5a8bed27bdcf43eb3
```

Preflight 仍为 1,500 个任务、81,750,000,000 INPUT bytes、6,584,966
派生包和 28,707,289 packet-hops。完整 1000 s 仿真用时 52:48.08，
峰值 RSS 109,020 KB，退出码为 0，并精确复现：

```text
PARTIAL
1493 / 1500 tasks
2988 / 3000 transfers
FlowMonitor tx/rx/lost = 6578160 / 6578106 / 54
RUN_VALID / CONTINUE at 90%
```

54 个 loss 全部具有显式 `QUEUE_DISC`：

| transfer | drop packets | drop bytes |
|---:|---:|---:|
| 198 | 11 | 704,308 |
| 1287 | 1 | 64,028 |
| 2491 | 1 | 8,220 |
| 1303 | 38 | 2,433,064 |
| 2506 | 1 | 64,028 |
| 1465 | 1 | 64,028 |
| 401 | 1 | 64,028 |

汇总证据为：

```text
QUEUE_DISC packets/bytes = 54 / 3401704
QUEUE packets = 0
UDP socket drop packets = 0
UNATTRIBUTED_TIMEOUT packets = 0
```

因此此前 `diagnostics/failure/isl-queue-drops.csv=0` 与
`FlowMonitor lostPackets=54` 并不矛盾：前者只连接 device DropTail
trace，后者还观察默认 FqCoDel。

### 16.3 局部 replay 与 seed 对照

244.526–246.320 s 的三个受损 transfer 构成自足竞争簇：

| transfer | seed 1 路径 | drop packets |
|---:|---|---:|
| 198 | `36→37→38→39→40→51→62` | 11 |
| 1287 | `35→36→37→38→49→60→5` | 1 |
| 2491 | `35→36→37→48` | 1 |

5 秒 replay 保留原 size、相对到达时间和 source-port ordinal；38 条
1-byte 占位流在 0 s 完成，只用于保持五元组，不参与后续竞争。Replay
精确保留三条目标流每跳候选、selected index、gateway、interface、hash
和 selection reason，并复现相同的 11/1/1 个 `QUEUE_DISC`。

仅改变 replay 的 ECMP seed：

| seed | 目标流共享有向边 | lost | mean delay |
|---:|---|---:|---:|
| 1 | `35→36`×2，`36→37`×3，`37→38`×2 | 13 | 223.787 ms |
| 2 | 无 | 0 | 56.7752 ms |
| 3 | `36→37`×2 | 0 | 77.5558 ms |

这证明该 replay 的丢弃与固定 ECMP 映射造成的路径集中有关，但不证明 seed
2 或 seed 3 对完整 workload 普遍更优。FlowMonitor DropReason 不包含
丢弃节点/接口，因此具体 FqCoDel 实例仍属于路径证据推断。

长期只保留通用 DropReason 输出、运行级汇总、检查器和小型 FqCoDel
fixture；不提交完整 75% 输入、运行输出、逐包探索日志或 FqCoDel 参数修改。
后续 HRW 只解决候选集合变化时的最小重映射，不作为负载感知或完成率优化。
