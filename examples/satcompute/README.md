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
./waf configure --enable-examples --enable-tests
./waf build
./waf --run satcompute
```

```text
topologyDir              = examples/satcompute/input/topology/json/examples/xw-66sat
simulationDuration       = 110
offeredLoad              = 0
transport                = udp
trafficMatrix            = examples/satcompute/input/traffic/traffic_matrix(66).csv
transferTrace            = empty
transferPacketIntervalNs = 10000000
routingMode              = global-first
ecmpHashSeed             = 1
outputDir                = examples/satcompute/output
```

## 参数合同

- `--topologyDir`：卫星 JSON 全量快照目录。
- `--simulationDuration`：有限正秒数。
- `--offeredLoad`：legacy 业务矩阵倍率；为 0 时不读取矩阵。
- `--transport`：legacy 模式支持 `udp` 或 `tcp`。
- `--trafficMatrix`：legacy 100×N 行、N 列 CSV，单位 Gbps。
- `--transferTrace`：可选 NetworkTransfer JSON。
- `--transferPacketIntervalNs`：所有 transfer 共用的正纳秒间隔。
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

程序按 `transfer_id` canonical sort。应用 payload 每包最多 1024 bytes，
最后一包使用准确余量。第一包在 `arrival_time_ns` 发送，后续包使用全局
`transferPacketIntervalNs`；最后一次计划发送必须早于 simulation stop。
目的 UDP 端口固定为 9000，同一源卫星的 transfer 按
`(source_node_id,transfer_id)` 从源端口 10000 顺序派生。JSON 不接受包数、
包间隔、包长或端口字段。

## 地址与路由

卫星按外部 `sat_id` 升序映射到 ns-3 节点。service 地址来自
`172.16.0.0/12` 的 `/32`，ISL 来自 `10.0.0.0/8` 的 `/30`。JSON 中
`links[]` 的排列不影响地址分配。

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
./waf --run "satcompute \
  --topologyDir=examples/satcompute/input/topology/json/tests/diamond-4-static \
  --simulationDuration=3 \
  --offeredLoad=0 \
  --transferTrace=examples/satcompute/input/transfers/diamond-4-static-transfers.json \
  --transferPacketIntervalNs=10000000 \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-static-a"

./waf --run "satcompute \
  --topologyDir=examples/satcompute/input/topology/json/tests/diamond-4-static \
  --simulationDuration=3 \
  --offeredLoad=0 \
  --transferTrace=examples/satcompute/input/transfers/diamond-4-static-transfers.json \
  --transferPacketIntervalNs=10000000 \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-static-b"
```

动态场景在 2 秒断开一条支路、4 秒恢复：

```bash
./waf --run "satcompute \
  --topologyDir=examples/satcompute/input/topology/json/tests/diamond-4-dynamic \
  --simulationDuration=6 \
  --offeredLoad=0 \
  --transferTrace=examples/satcompute/input/transfers/diamond-4-dynamic-transfers.json \
  --transferPacketIntervalNs=10000000 \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-dynamic"

python3 examples/satcompute/tools/check-ecmp-output.py \
  --first=/tmp/satcompute-ecmp-static-a \
  --second=/tmp/satcompute-ecmp-static-b \
  --dynamic=/tmp/satcompute-ecmp-dynamic
```

检查器验证静态双支路覆盖、重复输出一致、每条 transfer 的精确 payload，以及
动态 epoch 的 `2 → 1 → 2` candidates 和恢复后的确定性选择。

## 输出与当前边界

- `network-flow-metrics.csv`：所有 IPv4 FlowMonitor 流的聚合结果；
- `network-flow-details.csv`：五元组、transfer ID、应用 payload 与逐流 IP 指标；
- `ecmp-route-events.csv`：每个 epoch、外部卫星 ID 和五元组的首次选择；
- `task-metrics.json`：兼容保留的运行摘要及应用层接收字节。

未匹配 NetworkTransfer 的 legacy FlowMonitor 行使用 `transfer_id=0`。当前尚未
实现任务计算、服务时间、调度、故障、checkpoint、备份或恢复语义。
