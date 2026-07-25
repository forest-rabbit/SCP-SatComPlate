# SatCompute

SatCompute 是基于 ns-3.33 的动态卫星网络仿真项目。主程序位于
`examples/satcompute/`，通过 JsonTopo 动态拓扑运行 `satcompute`，并统计业务流性能。

## 仓库来源

本仓库的 `main` 由原 `xw` 仓库的 `customer/jsontopo` 分支迁移而来。导入时移除了
历史仿真输出和超过 GitHub 单文件限制的旧 324 星流量矩阵；原始 `xw` 仓库保持不变。
需要运行 324 星实验时，请通过 `--trafficMatrix=<path>` 使用外部矩阵文件。

## 文档分工

```text
README.md                                   # 项目入口：面向使用者的构建、运行和数据入口
examples/satcompute/README.md                # satcompute 运行参数、主流程和输出说明
examples/satcompute/input/topology/README.md       # 拓扑数据总入口，区分 json/csv
examples/satcompute/input/topology/json/README.md  # JsonTopo 文件构建、命名规则和交付规范
examples/satcompute/input/topology/json/examples/  # 全量快照和增量 patch 的示例
examples/satcompute/input/topology/csv/README.md   # 传统 CSV 拓扑输入说明
examples/satcompute/input/traffic/README.md        # 业务流量矩阵输入说明
docs/dev-setup.md                           # 开发环境、VS Code 和 clangd 说明
```

甲方通常只需要阅读本文件、`examples/satcompute/README.md`、`input/topology/` 和 `input/traffic/` 下的数据说明；
开发环境和仓库维护内容不放在本文件中。

## 主要目录

```text
examples/satcompute/
├── satcompute.cc              # 主仿真入口
├── topo.cc                   # 拓扑构建与动态更新主流程
├── para.cc                   # 默认实验参数
├── jsontopo/                 # JsonTopo 解析、状态维护和时间片调度
├── input/                    # 输入数据目录
│   ├── topology/             # 拓扑数据；json/ 放 JsonTopo，csv/ 放传统 CSV
│   └── traffic/              # 业务流量矩阵
└── README.md                 # satcompute 详细运行说明
```

旧的 SDN/OpenFlow/OSPF 实验仍保留在 `examples/sdn-controller/`，当前主线不依赖该目录。

## 构建

以下命令默认在仓库根目录执行。每次打开新终端后，先激活 Python 环境：

```bash
source .venv/bin/activate
```

首次构建：

```bash
./waf configure --enable-examples --enable-tests
./waf build
```

如果已经配置过，日常只需要：

```bash
./waf build
```

如果没有激活环境，或直接运行 `./waf` 出现 Python 环境相关错误，可以临时使用：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf build
```

## 运行

快速自检可以直接使用仓库内置的 73 星 6 地面站客户尺度示例。`--offeredload=0`
表示只检查拓扑加载、链路更新和 OSPF 路由收敛，不注入业务流量：

```bash
./waf --run "satcompute --offeredload=0 --nodesJson=examples/satcompute/input/topology/json/examples/customer-73sat-6gs/nodes_0s.json --topologyJson=examples/satcompute/input/topology/json/examples/customer-73sat-6gs/topology_0s.json --trafficMatrix=examples/satcompute/input/traffic/traffic_matrix(73).csv"
```

正常输出应包含：

```text
[RUN] 实验参数
[TOPO:Init] 开始拓扑初始化
[TOPO:Nodes] 节点创建完成
[TOPO:Clusters] 初始簇信息
[TOPO:Links] 初始链路安装完成
[TOPO:Plan] JsonTopo 时间片计划
[TOPO:HoldTime] ...
[TRAFFIC] 读取流量矩阵 / offeredload=0，跳过流量矩阵和客户端创建
[RUN] Simulation wall-clock cost
[METRICS] 业务网络流
[METRICS] 结构化结果
```

常用参数：

```text
--offeredload=<double>             业务负载
--linkBandwidth=<bps>              JSON 未写带宽时的兜底链路带宽
--tranProtocol=<0|1>               0=UDP，1=TCP
--trafficMatrix=<path>             业务流量矩阵文件
--writeRoutingTables=<bool>        是否输出调试用路由表，默认 false
--useJsonTopo=<true|false>         是否使用 JsonTopo
--jsonTopoPatchMode=<true|false>   false=全量快照，true=增量 patch
--nodesJson=<path>                 初始节点文件
--topologyJson=<path>              初始链路文件
```

最小示例：启用增量 patch 模式：

```bash
./waf --run "satcompute --offeredload=0 --jsonTopoPatchMode=true --nodesJson=examples/satcompute/input/topology/json/examples/patch/nodes_0s.json --topologyJson=examples/satcompute/input/topology/json/examples/patch/topology_0s.json"
```

若要自动加载 patch 时间片，请将 `patch_<time>s.json` 放到 `examples/satcompute/input/topology/json/` 目录。

如果已将甲方数据放入 `examples/satcompute/input/topology/json/`，也可以直接运行 `./waf --run satcompute`。如果缺少 `nodes_0s.json` 或 `topology_0s.json`，程序会立即报错退出，并提示应放置的文件或可使用的命令行参数。完整参数和仿真流程见 `examples/satcompute/README.md`。

## JsonTopo 数据交付

默认数据目录：

```text
examples/satcompute/input/topology/json/
```

仓库不再提交默认读取的测试 JSON；该目录用于放置甲方交付的 JsonTopo 数据。可参考：

```text
examples/satcompute/input/topology/json/examples/customer-73sat-6gs/
```

必需初始文件：

```text
nodes_0s.json
topology_0s.json
```

后续时间片支持两种方式：

```text
nodes_<time>s.json + topology_<time>s.json  # 全量快照，默认模式
patch_<time>s.json                          # 增量变化项，需开启 --jsonTopoPatchMode=true
```

建议同一个 `input/topology/json/` 目录一次只放一种方案：要么放全量快照文件，要么放 patch 文件。切换方案前先清理另一类后续时间片文件，避免交付和运行参数不一致。

全量快照模式下，后续时间片可以只提供发生变化的一类文件；例如只有链路变化时，
只提供 `topology_5s.json` 即可，节点状态会保持上一时刻。

`time_slices.json` 不是常规必需文件。默认情况下程序会扫描 `input/topology/json/` 文件名并按时间加载；只有文件名无法遵守规则或必须显式控制顺序时，才建议使用 `--timeSlicesJson=<path>`。

详细字段、命名规则和示例见 `examples/satcompute/input/topology/json/README.md`。

## 输出

仿真结束后终端会输出两类统计：

```text
业务数据性能
控制信息性能
```

指标包括 Tx/Rx 包数、字节数、丢包、时延、吞吐、抖动和丢包率。
