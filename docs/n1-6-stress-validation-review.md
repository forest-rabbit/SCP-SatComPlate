# SatCompute N1.6 本地实施与压力验证审查报告

> 日期：2026-07-27
>
> 分支：`feature/n1-task-compute`
>
> 正式 review base：`71f23ef20bd0e342096e1c25c0a3e6e452d2abdc`
>
> 历史 N0 里程碑：`d67ca0a164db5b1eacca04d4fcca3a1a295ca8cf`
>
> 已完成本地验证的代码 HEAD：`c1541a4951c682376f600cb8185205df6db044a0`
>
> 结论：`NOT_READY_TO_OPEN_PR`

本报告用于 GPT/作者审查当前 N1/N1.6 分支。报告提交本身只增加本文档，
不改变已经在 `c1541a4` 上验证过的程序、输入和测试逻辑。大型 TaskTrace、
FlowMonitor 输出、CSV 指标和性能日志均保留在 `/tmp`，没有提交仓库。

## 1. 当前结论

N1.6 的生成器、preflight、通用输出检查器、静态 2 Gbit/s 压力拓扑和
轻量验证 fixture 已完成。Level 0～Level 3 全部通过，Level 4-A 的 25%
综合场景在每设备 32 MB 队列下通过且零丢包。

Level 4-B 的 50% 综合场景分别使用 32 MB 和 64 MB 队列运行。两次仿真
都完整推进到仿真时间 1000 s，随后因为仍有任务处于
`RESULT_TRANSFERRING`，在结束完整性检查中退出。它们不是中途被外部超时
或内存不足终止。

根据压力方案的停止条件，本轮没有继续运行 75% 和 109 GB 最大综合场景，
也没有创建 PR。

## 2. N1.6 提交

完成本地验证的代码 HEAD 包含以下五个 N1.6 提交：

```text
7983e5e fix: scale task output checks and add generic validation
564964f test: add deterministic N1 stress workload inputs
8991ec9 test: add deterministic task workload preflight
f8bcd07 test: add layered N1 stress validation smoke
c1541a4 fix: validate FCFS ordering for arbitrary task runs
```

主要新增或加固内容：

- `contrib/satcompute/tools/generate-task-workload.py`
- `contrib/satcompute/tools/preflight-task-workload.py`
- `contrib/satcompute/tools/check-task-output.py`
- `contrib/satcompute/tools/generate-stress-topology.py`
- `contrib/satcompute/input/topology/json/examples/xw-66sat-static-2g/`
- `contrib/satcompute/input/topology/json/resources/workload/`
- `contrib/satcompute/input/traffic/json/task/test/stress-generated-40.json`
- `.github/workflows/satcompute-smoke.yml` 中的轻量生成器、preflight 和
  40-task 端到端验证

Level 2～Level 5 的大型压力运行没有加入 GitHub CI。

## 3. 生成器合同

生成器版本为 `1.0.0`，本轮使用规则版本 `n1.6-v1`。

确定性方法：

```text
SHA-256(seed, rules_version, task_id, field_name)
```

已经验证：

- 相同 seed 和参数逐字节生成相同 TaskTrace 与 workload summary；
- 不同 seed 生成不同 TaskTrace；
- TaskTrace 继续使用 schema `0.1` 的八字段 closed-world 合同；
- workload summary 记录 TaskTrace SHA-256 和全部聚合量；
- 类别、尾部数量和尾部类别分配均使用整数 basis points 与
  largest-remainder；
- 所有 output byte 规则均为整数运算；
- 每个类别分别验证 input/work 正相关。

默认类别比例：

```text
image-enhancement    1500 bp
image-detection      2500 bp
dnn-inference        3500 bp
preprocess-compress  2500 bp
```

最大场景严格生成：

```text
2000 tasks
109000000000 input bytes
20 x 1000000000-byte tasks
40 x 500000000-byte tasks
arrival window: 1s..600s, uniform
```

最大场景类别数量为 `300/500/700/500`。大尾部按 75% 分给
`preprocess-compress`、25% 分给 `image-enhancement`。四个类别的
Spearman input/work 系数均约为 `0.99988～1.0`。

