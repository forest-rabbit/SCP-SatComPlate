# SatCompute ns-3.48 运行说明

> `main` 使用官方 ns-3.48，`legacy/ns-3.33` 永久保留为只读行为基线。本文以
> ns-3.33 中文 README 的章节和合同为主体。生产入口只接受 `para.cc` 默认值及
> 同名 CLI 覆盖；CSV 只承担星座物理结构，JSON 承担拓扑回放、流量、算力和
> 任务等相互独立的数据合同。

最终边界见 [v0.3 平台规格](../../docs/specs/platform-v0.3.md)，逐项结果见
[ns-3.33 到 ns-3.48 迁移矩阵](../../docs/plans/ns3-33-to-48-matrix.md)。

## 执行模型

1. 由 `para.cc` 默认值和同名 CLI 覆盖得到平台运行参数；
2. online 模式读取 ns-3.48 原生 LEO shell CSV，由原生圆轨道 mobility
   实时计算稳定卫星 ID 对应的 ECEF 坐标；replay 模式读取配对的
   `nodes_<time>s.json` 与 `topology_<time>s.json` 全量快照；
3. 按外部卫星 ID 的稳定顺序创建节点和 `/32` service 地址，按无向端点 ID 的
   canonical 顺序创建固定候选 ISL 和 `/30` 网段；
4. 每个网络 tick 更新位置、距离门控和 distance 时延；只有有效链路集合变化时
   才调用原生 `GlobalRouteManager` 重算当前 hop-based IPv4 路由；
5. 可选安装 NetworkTransfer JSON 或分离输入的任务闭环；无输入时只运行拓扑；
6. 拓扑导出按独立间隔输出卫星 ID、ECEF `x/y/z` 和有效链路，不改变网络 tick；
7. 输出网络、传输、任务、路由和计算节点的结构化结果，不生成第二份运行配置。

程序不会创建地面站，不解析 cluster，也不支持 CSV 拓扑。故障执行、前后端
传输、IPv6 与 SRv6 属于后续阶段，不在本轮兼容迁移中提前实现。

## 源码布局

目标布局保持 ns-3.33 的职责边界：根目录 `satcompute.cc` 是平台入口，
`para.h/.cc` 是唯一平台参数入口；`topology/satellite-topology.*` 是统一 facade。
facade 内部的 `topology/orbit/`、`online/`、`replay/` 与 `export/` 保存 ns-3.48
新增的原生轨道、在线应用、JSON 回放和切片导出实现。`topology/snapshot/` 保存
快照数据类型、JSON 读取器和目录调度器；`topology/link/` 保存运行期 ISL 资源与
启停状态。facade 已恢复并成为平台唯一拓扑入口；分层 `metrics/` 已按 legacy
的 `core/`、`routing/`、`diagnostics/` 与顶层 recorder 完成恢复。

`routing/common/` 保存路由模式、五元组、候选和 FNV 值类型；
`routing/algorithm/` 分别实现 global-first、Hash、HRW、size-aware HRW 与
capacity-aware HRW，算法层不操作 sender、socket、Simulator 或可变拓扑对象；
`routing/state/` 分别维护通用 flow assignment、size-aware 声明字节账本和
capacity-aware 有向链路速率账本；`routing/ns3/` 只负责读取 ns-3 路由候选、
解析五元组、构造 `Ipv4Route`、维护 route epoch/cache 及在途包 transition
fallback。发送暂停、恢复、pacing 和 pending admission 仍由 `traffic/` 编排。

任务、流量和拓扑 JSON 共用
仓库根目录 `third-party/nlohmann/json.hpp` 中固定版本的 nlohmann JSON 单头文件
（MIT）。
它是运行时依赖，不属于 `tools/`；`tools/` 只保存输入生成、分析、检查和可视化
工具。轨道传播公式只在共享 C++ 核心中实现，Python 工具不复制轨道计算。

## 构建与默认值

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
./ns3 run "satcompute --simulationDuration=2"
./ns3 run "satcompute --simulationDuration=2 --topologyOnly=1"
./ns3 run "satcompute --help"
```

不带参数运行 `satcompute` 会按 `para.cc` 的完整默认实验执行 1000 秒仿真；日常
开发建议显式给出较短的 `simulationDuration`。

日常和 CI 配置都不启用 ns-3 全局 examples 或 tests，也不运行上游 `test.py`。
SatCompute 自有 C++ 检查作为普通 executable 构建，自有测试仍保存在原来的
`tests/` 层次：

```bash
python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

