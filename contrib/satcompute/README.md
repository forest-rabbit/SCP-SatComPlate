# SatCompute 运行说明

## 执行模型

1. 扫描配对的 `nodes_<time>s.json` 与 `topology_<time>s.json` 全量快照；
2. 按外部卫星 ID 的稳定顺序创建节点和 `/32` service 地址；
3. 按无向端点 ID 的 canonical 顺序创建 ISL 和 `/30` 网段；
4. 由原生 `GlobalRouteManager` 填充路由表；
5. 可选安装 legacy CSV 背景流量，或安装互斥的 NetworkTransfer JSON；
6. 后续快照统一更新链路，再调用原生 `RecomputeRoutingTables()` 并推进 epoch；
7. 输出聚合指标、逐流指标和首次 ECMP 选择证据。

程序不会创建地面站，不解析 cluster，也不支持 CSV 拓扑。当前默认值集中在
`para.cc`。

## 构建与默认值

```bash
source .venv/bin/activate
./waf configure --disable-examples --disable-tests --enable-modules=satcompute
./waf build
./waf --run-no-build satcompute
```

`input/topology/json/tests/`、`input/traffic/json/test/` 和
`tools/check-ecmp-output.py` 是外部端到端验证资产，不进入
`ns3-satcompute` 模块编译。需要运行 ns-3 上游单元测试时再显式启用
`--enable-tests`；日常平台构建不启用 examples 或 tests。

```text
topologyDir              = contrib/satcompute/input/topology/json/examples/xw-66sat
simulationDuration       = 110
offeredLoad              = 0
transport                = udp
trafficMatrix            = contrib/satcompute/input/traffic/csv/traffic_matrix(66).csv
transferTrace            = empty
transferChunkMode        = fixed
transferPayloadBytes     = 1024
islMtuBytes              = 1500
islQueueBytes            = 1500000
transferLogMode          = summary
routingMode              = global-first
ecmpHashSeed             = 1
outputDir                = contrib/satcompute/output
```

## 参数合同

- `--topologyDir`：卫星 JSON 全量快照目录。
- `--simulationDuration`：有限正秒数。
- `--offeredLoad`：legacy 业务矩阵倍率；为 0 时不读取矩阵。
- `--transport`：legacy 模式支持 `udp` 或 `tcp`。
- `--trafficMatrix`：legacy 100×N 行、N 列 CSV，单位 Gbps。
- `--transferTrace`：可选 NetworkTransfer JSON。
- `--transferChunkMode`：`fixed` 或 `size-aware`，默认 `fixed`。
- `--transferPayloadBytes`：`fixed` 模式的 UDP payload 上限，默认 1024。
- `--islMtuBytes`：所有当前及后续 ISL 的 MTU，默认 1500。
- `--islQueueBytes`：所有当前及后续 ISL DropTail 队列的字节容量，默认
  1500000；容量不随 payload 大小变化。
- `--transferLogMode`：`summary`、`verbose` 或 `silent`。
- `--routingMode`：`global-first` 或 `global-hash-per-flow`。
- `--ecmpHashSeed`：确定性 FNV-1a-64 输入的 64-bit seed 前缀。
- `--outputDir`：结构化指标目录。

指定 `transferTrace` 时必须保持 `offeredLoad=0`，且只支持 UDP；此模式不会读取
`trafficMatrix`。

## Legacy 背景流量

业务文件必须恰好有 100×N 行、N 列。当前 UDP 兼容公式为：

```text
scaled_value = matrix_value × offeredLoad
MaxPackets = max(1, floor(scaled_value × 2^30 / (1024 × 8 × 10000)))
Interval = 100 s / MaxPackets
```

默认 66 星输入下，`offeredLoad=0.0001` 计划 4356 个包，
`offeredLoad=0.001` 计划 17712 个包。TCP 仍使用连续 `OnOff`。

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

## 地址与路由

卫星按外部 `sat_id` 升序映射到 ns-3 节点。service 地址来自
`172.16.0.0/12` 的 `/32`，ISL 来自 `10.0.0.0/8` 的 `/30`。JSON 中
`links[]` 的排列以及 `node1_id/node2_id` 的端点方向都不影响地址分配。

`global-first` 完整使用原生 `Ipv4GlobalRouting` 首条路由。
`global-hash-per-flow` 只枚举公开可读的 exact service `/32` host routes，
按 gateway、output interface、destination 和 mask 排序去重，再对以下 21
bytes 做 FNV-1a-64：