最大 TaskTrace 的 SHA-256 为：

```text
3908783dbacd1a517a66a91f93b839f11e6c7dff633504f18a27fd2b5fb36ba7
```

## 4. Preflight 与通用检查器

Preflight 在仿真前检查：

- 拓扑、ComputeProfile、TaskTrace 和 workload summary 合同；
- 节点、任务、transfer ID、UDP source port 和整数范围；
- 精确输入预算及 summary SHA-256；
- size-aware/fixed packetization、UDP payload、MTU 和无 IPv4 分片；
- INPUT/RESULT 包数和最短路径 packet-hop 估算；
- 每计算节点工作量、理论利用率和推荐仿真时长；
- ISL 数量、设备队列数量、每队列容量、聚合理论容量和排空时间。

通用检查器入口：

```bash
python3 contrib/satcompute/tools/check-task-output.py run \
  --topology-dir=... \
  --compute-profile=... \
  --task-trace=... \
  --workload-summary=... \
  --output-dir=...
```

它验证完整状态转换、计算时间、非抢占 FCFS、transfer ID/端口、
packetization、收发字节、FlowMonitor 零丢包、ECMP route evidence、
运行聚合量和 workload SHA。route evidence 使用 five-tuple 索引，
FCFS 使用排序，整体没有明显的 O(N²) 检查路径。

## 5. 拓扑与带宽

原始 `xw-66sat` 没有被修改，其 JSON 中：

```text
link_bandwidth = 10000000 kbps = 10 Gbit/s
```

压力测试使用独立的 `xw-66sat-static-2g`：

```text
66 satellites
132 undirected ISLs
link_bandwidth = 2000000 kbps = 2 Gbit/s
```

该压力拓扑只有 `nodes_0s.json` 和 `topology_0s.json`。这是有意设计的
静态 fixture：0 s 加载后一直保持到仿真结束，不发生节点或链路变化，
因此不需要复制 10 s、20 s 等冗余快照，也不触发无意义的路由重算。
它不替代用于验证定时快照加载的原始 66 星输入。

132 条无向 ISL 产生 264 个 PointToPointNetDevice 发送队列。队列容量是
上限而非启动时预分配内存。

## 6. 本地构建与回归

以下命令在 `c1541a4` 上通过：

```bash
./waf configure --disable-examples --disable-tests --enable-modules=satcompute
./waf build
```

回归结果：

- N0 static/dynamic/canonical：通过；
- 5000 varied transfers：53,100 packets，零丢包；
- mixed large workload：41,507 packets，零丢包；
- 单任务 ECMP、FCFS、异构 R/2R、TaskTrace 顺序确定性和
  ComputeProfile 顺序确定性：通过；
- 40-task 生成器端到端 fixture：40/40 tasks、80/80 transfers、零丢包；
- 当前通用检查器重新验收 Level 2、Level 3 和 25% 输出：全部通过。

Level 2 仿真产生于 `f8bcd07`，之后到 `c1541a4` 只修改了 Python 通用
检查器，没有改变 C++ 仿真路径或压力输入；这些输出已经使用
`c1541a4` 的检查器重新验收。

## 7. 已完成压力结果

所有下表中的完成场景均满足：

```text
all tasks COMPLETED
all INPUT/RESULT transfers complete
FlowMonitor lost_packets = 0
generic checker PASS
```

| 场景 | 仿真时间 | tasks/transfers | INPUT bytes | OUTPUT bytes | 包数 | packet hops | wall-clock | peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Level 1 | 20 s | 40/80 | 4,000,000 | 3,332,332 | 7,199 | 32,150 | 3.78 s | 未单独记录 |
| Level 2-500 | 100 s | 500/1,000 | 4,096,000 | 23,433,252 | 27,433 | 113,598 | 12.77 s | 64,528 KB |
| Level 2-1000 | 200 s | 1,000/2,000 | 8,192,000 | 46,180,167 | 54,173 | 232,349 | 25.43 s | 82,304 KB |
| Level 2-2000 | 400 s | 2,000/4,000 | 16,384,000 | 94,077,643 | 110,016 | 470,849 | 52.06 s | 116,640 KB |
| Level 3 compute | 240 s | 500/1,000 | 4,096,000 | 25,065,859 | 29,012 | 123,179 | 13.93 s | 64,060 KB |
| Level 4-A 25% | 1000 s | 500/1,000 | 27,250,000,000 | 8,473,331,744 | 2,283,410 | 9,861,490 | 17:02.59 | 67,984 KB |