`para.cc` 当前目标默认值如下；时间和人工设置的间隔均以秒输入，平台解析后
统一转换为整数纳秒：

```text
simulationDuration       = 1000
constellationConfig      = contrib/satcompute/input/topology/constellations/synthetic-66.csv
topologySource           = online
topologyDir              = empty
islCandidateStrategy     = plus-grid
seamEnabled              = false
maxIslDistance           = 6174589
delayMode                = fixed
fixedDelay               = 0.008
networkUpdateInterval    = 20
islBandwidthBps          = 2000000000
islMtuBytes              = 1500
islQueueBytes            = 1500000
receiverRcvBufBytes      = 131072
routingMode              = global-capacity-aware-hrw
ecmpHashSeed             = 1
transferTrace            = empty
computeProfile           = empty
taskTrace                = empty
transferChunkMode        = fixed
transferPayloadBytes     = 1024
taskCompletionPolicy     = strict
topologyOnly             = false
topologySliceInterval    = 1
includeFinalTopologyState = true
outputDir                = /tmp/satcompute-output
transferLogMode          = summary
taskLogMode              = summary
diagnosticMode           = off
randomSeed               = 1
randomRun                = 1
```

## CI 与本地验证

阶段内的小分支和 PR 只运行与改动直接相关的本地测试，不触发 GitHub CI。
每个大阶段的全部 PR 合并并清理后，只在 `main` 上手动触发一次
`SatCompute CI`，依次运行 Python 合同测试、项目 C++ 测试、smoke 和 regression。

当前统一入口为：

```bash
contrib/satcompute/tests/integration/smoke/run-all.sh
```

阶段检查点再运行：

```bash
contrib/satcompute/tests/integration/regression/run-all.sh
```

当前回归已经合并 ns-3.33 的 topology-only、Hash/HRW/size-aware、
capacity-aware、任务、FCFS、strict/report、诊断与 canonical ordering 黄金
合同，以及 ns-3.48 新增的 online、独立导出间隔、网络应用周期和 replay
等价性测试。

## 参数合同

- `--simulationDuration`：有限正持续时间，单位为秒。
- `--constellationConfig`：ns-3.48 原生 LEO shell CSV 路径；不得包含仿真、
  时延、路由、workload、随机数或输出参数。
- `--topologySource`：`online` 或 `replay`。前者实时计算，后者读取全量切片。
- `--topologyDir`：`replay` 模式的 JSON 全量快照目录；online 模式必须为空。
- `--islCandidateStrategy`：固定候选 ISL 策略，当前为 `plus-grid`。
- `--seamEnabled`、`--maxIslDistance`：seam 候选开关和有效链路距离门限。
- `--delayMode`：`fixed` 或 `distance`；两者使用同一星座和固定候选身份。
- `--fixedDelay`：fixed 模式单向链路时延，单位为秒。
- `--networkUpdateInterval`：online/replay 网络状态应用周期，单位为秒。
  distance 实验可设 1 秒或 2 秒，fixed 实验可设 20 秒，均由输入决定；replay
  目录必须存在这些规则时刻的切片。
- `--islBandwidthBps`：每条 ISL 的 bit/s 数据率。
- `--islMtuBytes`：所有当前及后续 ISL 的 MTU，默认 1500。
- `--islQueueBytes`：所有当前及后续 ISL DropTail 队列字节容量，默认 1500000。
- `--receiverRcvBufBytes`：每个 UDP 接收 socket 的缓冲区，默认 131072 bytes。
- `--routingMode`：`global-first`、`global-hash-per-flow`、
  `global-hrw-per-flow`、`global-size-aware-hrw` 或
  `global-capacity-aware-hrw`，默认最后一种。
- `--ecmpHashSeed`：逐流 Hash、HRW、size-aware 与 capacity-aware 的确定性 seed。
- `--transferTrace`：可选 NetworkTransfer JSON。
- `--computeProfile`：`topology/resources` 下的静态计算能力 JSON。
- `--taskTrace`：独立生成或 fixture 中的任务到达 JSON。
- `--transferChunkMode`：`fixed` 或 `size-aware`，默认 `fixed`。
- `--transferPayloadBytes`：`fixed` 模式的 UDP payload 上限，默认 1024。
- `--taskCompletionPolicy`：`strict` 在任务未全部完成时写出指标后返回非零；
  `report` 写出相同结果后正常退出。默认 `strict`。