```text
seed(8) + source IPv4(4) + destination IPv4(4) +
protocol(1) + source port(2) + destination port(2)
```

没有 exact host route 或不能解析合法五元组时回退原生行为。项目不复制
GlobalRouteManager、SPF 或私有 `LookupGlobal()`，也不使用随机逐包 ECMP。
当前验证范围是未发生 IPv4 分片的 UDP NetworkTransfer。

## Diamond 验证

静态场景重复运行：

```bash
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/json/tests/diamond-4-static \
  --simulationDuration=3 \
  --offeredLoad=0 \
  --transferTrace=contrib/satcompute/input/traffic/json/test/diamond-4-static-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-static-a"

./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/json/tests/diamond-4-static \
  --simulationDuration=3 \
  --offeredLoad=0 \
  --transferTrace=contrib/satcompute/input/traffic/json/test/diamond-4-static-transfers.json \
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
  --topologyDir=contrib/satcompute/input/topology/json/tests/diamond-4-dynamic \
  --simulationDuration=6 \
  --offeredLoad=0 \
  --transferTrace=contrib/satcompute/input/traffic/json/test/diamond-4-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-dynamic"

python3 contrib/satcompute/tools/check-ecmp-output.py \
  --first=/tmp/satcompute-ecmp-static-a \
  --second=/tmp/satcompute-ecmp-static-b \
  --dynamic=/tmp/satcompute-ecmp-dynamic
```

检查器验证静态双支路覆盖、重复输出一致、每条 transfer 的精确 payload，以及
动态 epoch 的 `2 → 1 → 2` candidates 和恢复后的确定性选择。

## 变长规模输入

`input/traffic/json/workload/workload-5000-varied.json` 由
`tools/generate-transfer-workload.py` 确定性生成。5000 条记录的
`size_bytes` 均不同，范围为 1024–81920 bytes；使用 4096-byte cap 时，每条
transfer 产生 1–20 个包，总计 53,100 个包和 207,357,501 应用字节。

```bash
./waf --run-no-build "satcompute \
  --simulationDuration=8 \
  --transferTrace=contrib/satcompute/input/traffic/json/workload/workload-5000-varied.json \
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
  --topologyDir=contrib/satcompute/input/topology/json/tests/diamond-4-static \
  --simulationDuration=45 \
  --offeredLoad=0 \
  --transferTrace=contrib/satcompute/input/traffic/json/test/mixed-large-ci.json \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=1500000 \
  --transferLogMode=summary \
  --routingMode=global-hash-per-flow \
  --outputDir=/tmp/satcompute-mixed-large-ci"

python3 contrib/satcompute/tools/check-ecmp-output.py \
  --large=/tmp/satcompute-mixed-large-ci \
  --large-input=contrib/satcompute/input/traffic/json/test/mixed-large-ci.json
```

`mixed-large-local.json` 是不放入 CI 的完整压力输入，含 10 条不同大流量，
范围为 128 MiB–1 GiB，并明确包含 256 MiB、512 MiB 和 1 GiB：

```bash
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/json/tests/diamond-4-static \
  --simulationDuration=340 \
  --offeredLoad=0 \
  --transferTrace=contrib/satcompute/input/traffic/json/workload/mixed-large-local.json \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=1500000 \
  --transferLogMode=summary \
  --routingMode=global-hash-per-flow \
  --outputDir=/tmp/satcompute-mixed-large-local"

python3 contrib/satcompute/tools/check-ecmp-output.py \
  --large-local=/tmp/satcompute-mixed-large-local \
  --large-local-input=contrib/satcompute/input/traffic/json/workload/mixed-large-local.json
```

64000-byte effective payload 只用于降低大数据仿真的事件数量，不宣称真实卫星
网络使用 64 KB 物理帧。

## 输出与当前边界

- `network-flow-metrics.csv`：所有 IPv4 FlowMonitor 流的聚合结果；
- `network-flow-details.csv`：五元组、transfer ID、应用 payload 与逐流 IP 指标；
- `ecmp-route-events.csv`：每个 epoch、外部卫星 ID 和五元组的首次选择；
- `transfer-summary.csv`：每条逻辑 transfer 的声明大小、分包、收发和完成时间；
- `run-summary.json`：本次运行及应用层、FlowMonitor 聚合结果。

未匹配 NetworkTransfer 的 legacy FlowMonitor 行使用 `transfer_id=0`。当前尚未
实现任务计算、服务时间、调度、故障、checkpoint、备份或恢复语义。
