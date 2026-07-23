# link-test 运行说明（JsonTopo 版本）

本文件说明 `link-test` 的构建、运行参数、仿真流程和输出。JsonTopo 文件格式、
命名规则、字段单位和甲方交付示例统一维护在 `input/topology/json/README.md`，避免多处重复。

## 1. 入口文件

- 主程序：`examples/link-selection/link-test.cc`
- 拓扑构建：`examples/link-selection/topo.cc`
- JsonTopo 模块：`examples/link-selection/jsontopo/`
- 全局参数：`examples/link-selection/para.cc`
- waf 目标：`link-test`

## 2. 构建与运行

以下命令默认在仓库根目录执行。每次打开新终端后，先激活 Python 环境：

```bash
source .venv/bin/activate
```

然后构建并运行。甲方当前的 `link_output` 时间序列可用以下命令验证：

```bash
./waf build
./waf --run "link-test --routingMode=0 --offeredload=0 --linkOutputDir=examples/link-selection/input/topology/json/examples/link_output --simulationDuration=300 --outputDir=/tmp/link-output-smoke"
```

该命令自动把目录中最早的 `2024-01-02_00-00-00.json` 映射为仿真 `0s`，
并读取其后 300 秒内的全部快照。

仓库内置的传统 73 星 6 地面站 JsonTopo 示例也可用于兼容性自检：

```bash
./waf --run "link-test --routingMode=0 --offeredload=0 --nodesJson=examples/link-selection/input/topology/json/examples/customer-73sat-6gs/nodes_0s.json --topologyJson=examples/link-selection/input/topology/json/examples/customer-73sat-6gs/topology_0s.json --trafficMatrix=examples/link-selection/input/traffic/traffic_matrix(73).csv"
```

如果 `build/` 目录不存在，或修改了 waf / wscript / 模块依赖，先重新配置：

```bash
./waf configure --enable-examples --enable-tests
./waf build
```

如果没有激活环境，或直接运行 `./waf` 出现 Python 环境相关错误，可以临时使用：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf --run "link-test --offeredload=0 --nodesJson=examples/link-selection/input/topology/json/examples/customer-73sat-6gs/nodes_0s.json --topologyJson=examples/link-selection/input/topology/json/examples/customer-73sat-6gs/topology_0s.json --trafficMatrix=examples/link-selection/input/traffic/traffic_matrix(73).csv"
```

如果直接运行 `./waf --run link-test`，需要先把 `nodes_0s.json` 和
`topology_0s.json` 放到 `input/topology/json/`。缺少任一初始化文件时，程序会立即报错退出，
不会继续进入仿真。

默认参数位于 `para.cc`：

```text
_useJsonTopo = true
_jsonTopoPatchMode = false
offeredload = 0.0001
_tranProc = 0  // UDP
linkBandwidth = 10000000000  // 10Gbps
totalTimeStep = 110
```

正常输出应包含：

```text
[RUN] 实验参数
[TOPO:Init] 开始拓扑初始化
[TOPO:Nodes] 节点创建完成
[TOPO:Clusters] 初始簇信息
[TOPO:Links] 初始链路安装完成
[TOPO:Plan] JsonTopo 时间片计划 / link_output 时间窗口
[TOPO:HoldTime] ...
[TRAFFIC] 读取流量矩阵 / offeredload=0，跳过流量矩阵和客户端创建
[RUN] Simulation wall-clock cost
[METRICS] 业务网络流
[METRICS] 结构化结果
```

## 3. 命令行参数

- `--offeredload=<double>`：业务负载，快速验证可使用默认值 `0.0001`。
- `--routingMode=<0|1>`：`0=OSPF`，`1=簇内/簇间路由`。
- `--linkBandwidth=<bps>`：链路带宽；当 JSON 链路未写带宽时作为兜底值。
- `--tranProtocol=<0|1>`：`0=UDP`，`1=TCP`。
- `--trafficMatrix=<path>`：业务流量矩阵 CSV 文件，默认读取 `input/traffic/traffic_matrix(324).csv`。
- `--outputDir=<path>`：指标输出目录，默认 `examples/link-selection/output`。
- `--writeRoutingTables=<true|false>`：是否输出调试用路由表文件，默认 `false`。
- `--useJsonTopo=<true|false>`：是否使用 JsonTopo，默认 `true`。
- `--jsonTopoPatchMode=<true|false>`：后续时间片格式；`false=全量快照`，`true=增量 patch`。
- `--nodesJson=<path>`：初始节点 JSON 文件。
- `--topologyJson=<path>`：初始链路 JSON 文件。
- `--timeSlicesJson=<path>`：可选索引文件；常规情况下不需要，默认按文件名扫描 `input/topology/json/`。
- `--linkOutputDir=<path>`：甲方绝对时间快照目录；设置后启用 `link_output` 模式。
- `--simulationDuration=<seconds>`：仿真时长。`link_output` 模式下必须为正数；
  其他模式下提供正数时可覆盖默认的 `110s`。
- `--isSate=<1|2|3|4>`：传统拓扑模式使用；JsonTopo 模式下不决定节点数量。
- `--consType=<0|1>`：传统拓扑模式使用；`0=Walker Star`，`1=Walker Delta`。

示例：运行最小增量 patch 模式：

```bash
./waf --run "link-test --offeredload=0 --jsonTopoPatchMode=true --nodesJson=examples/link-selection/input/topology/json/examples/patch/nodes_0s.json --topologyJson=examples/link-selection/input/topology/json/examples/patch/topology_0s.json"
```

若要自动加载 patch 时间片，请将 `patch_<time>s.json` 放到 `input/topology/json/`；`input/topology/json/examples/` 下的文件只作为格式示例。

示例：指定初始 JsonTopo 文件：

```bash
./waf --run \
  "link-test --nodesJson=examples/link-selection/input/topology/json/nodes_0s.json --topologyJson=examples/link-selection/input/topology/json/topology_0s.json"