- `--topologyOnly`：不创建网络、路由、任务和指标对象，只生成轨道/拓扑切片。
- `--topologySliceInterval` 与 `--includeFinalTopologyState`：控制独立于网络 tick
  的切片间隔和是否包含仿真终点。
- `--outputDir`：结构化结果目录，默认 `/tmp/satcompute-output`；正式实验应显式
  填写仓库外的持久路径。
- `--transferLogMode`、`--taskLogMode`：`summary`、`verbose` 或 `silent`。
- `--diagnosticMode`：`off` 只保留基础指标；`failure` 在任务失败时额外
  采集未完成对象、ISL 队列 Drop 和路由集中度。
- `--randomSeed`、`--randomRun`：固定 ns-3 随机过程以复现实验。

`computeProfile` 与 `taskTrace` 必须同时指定，任务模式不能同时指定
`transferTrace`。NetworkTransfer 与任务模式都使用 UDP；只有显式设置
`topologyOnly=1` 时才不安装网络、NetworkTransfer 或 TaskCoordinator。

下面各节保留 ns-3.33 的业务、路由、指标和验证合同，并使用当前 ns-3.48
参数补齐可执行命令。旧 fixture、检查器和分层 metrics 均已恢复；Hypatia/TLE、
旧完整 scenario 配置和两个过渡 routing 输出按审计结论不迁移。

## NetworkTransfer

文件根节点只允许 `schema_version` 和 `transfers`，版本必须严格为 `0.1`。
每条 transfer 只允许：

```text
transfer_id
source_node_id
destination_node_id
size_bytes
arrival_time_ns
```

`transfer_id` 是文件内唯一正整数；源宿是存在且不同的外部卫星 ID；
`size_bytes` 是不含 UDP/IP/链路 header 的正应用 payload；到达时间必须非负且
严格早于仿真结束。

程序按 `transfer_id` canonical sort。`fixed` 模式使用全局 cap；
`size-aware` 模式固定使用以下可审计策略：

```text
size <= 1 MiB        -> 1024 bytes
1 MiB < size <= 64MiB -> 8192 bytes
size > 64 MiB        -> 64000 bytes
```

每条 transfer 的 effective payload 写入 `transfer-summary.csv`，最后一包使用
准确余量。第一包在 `arrival_time_ns` 发送；每包发送后重新查询该五元组
当前选定的首跳 PointToPoint 设备与 DataRate，并按当前包的链路序列化时间调度
下一包，不增加应用层人工 gap。
目的 UDP 端口固定为 9000，同一源卫星的 transfer 按
`(source_node_id,transfer_id)` 从源端口 10000 顺序派生。JSON 不接受包数、
包间隔、包长、端口、MTU或速率字段。

要求本次运行可能使用的最大 effective payload 加 28 bytes 后不超过
`islMtuBytes`，且 UDP payload 不超过 65507。因此 fixed 模式按
`transferPayloadBytes` 校验，size-aware 模式按 64000 bytes 校验，
NetworkTransfer 不依赖 IPv4 分片。

## Task 模式

Task 模式保持两类输入独立：

```text
topology/resources/...  ComputeProfile：节点静态计算能力
独立 TaskTrace JSON     任务、数据量、计算量与到达时间
```

`xw-66sat-static-2g-compute-profile.json` 是 22 个计算节点的受限对照；
`xw-66sat-static-2g-all-compute-profile.json` 覆盖卫星 0–65，是 66
计算节点正式压力矩阵的配置。大型压力 TaskTrace 和输出不提交仓库，精确
生成参数、输入哈希及 50%/75%/109 GB 结果记录在
`docs/reviews/n1-6-stress-validation-review.md`。

`ComputeProfile` 根对象只允许 `schema_version` 和 `compute_nodes`，版本为
`0.1`。每个计算节点只允许：

```json
{
  "node_id": 3,
  "compute_rate_work_units_per_second": 1000000
}
```

`TaskTrace` 根对象只允许 `schema_version` 和 `tasks`，版本同为 `0.1`。每个
任务只允许：

```json
{
  "task_id": 1,
  "source_node_id": 0,
  "compute_node_id": 3,
  "result_node_id": 0,
  "input_bytes": 4096,
  "output_bytes": 2050,
  "compute_work_units": 1000000,
  "arrival_time_ns": 100000000
}
```

