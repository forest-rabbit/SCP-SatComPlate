# link-test 运行说明（JsonTopo 版本）

本文件说明 `link-test` 的构建、运行参数、仿真流程和输出。JsonTopo 文件格式、
命名规则、字段单位和甲方交付示例统一维护在 `Topodata/README.md`，避免多处重复。

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

然后构建并运行。仓库内置的 66 星 5 地面站示例可用于快速自检：

```bash
./waf build
./waf --run "link-test --nodesJson=examples/link-selection/Topodata/examples/constellation-66sat-5gs/nodes_0s.json --topologyJson=examples/link-selection/Topodata/examples/constellation-66sat-5gs/topology_0s.json"
```

如果 `build/` 目录不存在，或修改了 waf / wscript / 模块依赖，先重新配置：

```bash
./waf configure --enable-examples --enable-tests
./waf build
```

如果没有激活环境，或直接运行 `./waf` 出现 Python 环境相关错误，可以临时使用：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf --run "link-test --nodesJson=examples/link-selection/Topodata/examples/constellation-66sat-5gs/nodes_0s.json --topologyJson=examples/link-selection/Topodata/examples/constellation-66sat-5gs/topology_0s.json"
```

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
[TOPO:Nodes] 节点创建完成
[TOPO:Links] 初始链路安装完成
[TOPO:Init] JsonTopo 初始化完成
[TRAFFIC] 读取流量矩阵
Simulation real - time cost
```

## 3. 命令行参数

- `--offeredload=<double>`：业务负载，快速验证可使用默认值 `0.0001`。
- `--linkBandwidth=<bps>`：链路带宽；当 JSON 链路未写带宽时作为兜底值。
- `--tranProtocol=<0|1>`：`0=UDP`，`1=TCP`。
- `--useJsonTopo=<true|false>`：是否使用 JsonTopo，默认 `true`。
- `--jsonTopoPatchMode=<true|false>`：后续时间片格式；`false=全量快照`，`true=增量 patch`。
- `--nodesJson=<path>`：初始节点 JSON 文件。
- `--topologyJson=<path>`：初始链路 JSON 文件。
- `--timeSlicesJson=<path>`：可选索引文件；常规情况下不需要，默认按文件名扫描 `Topodata/`。
- `--isSate=<1|2|3|4>`：传统拓扑模式使用；JsonTopo 模式下不决定节点数量。
- `--consType=<0|1>`：传统拓扑模式使用；`0=Walker Star`，`1=Walker Delta`。

示例：运行增量 patch 模式：

```bash
./waf --run "link-test --jsonTopoPatchMode=true --nodesJson=examples/link-selection/Topodata/examples/constellation-66sat-5gs/nodes_0s.json --topologyJson=examples/link-selection/Topodata/examples/constellation-66sat-5gs/topology_0s.json"
```

若要自动加载 patch 时间片，请将 `patch_<time>s.json` 放到 `Topodata/` 顶层；`Topodata/examples/` 下的文件只作为格式示例。

示例：指定初始 JsonTopo 文件：

```bash
./waf --run \
  "link-test --nodesJson=examples/link-selection/Topodata/nodes_0s.json --topologyJson=examples/link-selection/Topodata/topology_0s.json"
```

## 4. JsonTopo 数据入口

默认数据目录：

```text
examples/link-selection/Topodata/
```

该目录顶层 JSON 会被仿真读取；仓库不再提交顶层测试 JSON。`Topodata/examples/` 只放说明示例，不参与默认扫描。

常规交付只需要遵守文件命名规则，程序会按时间自动加载：

```text
nodes_0s.json + topology_0s.json            # 必需初始拓扑
nodes_<time>s.json + topology_<time>s.json  # 全量快照模式
patch_<time>s.json                          # 增量 patch 模式
```

全量快照模式下，后续时间片的 `nodes_<time>s.json` 和 `topology_<time>s.json`
可以只提供发生变化的一类；缺少的节点或链路部分会保持上一状态。

详细规范见：

```text
examples/link-selection/Topodata/README.md
```

## 5. 流量输入

热点流量模式下（`_trafficMode=0`），程序读取：

```text
examples/link-selection/traffic_matrix(324).csv
```

当前 JsonTopo 示例节点规模为 66 颗卫星和 5 个地面站。流量矩阵按卫星业务源宿关系读取。

## 6. 主流程

1. 解析命令行参数并打印关键配置。
2. 调用 `initTopo()` 创建节点、安装协议栈、创建链路并分配地址。
3. JsonTopo 模式下加载初始 JSON 拓扑，并按时间片更新节点和链路状态。
4. 调用 `buildApp()` 安装服务器与客户端应用。
5. 安装 FlowMonitor，运行仿真到 `totalTimeStep`。
6. 仿真结束后调用 `dealSimInfo()` 汇总业务流与控制流性能指标。

## 7. 统计输出

终端会输出两类统计：

```text
业务数据性能
控制信息性能
```

指标包括 Tx/Rx 包数、字节数、丢包、时延、吞吐、抖动和丢包率。

## 8. 传统 CSV 拓扑模式

如果通过 `--useJsonTopo=false` 切回传统拓扑，程序会使用：

```text
examples/link-selection/topo(324).csv
```

该模式主要保留兼容旧实验流程；当前新增拓扑数据优先使用 JsonTopo。
