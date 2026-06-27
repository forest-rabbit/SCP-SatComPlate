# link-test 运行说明（JsonTopo 版本）

数据说明：当前 `Topodata/` 中的 JSON 文件是为了调试 JsonTopo 流程自行生成的测试/示例数据，不是甲方提供的真实数据。后续接入甲方真实拓扑数据时，应将符合 JsonTopo 格式的节点、链路和时间片文件放入 `examples/link-selection/Topodata/`，或通过 `--nodesJson`、`--topologyJson`、`--timeSlicesJson` 显式指定数据路径。

甲方数据建议放置与命名：

```text
examples/link-selection/Topodata/
├── nodes_0s.json        # 必需：初始节点状态
├── topology_0s.json     # 必需：初始链路状态
├── nodes_5s.json        # 可选：5s 节点状态更新
├── topology_5s.json     # 可选：5s 链路状态更新
└── time_slices.json     # 可选：显式时间片索引
```

推荐命名规则：

```text
nodes_<time>s.json
topology_<time>s.json
```

其中 `<time>` 是仿真秒数，例如 `5s`、`7s`、`10s`。如果不提供 `time_slices.json`，程序会自动扫描 `Topodata/`，按上述命名规则配对时间片。

## 1. 入口文件

- 主程序：`examples/link-selection/link-test.cc`
- 拓扑构建：`examples/link-selection/topo.cc`
- JSON 拓扑解析：`examples/link-selection/topo-json.cc`
- 全局参数：`examples/link-selection/para.cc`
- waf 目标：`link-test`

## 2. 当前推荐运行方式

在仓库根目录执行：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf --run link-test
```

当前 `para.cc` 中默认启用 JsonTopo：

```text
_useJsonTopo = true
offeredload = 0.0001
_tranProc = 0  // UDP
linkBandwidth = 10000000000  // 10Gbps
totalTimeStep = 110
```

这组默认参数已经可以作为快速自检配置。实测输出应包含：

```text
[JSON-TOPO] 节点创建完成
[JSON-TOPO] 初始化完成
[TRAFFIC] 读取流量矩阵
Simulation real - time cost
```

## 3. 首次构建

如果 `build/` 目录不存在，或者修改了 waf / wscript / 模块依赖，可以重新配置：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
PATH="$PWD/.venv/bin:$PATH" ./waf build
```