两个解析器都采用 closed-world 校验，并按 ID canonical sort。
`compute_node_id` 必须出现在 ComputeProfile 中。每个任务严格派生输入
`2 × task_id - 1` 和结果 `2 × task_id` 两条 NetworkTransfer，共用同一套
分包、首跳串行化 pacing、接收完成和 ECMP 证据逻辑。

任务状态为：

```text
PENDING → INPUT_TRANSFERRING → QUEUED → RUNNING
        → RESULT_TRANSFERRING → COMPLETED
```

输入未完整接收时不会排队或计算。每个 ComputeProfile 节点是一个单服务台、
非抢占 FCFS 服务，排序键为 `(queue_enter_time_ns, task_id)`。整数服务时间为：

```text
ceil(compute_work_units × 1,000,000,000
     / compute_rate_work_units_per_second) ns
```

计算完成时立即启动结果传输；结果完整接收后任务才进入 `COMPLETED`。
`taskCompletionPolicy` 不改变上述状态机、调度、计算、传输、ECMP 或仿真
停止时间，只控制指标落盘后的退出码。`run-summary.json` 对完整运行写
`run_status=COMPLETE`，对未完全完成的任务运行写 `run_status=PARTIAL`，
并同时记录实际使用的策略。

单任务运行：

```bash
./ns3 run "satcompute \
  --topologySource=replay \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=10 \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/tests/fixtures/traffic/tasks/task-single-ecmp.json \
  --simulationDuration=10 \
  --taskLogMode=verbose \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-task-single"
```

FCFS 运行只需把 `taskTrace` 改为 `task-fcfs.json`，并把输出目录改为
`/tmp/satcompute-task-fcfs`。`tools/validation/check-task-output.py` 同时
验证单任务两段
ECMP、FCFS、异构算力和两类 JSON 数组换序确定性。

## 地址与路由

卫星按外部 `sat_id` 升序映射到 ns-3 节点。service 地址来自
`172.16.0.0/12` 的 `/32`，ISL 来自 `10.0.0.0/8` 的 `/30`。JSON 中
`links[]` 的排列以及 `node1_id/node2_id` 的端点方向都不影响地址分配。

`global-first` 完整使用原生 `Ipv4GlobalRouting` 首条路由。四种逐流模式都只
枚举公开可读的 exact service `/32` host routes，并按 gateway、output
interface、destination 和 mask 排序去重。

`global-hash-per-flow` 是 N1 基线，对以下 21 bytes 做 FNV-1a-64，再用
`hash % candidateCount` 选择候选；其既有结果保持不变：

```text
seed(8) + source IPv4(4) + destination IPv4(4) +
protocol(1) + source port(2) + destination port(2)
```

`global-hrw-per-flow` 对每个候选分别计算 Rendezvous/Highest Random Weight
分数并选择最大值：

```text
score(candidate) = FNV-1a-64(
  seed + five-tuple +
  gateway(4) + output interface(4) + destination(4) + mask(4)
)
```

分数相同时按上述候选身份的 canonical 顺序选择。分数不包含 route epoch 或
候选数组位置，因此候选集合不变时跨 epoch 结果不变；删除未选候选不会影响
该 flow，删除已选候选才会重选，新增候选也只迁移由新候选取得更高分的 flow。
这是 hop-by-hop 的稳定逐流选择，不读取队列、FqCoDel backlog 或实时负载，
也不实现逐包 ECMP、端到端 path pinning、pacing 或拥塞控制。

`global-size-aware-hrw` 只对已登记且 sender 仍在发送的
NetworkTransfer 生效。每条 flow 在当前节点首次查路时：

```text
1. 按纯 HRW 对完整 route candidate 排名；
2. 只比较 HRW 前两名；
3. 读取两者物理下一跳的逻辑预留：
   reserved_bytes[node, gateway, output interface]；
4. 选择预留较小者；相等时选择 HRW 第一名；
5. 以完整 candidate 身份建立 node+flow sticky assignment。
```

预留值使用该 transfer 的声明字节，不逐包递减。不同最终目的地只要共用同一
gateway 和 output interface，就进入同一个物理下一跳负载桶；destination 和
mask 仍参与 HRW 分数及 sticky 身份，但不拆分链路负载。拓扑 epoch 更新后，
完整 sticky candidate 仍存在就保持原选择；只有它消失时才释放并重选，恢复
候选不会让已有 flow 自动迁回。sender 把最后一个 payload 成功交给 UDP socket
后，释放该 flow 在全部节点的预留。已发送完的尾包和未登记 flow 回退纯 HRW，
且不会重新建立预留。