```

## 4. JSON 拓扑数据入口

### 4.1 甲方 link_output 时间序列

`link_output` 模式通过命令行显式指定目录，不要求把数据复制到默认目录。程序严格扫描
`YYYY-MM-DD_HH-MM-SS.json`，自动选择目录中时间最早的快照作为仿真 `0s`，并只选择
闭区间 `[最早快照, 最早快照 + simulationDuration]` 内的文件。

最早快照中的 `sat_id` 用于创建任意规模的卫星节点；`feeder` 的非卫星端点自动推导为
地面站。后续文件作为完整快照处理，缺失链路会被断开。目录即使包含一天约 1440 个文件，
内存中也只保留时间戳和路径，JSON 内容到对应仿真时间才读取。

新格式的 `delay` 单位是毫秒，`hold_time` 单位是秒。`clusterId=0` 表示卫星
未分簇，`clusterId=n (n≥1)` 映射到内部簇 `n-1`；地面站按 ID 数值升序依次
作为簇 0、簇 1……的簇首。
完整格式见 `input/topology/json/examples/link_output/README.md`。

### 4.2 传统 JsonTopo

默认数据目录：

```text
examples/link-selection/input/topology/json/
```

该目录中的 JSON 会被仿真读取；仓库不再提交默认读取的测试 JSON。`input/topology/json/examples/` 只放说明示例，不参与默认扫描。

常规交付只需要遵守文件命名规则，程序会按时间自动加载：

```text
nodes_0s.json + topology_0s.json            # 必需初始拓扑
nodes_<time>s.json + topology_<time>s.json  # 全量快照模式
patch_<time>s.json                          # 增量 patch 模式
```

建议同一个 `input/topology/json/` 目录一次只放一种运行方案：要么放全量快照文件，要么放 patch 文件。切换方案前先清理另一类后续时间片文件。

全量快照模式下，后续时间片的 `nodes_<time>s.json` 和 `topology_<time>s.json`
可以只提供发生变化的一类；缺少的节点或链路部分会保持上一状态。

详细规范见：

```text
examples/link-selection/input/topology/json/README.md
```

## 5. 流量输入

热点流量模式下（`_trafficMode=0`），程序默认读取：

```text
examples/link-selection/input/traffic/traffic_matrix(324).csv
```

客户尺度 JsonTopo 示例包含 73 颗卫星和 6 个地面站。运行该示例时应显式指定
`--trafficMatrix=examples/link-selection/input/traffic/traffic_matrix(73).csv`。
流量数据目录说明见：

```text
examples/link-selection/input/traffic/README.md
```

JsonTopo 默认全局路由路径会为每个节点分配 `172.16.0.0/12` 范围内的独立 `/32`
服务地址，地址按外部 `node_id` 排序后稳定映射。业务应用统一监听本地端口 `9`；
链路 `/30` 地址只用于逐跳传输，不再作为节点业务身份。

当 `--offeredload=0` 时，程序仍安装服务器以保留指标结构，但不会读取流量矩阵、
分配客户端业务矩阵或创建客户端；此时业务网络流指标为 0 是预期结果。

## 6. 主流程

1. 解析命令行参数并打印关键配置。
2. 调用 `initTopo()` 创建节点、安装协议栈、创建链路并分配地址。
3. JsonTopo 模式下加载初始 JSON 拓扑，并按时间片更新节点和链路状态。
4. 调用 `buildApp()` 安装服务器与客户端应用。
5. 安装 FlowMonitor，运行仿真到 `totalTimeStep`。
6. 在 `Simulator::Destroy()` 前调用 `MetricsRecorder` 汇总并写出指标。

## 7. 统计输出

终端会分别输出业务网络流和控制网络流统计。网络流吞吐量统一使用 Mbps，
测量区间来自实际 flow 首发和末次活动时间。

每次运行还会在 `--outputDir` 下生成：

- `network-flow-metrics.csv`：包数、字节数、真实测量区间、时延、抖动、吞吐量和丢包率。
- `task-metrics.json`：仿真时长、墙钟时间、传输协议及 UDP/TCP 应用层接收量。

## 8. 传统 CSV 拓扑模式

如果通过 `--useJsonTopo=false` 切回传统拓扑，程序会使用：

```text
examples/link-selection/input/topology/csv/topo(324).csv
```

该模式主要保留兼容旧实验流程；当前新增拓扑数据优先使用 JsonTopo。
