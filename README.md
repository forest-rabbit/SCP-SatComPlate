# SatCompute

SatCompute 是基于 ns-3.33 的纯星上动态网络仿真项目。项目只创建卫星节点和星间链路
（ISL），拓扑由 JSON 全量快照驱动，路由使用 ns-3 自带的
`Ipv4GlobalRouting`。

本仓库的 `main` 由原 `xw` 仓库的 `customer` 分支迁移而来；原仓库保持
不变。迁移后已移除 cluster、地面站、星地链路、CSV 建图、簇内/簇间路由、
SDN/OpenFlow 实验和相关兼容参数。

## 目录

```text
examples/satcompute/
├── satcompute.cc          # 主程序和命令行参数
├── para.cc                # 当前有效参数的默认值
├── topo.cc                # 卫星节点、ISL 和动态更新
├── jsontopo/              # JSON 快照解析与链路状态
├── metrics/               # FlowMonitor 和应用层指标
└── input/
    ├── topology/json/examples/xw-66sat/  # xw 66 星静态拓扑快照
    └── traffic/                          # 由原 324 星 CSV 派生的 66 星业务输入
```

## 构建

```bash
source .venv/bin/activate
./waf configure --enable-examples --enable-tests
./waf build
```

## 运行

默认运行 110 秒，读取 0–110 秒、间隔 10 秒且内容不变的 xw 66 星快照，
不注入业务流量：

```bash
./waf --run satcompute
```

启用样例业务：

```bash
./waf --run "satcompute --offeredLoad=0.001"
```

`para.cc` 只保存当前有效参数的默认值。建议用命令行为每次实验覆盖参数；
若要永久修改默认负载强度，可调整其中的 `config.offeredLoad`。例如
`0.001` 表示使用业务输入累计值的 0.1%，`0` 表示不创建业务流。

查看所有可调参数：

```bash
./waf --run "satcompute --PrintHelp"
```

快速拓扑与路由自检：

```bash
./waf --run "satcompute --simulationDuration=110 --offeredLoad=0 --outputDir=/tmp/satcompute-smoke"
```

主要参数：

```text
--topologyDir=<dir>           JSON 快照目录
--simulationDuration=<s>      仿真时长
--offeredLoad=<double>        业务矩阵倍率；0 表示不创建客户端流
--transport=<udp|tcp>         业务传输协议
--trafficMatrix=<file>        100×N 行、N 列的业务输入
--outputDir=<dir>             指标输出目录
```

拓扑格式见
[`examples/satcompute/input/topology/json/README.md`](examples/satcompute/input/topology/json/README.md)，
完整运行说明见
[`examples/satcompute/README.md`](examples/satcompute/README.md)。

## 路由与动态链路

初始拓扑调用 `Ipv4GlobalRoutingHelper::PopulateRoutingTables()`。每个后续完整快照
应用完毕后，统一调用 `Ipv4GlobalRoutingHelper::RecomputeRoutingTables()`。
项目没有自定义选路、簇内路由或簇间路由。

JSON 中的链路消失和恢复直接使用 ns-3 的 `Ipv4::SetDown/SetUp`；最短路计算
和路由表维护均由原生全局路由完成。

## 输出

`--outputDir` 下生成：

- `network-flow-metrics.csv`：所有 IPv4 流的包、字节、时延、抖动、吞吐和丢包；
- `task-metrics.json`：仿真时长、墙钟耗时、协议和应用层接收字节。