该模式不设置大流阈值；大 transfer 仅因声明字节更大而具有更高权重。它不读取
FqCoDel、DropTail、实时利用率或时延，也不实现周期采样、中途主动迁移、速率
控制、重传或全局流量工程。

`global-capacity-aware-hrw` 是当前默认模式，也是 size-aware 之后的当前迭代：
它从节点级负载选择进一步扩展到完整路径准入和发送速率控制。它仍然只使用 ns-3
全局路由给出的等价最短路径，不生成更长路径，也不是 KSP：

```text
residual(link) = configured_data_rate(link) - active_admitted_rate(link)
path_rate      = min(residual(link) for link in path)
```

每条 flow 到达时，在当前 ECMP 有向图中选择 `path_rate` 最大的完整路径；相同
剩余瓶颈带宽时按逐节点 HRW 顺序确定结果。选中的逐跳 candidate 会在 flow
期间固定，发送端按 `path_rate` 计算包含 UDP/IP/PPP 头的逐包间隔。如果所有
等价最短路径的剩余带宽均为零，该 flow 保留原始 arrival time，但延后首包
注入；活动 flow 完整到达目的节点并释放路径容量后，按到达顺序重试等待流。
因此互不共享有向 ISL 的 flow 仍可并行，共享低速瓶颈的 flow 不会继续各自按
首跳线速叠加注入。

每次 network tick 都应用网络状态；只有有效链路集合变化并重算 ns-3 全局路由
后，传输控制器才检查全部活动路径。
路径仍有效且方向总预留不超过新带宽时保持 sticky，不因其他路径更空闲而
主动迁移。路径失效时执行：

```text
ACTIVE(old path)
  -> pause unsent packets
  -> release every old-path assignment and rate reservation
  -> re-admit on the current ECMP graph
  -> ACTIVE(new path), or WAITING_ADMISSION when unavailable
```

失效活动 flow 优先于新到达 flow，并按原 arrival time 和 transfer ID 确定性
重试。无路或无剩余容量时等待下一次路由更新或其他 flow 释放容量，不会
触发 `candidate-invalid` 中止。已经离开源端、后续到达旧路径节点的在途包可以
使用确定性 `CAPACITY_AWARE_TRANSITION_FALLBACK` 继续转发，但不为它创建局部
路径预留。

当前迭代不执行非最短绕行，也没有 ACK/NACK 或重传。因此它保证动态路由
状态和容量账本一致、未发数据可暂停和恢复，不承诺物理链路关闭时已在途
UDP 包在任意时序下都不丢失。可靠恢复和节点故障属于后续阶段。

没有 exact host route 或不能解析合法五元组时回退原生行为。项目不复制
GlobalRouteManager、SPF 或私有 `LookupGlobal()`，也不使用随机逐包 ECMP。
当前验证范围是未发生 IPv4 分片的 UDP NetworkTransfer。

项目 C++ 测试覆盖并行 ECMP 准入、容量耗尽等待、完整路径失效释放和重新准入；
平台 smoke 使用 4 星 diamond 和两条同时到达的传输，验证真实 UDP 入口、完整
路径瓶颈 pacing、两条传输完成及结束时容量账本归零：

```bash
bash contrib/satcompute/tests/integration/smoke/run-capacity-aware-smoke.sh
```

完整动态恢复断言位于 `capacity-aware-routing-test.cc` 与
`network-transfer-engine-test.cc`，统一由 `tests/unit/run-cpp-tests.sh` 调用。

## Diamond 验证

静态场景重复运行：

```bash
./ns3 run "satcompute \
  --topologySource=replay \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=3 \
  --simulationDuration=3 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/diamond-4-static-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-static-a"

./ns3 run "satcompute \
  --topologySource=replay \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=3 \
  --simulationDuration=3 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/diamond-4-static-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-static-b"
```

动态场景在 2 秒断开一条支路、4 秒恢复：

```bash
./ns3 run "satcompute \
  --topologySource=replay \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=2 \
  --simulationDuration=6 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/diamond-4-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-dynamic"

python3 contrib/satcompute/tools/validation/check-ecmp-output.py \
  --first=/tmp/satcompute-ecmp-static-a \
  --second=/tmp/satcompute-ecmp-static-b \
  --dynamic=/tmp/satcompute-ecmp-dynamic
```

