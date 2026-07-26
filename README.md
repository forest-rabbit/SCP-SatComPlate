# SatCompute

SatCompute 是基于 ns-3.33 的纯星上动态网络仿真项目，只创建卫星节点和星间链路
（ISL）。拓扑由分离式 JSON 全量快照驱动；最短路、路由表生成和动态重算仍由
ns-3 原生 `GlobalRouteManager` 完成。

本仓库的 `main` 由原 `xw` 仓库的 `customer` 分支迁移而来。迁移后已移除
cluster、地面站、星地链路、CSV 建图、簇内/簇间路由和 SDN/OpenFlow 实验。

## 目录

```text
examples/satcompute/
├── satcompute.cc          # 主程序和 CLI
├── para.cc                # 当前有效默认参数
├── topo.cc                # 卫星、ISL 和动态快照
├── jsontopo/              # JSON 拓扑解析与链路状态
├── routing/               # 原生全局路由之上的确定性逐流 ECMP 选择
├── traffic/               # legacy 背景流量与 NetworkTransfer
├── metrics/               # 聚合、逐流和 ECMP 路由证据
├── tools/                 # 最小确定性检查器
└── input/
    ├── topology/json/examples/xw-66sat/
    ├── topology/json/tests/diamond-4-*/
    └── traffic/
        ├── csv/          # 临时保留的 legacy 业务矩阵
        └── json/         # NetworkTransfer 输入
```

## 构建与基本运行

```bash
source .venv/bin/activate
./waf configure --enable-examples --enable-tests
./waf build
./waf --run satcompute
```

默认运行 xw 66 星的 0–110 秒快照，不注入业务。legacy CSV 背景流量仍可通过
`offeredLoad` 启用：

```bash
./waf --run "satcompute --offeredLoad=0.0001 --routingMode=global-first"
```

`para.cc` 保存默认值；命令行只覆盖当前运行。查看全部参数：

```bash
./waf --run "satcompute --PrintHelp"
```

主要参数：

```text
--topologyDir=<dir>                    JSON 全量快照目录
--simulationDuration=<s>               仿真时长
--offeredLoad=<double>                 legacy 业务矩阵倍率
--transport=<udp|tcp>                  legacy 传输协议
--trafficMatrix=<file>                 legacy 100×N 行、N 列业务输入
--transferTrace=<file>                 NetworkTransfer JSON；默认关闭
--transferPayloadBytes=<uint32>         每个 UDP 包的应用 payload 上限
--islMtuBytes=<uint16>                  所有 ISL 的 MTU
--transferLogMode=<summary|verbose|silent>
--routingMode=<global-first|global-hash-per-flow>
--ecmpHashSeed=<uint64>                 FNV-1a-64 seed 前缀
--outputDir=<dir>                       指标输出目录
```

`transferTrace` 与正的 `offeredLoad` 互斥；当前 NetworkTransfer 仅支持 UDP。

## NetworkTransfer

NetworkTransfer JSON 的 `schema_version` 必须为 `0.1`。每条记录只含：

```json
{
  "transfer_id": 1,
  "source_node_id": 0,
  "destination_node_id": 3,
  "size_bytes": 4096,
  "arrival_time_ns": 100000000
}
```

`source_node_id` 和 `destination_node_id` 是外部卫星 ID。JSON 不保存包数、
包长、发包间隔、MTU、速率或 UDP 端口。程序根据 payload cap 自动分包，
最后一包使用精确余量。每包发送后重新查询该五元组当前选中的首跳
PointToPoint 设备，并按该包的链路序列化时间调度下一包，不再使用应用层人工
发送速率。目的端口固定为 9000，并按每个源卫星的 `transfer_id` 顺序从
10000 派生唯一源端口。

已提交的 `workload-5000-varied.json` 包含 5000 个不同的 `size_bytes`，在
4096-byte cap 下覆盖 1–20 包。`varied-multipacket.json` 用于验证不同大小的
5、10、15、20 包传输；不再把“大流量”固定解释为 1 Gbit 或 1 GiB。

## 路由

- `global-first`：默认模式，完整委托原生 `Ipv4GlobalRouting` 的首条路由行为；
- `global-hash-per-flow`：只对公开路由表中的目标 service `/32` exact host
  candidates 做稳定排序、去重和五元组 FNV-1a-64 选择。

自定义层不复制 SPF、Dijkstra、LSDB 或 `LookupGlobal()`，也不启用原生随机
ECMP。每次完整快照调用原生 `RecomputeRoutingTables()` 后进入新的 route
epoch。当前 ECMP 验证只覆盖能够直接读取 UDP header 的未分片 IPv4 包。

## Diamond 验证

```bash
./waf --run "satcompute \
  --topologyDir=examples/satcompute/input/topology/json/tests/diamond-4-static \
  --simulationDuration=3 \
  --transferTrace=examples/satcompute/input/traffic/json/diamond-4-static-transfers.json \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-static-a"
```

完整的静态重复运行、动态 `2 → 1 → 2` route epoch 验证和检查器命令见
[`examples/satcompute/README.md`](examples/satcompute/README.md)。

## 输出与范围

`--outputDir` 下生成：

- `network-flow-metrics.csv`：所有 IPv4 流的聚合指标；
- `network-flow-details.csv`：FlowMonitor 五元组与 NetworkTransfer payload 的逐流指标；
- `ecmp-route-events.csv`：每个 `(route_epoch,node_id,five_tuple)` 的首次选路证据；
- `transfer-summary.csv`：声明大小、分包、发送、接收和完成时间；
- `run-summary.json`：运行配置及应用层、FlowMonitor 聚合结果。

拓扑协议见
[`examples/satcompute/input/topology/json/README.md`](examples/satcompute/input/topology/json/README.md)。
当前阶段只完成网络传输和确定性 ECMP；任务计算、服务时间、调度、故障、备份和
恢复语义尚未实现。64000-byte payload 仅是降低大数据仿真事件数量的可扩展性
配置，不表示真实卫星网络使用 64 KB 物理帧。