25% 场景还包含：

```text
compute work units: 1194978357
large tail: 5 x 1 GB, 10 x 500 MB
mean/max task delay: 2.014 s / 10.935 s
max compute queue length: 2
max per-node full-run utilization: 4.273%
aggregate active-window utilization: 6.034%
```

## 8. 队列校准

25% 场景使用相同 TaskTrace、seed、拓扑和 ComputeProfile：

| 每设备队列 | 264 队列聚合理论容量 | 排空时间/2 Gbit/s | 结果 |
|---:|---:|---:|---|
| 8,000,000 bytes | 1.967 GiB | 32 ms | 失败：task 143 INPUT 未完成 |
| 16,000,000 bytes | 3.934 GiB | 64 ms | 失败：同一 task 143 |
| 32,000,000 bytes | 7.868 GiB | 128 ms | 通过：500/500，零丢包 |
| 64,000,000 bytes | 15.736 GiB | 256 ms | 25% 未继续；50% 下仍失败 |

因此 25% 的最小通过值为 32 MB，但该值不能保证 50% 场景成功。

## 9. 50% 场景停止证据

50% workload：

```text
tasks: 1000
INPUT bytes: 54500000000
OUTPUT bytes: 17735650347
compute work units: 2390026806
large tail: 10 x 1 GB, 20 x 500 MB
INPUT packets: 3254828
RESULT packets: 1198246
total packets: 4453074
estimated packet hops: 18942110
arrival window: 1.318s..599.410s
simulation duration: 1000s
```

结果：

| 每设备队列 | wall-clock | peak RSS | 结束检查发现的首个未完成任务 |
|---:|---:|---:|---|
| 32 MB | 32:58.68 | 85,620 KB | task 57，RESULT transfer 114 |
| 64 MB | 33:03.27 | 85,760 KB | task 906，RESULT transfer 1812 |

两次都已经运行到仿真时间 1000 s。当前 `ValidateCompleted()` 遇到第一个
未完成任务就终止，所以只能证明“至少一个任务未完成”，不能从现有输出
得出未完成任务总数。

64 MB 场景中的 task 906：

```text
source node: 18
compute node: 15
result node: 28
INPUT bytes: 1000000000
OUTPUT bytes: 289600000
RESULT transfer: 1812
RESULT_TRANSFERRING since: 424.529159580s
```

其 RESULT 首跳纯序列化时间约为 1.16 s，即使加入三跳传播和每跳
64 MB 队列的理论排空时间，也不应持续到 1000 s。因此失败不是
仿真时长不足。

## 10. 当前失败机制判断

代码证据：

1. `NetworkTransferApplication` 使用 UDP；
2. 每条 transfer 独立按照自身首跳链路的序列化时间发送；
3. 多条并发流可能分别以 2 Gbit/s 注入，并在共享下游输出链路形成
   瞬时过载；
4. ISL 设备使用有限容量 DropTail 队列；
5. UDP 路径没有 ACK 或重传；
6. Receiver 只有在 `receivedBytes == expectedBytes` 时才触发完成。

因此，最符合现有证据的解释是：竞争期间至少一个 UDP 数据报没有抵达
Receiver；没有重传时，该 transfer 会永久保持未完成。32 MB 与 64 MB
出现不同受害 transfer，也符合瞬时竞争和 ECMP 路径碰撞，而非固定非法
TaskTrace。

目前不能确认具体丢包链路或精确缺失包数。原因是 `satcompute.cc` 在
`Simulator::Run()` 后先执行 `TaskCoordinator::ValidateCompleted()`，
成功后才收集并写出 FlowMonitor、transfer 和 task 指标。失败时程序在
指标落盘之前退出。