检查器验证静态双支路覆盖、重复输出一致、每条 transfer 的精确 payload，以及
动态 epoch 的 `2 → 1 → 2` candidates 和恢复后的确定性选择。

v0.3 平台按规则 network cadence 消费切片。下面复用 `0/2/4s` 动态 diamond，
以 HRW 验证候选删除与恢复；旧 `1/3/5s` 非规则 fixture 继续作为解析和算法黄金
输入，但不作为规则 cadence 的平台命令：

```bash
./ns3 run "satcompute \
  --topologySource=replay \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=2 \
  --simulationDuration=6 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/diamond-4-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=64000 \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-hrw-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-hrw-seed1-a"
```

阶段检查点对 seed 1 和 2 各重复两次，并由
`tools/validation/check-ecmp-output.py` 独立重算
HRW 分数，验证候选顺序、跨 epoch 稳定性、增删候选的最小迁移、seed
可复现性，以及旧 `global-hash-per-flow` 的固定黄金结果。

## Size-aware HRW 验证

`size-aware-static-transfers.json` 使用八条同时到达且大小不同的 flow，
验证 HRW 前两名、相等负载回退第一名、声明字节预留、第二候选分流及最终
释放。`size-aware-dynamic-transfers.json` 复用动态 diamond，验证候选顺序
变化不迁移、已选候选消失才迁移、恢复后旧 flow 不迁回，以及新 flow 可以
使用恢复后的较轻候选。两类场景各重复两次后运行：

```bash
python3 contrib/satcompute/tools/validation/check-size-aware-output.py \
  --static-hrw=<pure-hrw-output> \
  --static-first=<size-aware-static-a> \
  --static-second=<size-aware-static-b> \
  --dynamic-first=<size-aware-dynamic-a> \
  --dynamic-second=<size-aware-dynamic-b>
```

检查器独立重算 HRW 排名，逐事件重放物理下一跳预留账本，并检查最终
`active=0`、`assignments=0`、`reserved=0`。66 星中型本地场景使用
`tests/fixtures/traffic/tasks/size-aware-medium-60.json`：60 个集中到达任务、
3 GB 输入，其中
17 个输入大于 64 MiB；预估 243,028 个 UDP 包，不进入阶段 CI。可在上述命令
追加 `--medium-hrw=<dir> --medium-size=<dir>` 验证冻结的对照结果。

`n1-75-fqcodel-replay.json` 包含三条目标流和 38 条 1-byte source-port
占位流。旧 hash、纯 HRW 和大小感知模式的本地输出由
`tools/validation/check-size-aware-replay.py` 比较；占位流必须在 0 ns
完成释放，
0.1 s 目标流开始前总预留必须为零。

冻结 75% 压力输入不提交仓库。保留基线和大小感知输出时，可在主检查命令
追加 `--full-baseline=<hash-output> --full-size=<size-aware-output>`。检查器
固定核对 66 星、66 计算节点、2 Gbit/s ISL、1000 s、109,263,294,080
应用字节等场景合同，并要求任务完成数不低于 1493、QueueDisc 丢包少于
54、受害 transfer 不增加，以及 device queue、UDP socket 和未归因丢包
保持为零。最终本地结果和输入哈希记录在
`docs/reviews/pre-n2-size-aware-hrw-validation.md`。

## 变长规模输入

`input/traffic/workload/workload-5000-varied.json` 由
`tools/generation/generate-transfer-workload.py` 确定性生成。5000 条记录的
`size_bytes` 均不同，范围为 1024–81920 bytes；使用 4096-byte cap 时，每条
transfer 产生 1–20 个包，总计 53,100 个包和 207,357,501 应用字节。

```bash
./ns3 run "satcompute \
  --simulationDuration=8 \
  --transferTrace=contrib/satcompute/input/traffic/workload/workload-5000-varied.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=4096 \
  --islMtuBytes=9000 \
  --transferLogMode=summary \
  --routingMode=global-hash-per-flow \
  --outputDir=/tmp/satcompute-workload-5000"
```

## 混合大流量输入

`mixed-large-ci.json` 含两个 size-aware 阈值探针，以及 10 条大小互异且不小于
8 MiB 的大流量；其中包含 64 MiB 和 125,000,000 bytes（1 Gbit），一次运行
覆盖 1024、8192 和 64000-byte 三个 effective payload 分级：

