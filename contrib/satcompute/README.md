# SatCompute 运行说明

## 执行模型

1. 扫描配对的 `nodes_<time>s.json` 与 `topology_<time>s.json` 全量快照；
2. 按外部卫星 ID 的稳定顺序创建节点和 `/32` service 地址；
3. 按无向端点 ID 的 canonical 顺序创建 ISL 和 `/30` 网段；
4. 由原生 `GlobalRouteManager` 填充路由表；
5. 可选安装 NetworkTransfer JSON 或分离输入的任务闭环；无输入时只运行拓扑；
6. 后续快照统一更新链路，再调用原生 `RecomputeRoutingTables()` 并推进 epoch；
7. 输出网络、传输、任务和计算节点的结构化证据。

程序不会创建地面站，不解析 cluster，也不支持 CSV 拓扑。当前默认值集中在
`para.cc`。

## 源码布局

`topology/satellite-topology.cc` 负责创建卫星、编排初始和运行期快照，并在
完整快照应用后调用 ns-3 全局路由重算。`topology/snapshot/` 分别保存快照数据
类型、单对 JSON 文件读取器和快照目录调度器；`topology/link/` 保存运行期 ISL
设备、带宽、时延、MTU、队列、启停状态和设备队列丢包事件。

任务、流量和拓扑 JSON 共用
`third-party/nlohmann/json.hpp` 中未经修改的 nlohmann JSON 3.11.3 单头文件
（MIT）。它是运行时依赖，不属于 `tools/`；`tools/` 只保存外部 CI、输入生成器、
preflight 和输出检查器。

## 构建与默认值

```bash
source .venv/bin/activate
./waf configure --disable-examples --disable-tests --enable-modules=satcompute
./waf build
./waf --run-no-build satcompute
```

`tests/fixtures/topology/snapshots/`、`tests/fixtures/topology/compute-profiles/`、
`tests/fixtures/traffic/transfers/`、`tests/fixtures/traffic/tasks/` 和 `tools/`
中的检查器是外部端到端验证资产，不进入 `ns3-satcompute` 模块编译。需要运行
ns-3 上游单元测试时再显式启用 `--enable-tests`；日常平台构建不启用
examples 或 tests。

```text
topologyDir              = contrib/satcompute/input/topology/examples/xw-66sat
simulationDuration       = 110
transferTrace            = empty
computeProfile           = empty
taskTrace                = empty
transferChunkMode        = fixed
transferPayloadBytes     = 1024
islMtuBytes              = 1500
islQueueBytes            = 1500000
receiverRcvBufBytes      = 131072
transferLogMode          = summary
taskLogMode              = summary
taskCompletionPolicy     = strict
diagnosticMode           = off
routingMode              = global-hash-per-flow
ecmpHashSeed             = 1
outputDir                = /tmp/satcompute-output
```

## CI 分级

Pull request 只运行 `SatCompute Fast Smoke`，覆盖核心路由、任务和失败诊断
合同。`main` push 与手动触发运行 `SatCompute Full Regression`；Full 先执行
全部 Fast 脚本，再补充规模、顺序、generator 和 preflight 边界。

完成上述 configure/build 后，可在本地直接运行 Fast：

```bash
contrib/satcompute/tools/ci/run-routing-smoke.sh
contrib/satcompute/tools/ci/run-task-smoke.sh
contrib/satcompute/tools/ci/run-diagnostics-smoke.sh
```

完整回归必须在同一工作区按顺序继续运行：

```bash
contrib/satcompute/tools/ci/run-full-routing-regression.sh
contrib/satcompute/tools/ci/run-full-workload-regression.sh
```

前三级脚本保留 topology-only、Hash/HRW/size-aware、单任务、FCFS、
strict/report、FqCoDel、设备队列和 UDP socket 合同。后两级保留 canonical
ordering、5000-transfer、mixed-large、TaskTrace/ComputeProfile 换序、
generator seed/tail、preflight 成功/失败/warning 及全部扩展检查器。测试仅按
频率分级，没有从回归集合中删除。

## 参数合同

- `--topologyDir`：卫星 JSON 全量快照目录。
- `--simulationDuration`：有限正秒数。
- `--transferTrace`：可选 NetworkTransfer JSON。
- `--computeProfile`：`topology/resources` 下的静态计算能力 JSON。
- `--taskTrace`：`traffic/task` 下的任务到达 JSON。
- `--transferChunkMode`：`fixed` 或 `size-aware`，默认 `fixed`。
- `--transferPayloadBytes`：`fixed` 模式的 UDP payload 上限，默认 1024。
- `--islMtuBytes`：所有当前及后续 ISL 的 MTU，默认 1500。
- `--islQueueBytes`：所有当前及后续 ISL DropTail 队列的字节容量，默认
  1500000；容量不随 payload 大小变化。
- `--receiverRcvBufBytes`：每个 NetworkTransfer UDP 接收 socket 的缓冲区
  字节数，默认 131072，必须大于 0。