## 11. 最大场景 preflight

109 GB/2000-task preflight 已通过：

```text
INPUT bytes: 109000000000
OUTPUT bytes: 35877906094
compute work units: 4779959062
20 x 1 GB, 40 x 500 MB
INPUT packets: 6277117
RESULT packets: 2447223
total packets: 8724340
estimated packet hops: 37406704
max UDP source ports on one node: 122 / 55536
recommended duration lower-bound: 767.840s
max per-node full-run compute utilization: 16.06%
max per-node arrival-window compute utilization: 26.82%
```

该结果只证明输入合同、端口、packetization、计算容量和 1000 s 下界检查
通过，不代表 109 GB 实际运行通过。由于 50%/64 MB 已触发停止条件，
75% 和 109 GB 均未实跑。

## 12. 复现 50%/64 MB

重新生成 50% TaskTrace：

```bash
python3 contrib/satcompute/tools/generate-task-workload.py \
  --nodes-file=contrib/satcompute/input/topology/json/examples/xw-66sat-static-2g/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/json/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-count=1000 \
  --total-input-bytes=54500000000 \
  --seed=20260726 \
  --rules-version=n1.6-v1 \
  --arrival-start-ns=1000000000 \
  --arrival-end-ns=600000000000 \
  --arrival-mode=uniform \
  --large-1gb-count=20 \
  --large-500mb-count=40 \
  --scenario-scale-bp=5000 \
  --non-tail-min-input-bytes=1048576 \
  --non-tail-max-input-bytes=300000000 \
  --output-task-trace=/tmp/satcompute-n1-6-50-task-trace.json \
  --output-workload-summary=/tmp/satcompute-n1-6-50-summary.json
```

运行 64 MB 场景，当前机器预计约需 33 分钟：

```bash
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/json/examples/xw-66sat-static-2g \
  --computeProfile=contrib/satcompute/input/topology/json/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --taskTrace=/tmp/satcompute-n1-6-50-task-trace.json \
  --simulationDuration=1000 \
  --offeredLoad=0 \
  --taskLogMode=silent \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-n1-6-50-64m-output"
```

## 13. 建议 GPT 重点审查

请重点判断：

1. 是否先增加“失败诊断模式”：在不关闭完成性检查的前提下，枚举全部
   未完成任务和 transfer，并在退出前记录已发送/已接收字节、包数、
   FlowMonitor loss 和可获得的 DropTail drop evidence；
2. N1 是否继续保持 UDP 语义并增加应用层可靠重传，或改为 TCP/其他可靠
   bulk transfer；
3. 是否需要把每流首跳 2 Gbit/s pacing 改为竞争感知或全局限速；
4. 是否保持冻结 workload 不变，还是允许调整大任务的到达相关性、
   source/compute/result 映射或并发度；
5. 如果本轮只声明 25% 为当前平台已验证能力，是否允许先进入 PR，还是
   必须先解决 50% 和 109 GB。

不建议在缺少丢包证据时直接把队列提高到 128 MB。64 MB 已对应
15.736 GiB 的全部设备队列理论容量，而且增大队列只改变了首个失败
transfer，没有解决可靠完成问题。

## 14. 当前门槛状态

```text
Local implementation: complete
Level 0: PASS
Level 1: PASS
Level 2: PASS
Level 3: PASS
Level 4-A 25%: PASS at 32 MB
Level 4-B 50%: FAIL at 32 MB and 64 MB
Level 4-C 75%: NOT RUN
Level 5 109 GB/2000 preflight: PASS
Level 5 109 GB/2000 actual run: NOT RUN
Open-PR readiness: NOT_READY_TO_OPEN_PR
Merge readiness: NOT_APPLICABLE
```

本轮未引入 `TaskProfileCatalog`、`task_profile_id`、failure、checkpoint、
backup、recovery、RTO 或 RPO，也没有修改 `src/internet` 或
`src/point-to-point`。