```bash
./ns3 run "satcompute \
  --topologySource=replay \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=45 \
  --simulationDuration=45 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/mixed-large-ci.json \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=1500000 \
  --transferLogMode=summary \
  --routingMode=global-hash-per-flow \
  --outputDir=/tmp/satcompute-mixed-large-ci"

python3 contrib/satcompute/tools/validation/check-ecmp-output.py \
  --large=/tmp/satcompute-mixed-large-ci \
  --large-input=contrib/satcompute/tests/fixtures/traffic/transfers/mixed-large-ci.json
```

`mixed-large-local.json` 是不放入阶段 CI 的完整压力输入，含 10 条不同大流量，
范围为 128 MiB–1 GiB，并明确包含 256 MiB、512 MiB 和 1 GiB：

```bash
./ns3 run "satcompute \
  --topologySource=replay \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=340 \
  --simulationDuration=340 \
  --transferTrace=contrib/satcompute/input/traffic/workload/mixed-large-local.json \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=1500000 \
  --transferLogMode=summary \
  --routingMode=global-hash-per-flow \
  --outputDir=/tmp/satcompute-mixed-large-local"

python3 contrib/satcompute/tools/validation/check-ecmp-output.py \
  --large-local=/tmp/satcompute-mixed-large-local \
  --large-local-input=contrib/satcompute/input/traffic/workload/mixed-large-local.json
```

64000-byte effective payload 只用于降低大数据仿真的事件数量，不宣称真实卫星
网络使用 64 KB 物理帧。

## 失败诊断最小验证

下面的 4 星用例故意把每设备队列设为 1000 bytes，小于 1024-byte 应用
payload 加协议头后的单包大小。它只验证“失败后先落盘、再返回非零”以及
有向 ISL queue Drop 映射，不代表正式压力场景：

```bash
./ns3 run "satcompute \
  --topologySource=replay \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=2 \
  --simulationDuration=2 \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/tests/fixtures/traffic/tasks/task-single-ecmp.json \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=1000 \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --transferLogMode=silent \
  --taskLogMode=silent \
  --taskCompletionPolicy=strict \
  --diagnosticMode=failure \
  --outputDir=/tmp/satcompute-task-failure"

# 上一条命令的预期退出码为 3。
python3 contrib/satcompute/tools/validation/check-task-output.py failure \
  --topology-dir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --compute-profile="$PWD/contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json" \
  --task-trace="$PWD/contrib/satcompute/tests/fixtures/traffic/tasks/task-single-ecmp.json" \
  --output-dir=/tmp/satcompute-task-failure \
  --require-queue-drop
```

验证接收端缓冲区证据时，复用同一 fixture，将运行参数改为
`--islQueueBytes=1500000 --receiverRcvBufBytes=1000`，并把检查器末尾改为
`--require-udp-socket-drop`。该用例应由
`diagnostics/failure/udp-socket-drops.csv` 直接记录 socket 缓冲区 Drop；
FlowMonitor 可能仍将这些包记为 IP 层已接收。

正式压力运行使用
`--taskCompletionPolicy=report --diagnosticMode=failure`。无论结果为
`COMPLETE` 还是 `PARTIAL`，都可用同一个入口检查任务状态前缀、时间戳、
传输字节/包计数、计算节点计数、FlowMonitor 与失败诊断聚合：

```bash
python3 contrib/satcompute/tools/validation/check-task-output.py stress \
  --topology-dir=<topology-dir> \
  --compute-profile=<compute-profile.json> \
  --task-trace=<task-trace.json> \
  --workload-summary=<workload-summary.json> \
  --output-dir=<run-output> \
  --minimum-completion-rate-percent=90
```

检查器先输出 `RUN_VALID`，再按完成率给出 `CONTINUE` 或 `STOP`；低于阈值
表示停止后续更大压力场景，不表示当前部分完成结果的结构合同无效。

### FlowMonitor DropReason

`diagnosticMode=failure` 会额外写出
`diagnostics/failure/flow-drop-reasons.csv`。每行记录一个五元组的一种
非零 IPv4 FlowMonitor DropReason；`QUEUE` 表示 NetDevice 发送队列丢弃，
`QUEUE_DISC` 表示流量控制层 QueueDisc 丢弃。
`UNATTRIBUTED_TIMEOUT` 只表示 `lostPackets` 没有对应显式 DropReason，
不能据此推断具体丢弃层。