- `--transferLogMode`：`summary`、`verbose` 或 `silent`。
- `--taskLogMode`：`summary`、`verbose` 或 `silent`，只影响任务输入日志。
- `--taskCompletionPolicy`：`strict` 在任务未全部完成时写出指标后返回非零；
  `report` 写出相同结果后正常退出。默认 `strict`。
- `--diagnosticMode`：`off` 只保留基础指标；`failure` 在任务失败时额外
  采集并写出未完成对象、ISL 队列 Drop 和 ECMP 链路集中度。默认 `off`。
- `--routingMode`：`global-first`、`global-hash-per-flow`、
  `global-hrw-per-flow` 或 `global-size-aware-hrw`，默认保留 N1 基线
  `global-hash-per-flow`。
- `--ecmpHashSeed`：三种逐流 ECMP 使用的确定性 FNV-1a-64 64-bit seed
  前缀。
- `--outputDir`：结构化指标目录，默认 `/tmp/satcompute-output`。正式实验应
  显式填写仓库外的持久绝对路径。

`computeProfile` 与 `taskTrace` 必须同时指定，任务模式不能同时指定
`transferTrace`。NetworkTransfer 与任务模式都使用 UDP；三项输入均为空时
运行 `topology-only`，不安装 PacketSink、NetworkTransfer 或
TaskCoordinator。

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
traffic/task/...        TaskTrace：任务、数据量、计算量与到达时间
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
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
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

`global-first` 完整使用原生 `Ipv4GlobalRouting` 首条路由。三种逐流模式都只
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

没有 exact host route 或不能解析合法五元组时回退原生行为。项目不复制
GlobalRouteManager、SPF 或私有 `LookupGlobal()`，也不使用随机逐包 ECMP。
当前验证范围是未发生 IPv4 分片的 UDP NetworkTransfer。

## Diamond 验证

静态场景重复运行：

```bash
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --simulationDuration=3 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/diamond-4-static-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-static-a"

./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
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
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic \
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

HRW 动态 fixture 在 `1s` 保持候选集合不变但打乱完整快照顺序，`3s` 删除
一条支路，`5s` 恢复。四条 flow 跨越全部 epoch：

```bash
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-hrw-dynamic \
  --simulationDuration=7 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/diamond-4-hrw-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=64000 \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-hrw-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-hrw-seed1-a"
```

CI 对 seed 1 和 2 各重复两次，并由
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
17 个输入大于 64 MiB；预估 243,028 个 UDP 包，不进入每次 CI。可在上述命令
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
./waf --run-no-build "satcompute \
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
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
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

`mixed-large-local.json` 是不放入 CI 的完整压力输入，含 10 条不同大流量，
范围为 128 MiB–1 GiB，并明确包含 256 MiB、512 MiB 和 1 GiB：

```bash
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
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
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
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

# 上一条命令的预期退出码为 1。
python3 contrib/satcompute/tools/validation/check-task-output.py failure \
  --topology-dir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --compute-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --task-trace=contrib/satcompute/tests/fixtures/traffic/tasks/task-single-ecmp.json \
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

四节点 fixture 使用两个 1 Gbit/s 入口汇入一个 10 Mbit/s 出口，确定性
验证默认 FqCoDel QueueDisc：

```bash
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/fqcodel-bottleneck \
  --simulationDuration=3 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/fqcodel-bottleneck-transfers.json \
  --diagnosticMode=failure \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1400 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-fqcodel"

python3 contrib/satcompute/tools/validation/check-flow-drop-reasons.py \
  --output-dir=/tmp/satcompute-fqcodel \
  --require-reason=QUEUE_DISC \
  --forbid-reason=QUEUE \
  --require-zero-unattributed \
  --expected-explicit-drop-packets=6262
```

检查器交叉验证 DropReason CSV、逐流详情和 `run-summary.json`。完整 75%
诊断及局部 replay 结果记录在
`docs/reviews/n1-6-stress-validation-review.md`，大型输入与输出不提交仓库。

## 输出与当前边界

- `network-flow-metrics.csv`：所有 IPv4 FlowMonitor 流的聚合结果；
- `network-flow-details.csv`：五元组、transfer ID、应用 payload 与逐流 IP 指标；
- `ecmp-route-events.csv`：每个 epoch、外部卫星 ID 和五元组的首次选择；
  `hash_value` 在旧模式中是 five-tuple hash，在 HRW 模式中是获胜候选分数；
- `size-aware-reservation-events.csv`：仅在 `global-size-aware-hrw` 中写出
  assignment、sticky reuse、候选失效释放和 sender-finish 释放，以及物理
  下一跳和全局预留的前后值；
- `size-aware-summary.json`：仅在 `global-size-aware-hrw` 中汇总登记/活动
  flow、结束时 assignment、最终/峰值总预留及峰值物理下一跳预留；
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