如果需要完全清理后重建：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf clean
PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
PATH="$PWD/.venv/bin:$PATH" ./waf build
```

## 4. 可覆盖的命令行参数

- `--offeredload=<double>`：业务负载。当前快速验证推荐使用默认值 `0.0001`。
- `--linkBandwidth=<bps>`：链路带宽；当 JSON 链路未写带宽时作为兜底值。
- `--tranProtocol=<0|1>`：`0=UDP`，`1=TCP`。
- `--useJsonTopo=<true|false>`：是否使用 JsonTopo。默认 `true`。
- `--nodesJson=<path>`：初始节点 JSON 文件；默认 `examples/link-selection/Topodata/nodes_0s.json`。
- `--topologyJson=<path>`：初始链路 JSON 文件；默认 `examples/link-selection/Topodata/topology_0s.json`。
- `--timeSlicesJson=<path>`：时间片索引 JSON 文件；不指定时默认扫描 `examples/link-selection/Topodata/`。
- `--isSate=<1|2|3|4>`：传统拓扑模式使用；JsonTopo 模式下不决定节点数量。
- `--consType=<0|1>`：传统拓扑模式使用；`0=Walker Star`，`1=Walker Delta`。

## 5. JsonTopo 输入文件

默认目录：

```text
examples/link-selection/Topodata/
```

默认初始拓扑：

```text
nodes_0s.json
topology_0s.json
```

当前会自动扫描并加载后续时间片，例如：

```text
nodes_5s.json
topology_5s.json
nodes_7s.json
topology_7s.json
```

运行时日志会显示实际加载的时间片和文件路径。

### 5.1 节点文件格式

文件名示例：

```text
nodes_0s.json
nodes_5s.json
nodes_10s.json
```

基本格式：

```json
{
  "nodes": [
    {
      "node_id": 0,
      "node_type": "sat",
      "is_cluster": 1,
      "cluster_id": 1,
      "is_cluster_head": 0
    },
    {
      "node_id": 66,
      "node_type": "ground",
      "is_cluster": 0,
      "cluster_id": 0,
      "is_cluster_head": 0
    }
  ]
}
```

字段说明：

```text
node_id           必需。节点 ID，链路文件中的 node1_id/node2_id 应引用该 ID。
node_type         可选。节点类型，推荐 sat、ground 或 gs；默认 sat。
is_cluster        可选。是否参与分簇，0 或 1；默认 0。
cluster_id        可选。簇编号；默认 0。
is_cluster_head   可选。是否为簇首，0 或 1；默认 0。
```

注意：`nodes_0s.json` 用于创建初始 ns-3 节点；运行期的 `nodes_5s.json`、`nodes_10s.json` 等只用于更新已有节点的簇信息。当前代码不支持在仿真运行中途通过时间片新增 ns-3 节点。

### 5.2 链路文件格式

文件名示例：

```text
topology_0s.json
topology_5s.json
topology_10s.json
```

基本格式：

```json
{
  "links": [
    {
      "node1_id": 0,
      "node2_id": 1,
      "type": "sat",
      "delay": 8000,
      "link_bandwidth": 10000000,
      "link_load_up": 0,
      "link_load_down": 0
    }
  ]
}
```

字段说明：

```text
node1_id          必需。链路一端节点 ID，应对应 nodes 文件中的 node_id。
node2_id          必需。链路另一端节点 ID，应对应 nodes 文件中的 node_id。
type              可选。链路类型，推荐 sat、feeder 或 ground；默认 sat。
delay             可选。链路时延，单位为微秒 us；默认 0。
link_bandwidth    可选。链路带宽，单位为 kbps；未提供时使用 linkBandwidth 参数兜底。
link_load_up      可选。上行负载，单位为 kbps；默认 0。
link_load_down    可选。下行负载，单位为 kbps；默认 0。
hold_time         可选。链路保持时间，单位为秒 s；默认 0。
```

### 5.3 time_slices.json 格式

如果使用目录扫描模式，可以不提供 `time_slices.json`。如果需要显式指定时间片顺序和文件路径，可以提供：

```json
{
  "time_slices": [
    {
      "time": 5,
      "nodes_file": "nodes_5s.json",
      "links_file": "topology_5s.json"
    },
    {
      "time": 10,
      "nodes_file": "nodes_10s.json",
      "links_file": "topology_10s.json"
    }
  ]
}
```

字段说明：

```text
time         可选。仿真秒数；不填时会尝试从 nodes_5s.json/topology_5s.json 文件名解析。
nodes_file   可选。该时间片的节点更新文件。
links_file   可选。该时间片的链路更新文件。
```

路径可以写相对路径；相对路径会按 `time_slices.json` 所在目录解析。

## 6. 流量输入

热点流量模式下（`_trafficMode=0`），程序读取：

```text
examples/link-selection/traffic_matrix(324).csv
```

当前 JsonTopo 默认节点规模为 66 颗卫星和 10 个地面节点。流量矩阵按卫星业务源宿关系读取。

## 7. 主流程

1. 解析命令行参数并打印关键配置。
2. 调用 `initTopo()` 创建节点、安装协议栈、创建链路并分配地址。
3. JsonTopo 模式下加载初始 JSON 拓扑，并按时间片更新节点和链路状态。
4. 调用 `buildApp()` 安装服务器与客户端应用。
5. 安装 FlowMonitor，运行仿真到 `totalTimeStep`。
6. 仿真结束后调用 `dealSimInfo()` 汇总业务流与控制流性能指标。

## 8. 统计输出

终端会输出两类统计：

```text
业务数据性能
控制信息性能
```

指标包括 Tx/Rx 包数、字节数、丢包、时延、吞吐、抖动和丢包率。

## 9. 传统 CSV 拓扑模式

如果通过 `--useJsonTopo=false` 切回传统拓扑，程序会使用：

```text
examples/link-selection/topo(324).csv
```

该模式主要保留兼容旧实验流程；当前新增拓扑数据优先使用 JsonTopo。