当前确定性门禁把 ISL device queue 缩到一个包以下，要求至少出现一个
`QUEUE`，并交叉验证 DropReason CSV、逐流详情和 `run-summary.json`：

```bash
contrib/satcompute/tests/integration/smoke/run-diagnostics-smoke.sh
```

`QUEUE_DISC` 映射和 checker 能力继续保留，但旧异构链路带宽 fixture 不再作为
平台黄金命令：v0.3 的 ISL 带宽统一由 `para.cc`/CLI 控制，replay JSON 中的旧
带宽值不会成为第二个运行配置源。因此 README 不冻结一个依赖旧带宽语义的
FqCoDel 丢包数。

## 目标输出与边界

- `network-flow-metrics.csv`：所有 IPv4 FlowMonitor 流的聚合结果；
- `network-flow-details.csv`：五元组、transfer ID、应用 payload 与逐流 IP 指标；
- `ecmp-route-events.csv`：每个 epoch、外部卫星 ID 和五元组的首次选择；
  `hash_value` 在旧模式中是 five-tuple hash，在 HRW 模式中是获胜候选分数；
- `size-aware-reservation-events.csv`：在 `global-size-aware-hrw` 和
  `global-capacity-aware-hrw` 中写出 assignment、sticky reuse、候选或整路径
  失效释放、sender-finish/receiver-complete 释放，以及物理下一跳和全局声明字节
  预留；
- `size-aware-summary.json`：在上述两种模式中汇总登记/活动 flow、结束时
  assignment、最终/峰值总预留及不同释放原因；
- `capacity-aware-summary.json`：只在 `global-capacity-aware-hrw` 中汇总
  结束时活动完整路径数、仍有预留的有向链路数、总预留速率及等待准入数；
  完整结束场景的四项均应为零；
- `transfer-summary.csv`：每条逻辑 transfer 的声明大小、分包、收发和完成时间；
- `task-events.csv`：每个完整任务恰好五条状态转换；
- `task-summary.csv`：每个任务的输入、排队、计算、结果和端到端时间；
- `compute-node-summary.csv`：计算节点的完成数、忙时、最大队列和利用率；
- `run-summary.json`：本次运行及网络、传输、任务聚合结果，包含任务完成数、
  完成率、`COMPLETE/PARTIAL` 运行状态、完成策略、完成任务的平均/最大
  端到端时间、接收缓冲区配置及可用时的 UDP socket Drop 聚合；诊断关闭时
  UDP Drop 聚合为 `null`，不会误报为零；同时汇总 FlowMonitor 显式
  DropReason 与未归因 loss。

失败输出统一位于 `<outputDir>/diagnostics/failure/`：

- `flow-drop-reasons.csv`：按五元组和 IPv4 DropReason 输出显式丢弃，并
  单列未归因/超时 loss；
- `incomplete-tasks.csv`、`incomplete-transfers.csv`：失败任务运行中的全部
  未完成对象及 partial 收发状态；
- `isl-queue-drops.csv`、`isl-queue-drop-summary.csv`：按有向 ISL 输出
  队列记录的逐次 Drop 与聚合；
- `udp-socket-drops.csv`、`udp-socket-drop-summary.csv`：按 UDP 接收
  socket 记录的逐次缓冲区 Drop 与接收端聚合；
- `flow-link-concentration.csv`、`diagnostic-summary.json`：计划业务量、
  ECMP 链路集中度、ISL/UDP socket 丢包和完成状态摘要。

任务与计算 CSV 只在任务模式生成；完整失败目录仅在
`diagnosticMode=failure` 且任务未全部完成时生成。显式启用诊断的
NetworkTransfer 模式保留例外，只在该目录生成 `flow-drop-reasons.csv`。
复用同一个 `outputDir` 时，程序先清理根目录旧路径及 failure 目录中的
九个已知诊断文件；不会递归删除未知用户文件，并且只在目录为空时移除
`failure/` 和 `diagnostics/`。非 size-aware 运行也会清理两个旧的
size-aware 文件，避免把历史结果误认为本次结果。
未匹配 NetworkTransfer 的 FlowMonitor 行使用 `transfer_id=0`。当前
任务调度仅支持单服务台、非抢占 FCFS；尚未实现可靠重传、故障、
checkpoint、备份或恢复语义。
