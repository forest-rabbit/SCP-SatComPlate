# JsonTopo 数据构建说明

本目录用于放置 `examples/link-selection/link-test` 的 JsonTopo 输入文件。
仓库不再提交默认读取的测试 JSON；`input/topology/json/` 用于放置甲方交付数据。
`input/topology/json/examples/` 只作为说明示例，不参与默认扫描。

## 甲方 link_output 时间序列

当前甲方格式位于 `examples/link_output/`，每个文件名是绝对时间：

```text
YYYY-MM-DD_HH-MM-SS.json
```

运行时填写目录、精确起始时间和正数仿真时长：

```bash
./waf --run "link-test \
  --linkOutputDir=examples/link-selection/input/topology/json/examples/link_output \
  --linkOutputStartTime=2024-01-02_00-02-00 \
  --simulationDuration=180 \
  --offeredload=0"
```

程序从起始快照的 `sat_id` 推导卫星，从 `feeder` 的另一个端点推导地面站；
节点数、链路数和文件数均不写死。后续文件是完整快照，并按相对起始时间调度。
目录可包含一天约 1440 个文件，扫描阶段只保存时间和路径，到点才解析内容。

该格式的 `delay` 是毫秒、`hold_time` 是秒、`clusterId` 是卫星簇编号。
详细字段和约束见 `examples/link_output/README.md`。

以下章节描述兼容保留的传统 `nodes_*.json/topology_*.json/patch_*.json` 协议。

## 传统 JsonTopo 必需初始文件

每次 JsonTopo 仿真都必须从 `0s` 的完整拓扑开始：

```text
nodes_0s.json
topology_0s.json
```

`nodes_0s.json` 创建全部 ns-3 节点；后续文件只能更新已有 `node_id`，
不能在仿真中途新增节点。`topology_0s.json` 安装初始完整链路集合。

客户尺度运行可参考 `examples/customer-73sat-6gs/`；只测试格式时可使用
`examples/snapshot/` 或 `examples/patch/` 中的最小示例。
如果启用 JsonTopo 但 `input/topology/json/` 缺少任一初始化文件，程序会立即报错退出；
可将交付文件放到该目录，或通过 `--nodesJson`、`--topologyJson` 显式指定文件路径。

## 命名规则

文件名使用固定小写前缀和 `.json` 后缀：

```text
nodes_<time>s.json
topology_<time>s.json
patch_<time>s.json
```

`<time>` 是仿真秒数，例如 `5`、`10`、`15.5`：

```text
nodes_5s.json
topology_5s.json
patch_10s.json
patch_15.5s.json
```

规则：

- `nodes_0s.json` 和 `topology_0s.json` 固定用于初始化。
- 后续运行期时间必须大于 `0`。
- 同一个 `input/topology/json/` 目录建议一次只放一种运行方案：要么放全量快照文件，要么放 patch 文件。
- 切换方案前先清理另一类后续时间片文件，避免交付内容和 `--jsonTopoPatchMode` 参数不一致。
- `input/topology/json/` 不要放无关 JSON 文件，避免被自动扫描。
- 文档示例放在 `input/topology/json/examples/` 子目录；程序默认不会扫描该子目录。

## 全量快照模式

全量快照是默认模式（`_jsonTopoPatchMode = false`）。每个时间戳都提供该时刻的完整状态。

推荐目录：

```text
input/topology/json/
├── nodes_0s.json
├── topology_0s.json
├── nodes_5s.json
└── topology_5s.json
```

运行期每个时间戳可以按变化内容提供：

```text
nodes_<time>s.json       # 该时刻完整节点/簇状态
topology_<time>s.json    # 该时刻完整活跃链路集合
```

如果某一时刻只有链路变化，可以只提供 `topology_<time>s.json`；节点状态会保持上一时刻。
如果某一时刻只有节点/簇信息变化，可以只提供 `nodes_<time>s.json`；链路状态会保持上一时刻。

注意：只要提供了 `topology_<time>s.json`，该文件就表示该时刻完整活跃链路集合，文件中没有出现的链路会被断开。如果甲方只能提供链路变化项，应使用 patch 模式。

## 增量 patch 模式

patch 模式用于只提交变化项。激活 Python 环境后，在仓库根目录运行：

```bash
./waf --run "link-test --jsonTopoPatchMode=true"
```

也可以在 `examples/link-selection/para.cc` 中将 `_jsonTopoPatchMode` 默认值改为 `true`。

推荐目录：

```text
input/topology/json/
├── nodes_0s.json
├── topology_0s.json
├── patch_5s.json
└── patch_10s.json
```

patch 文件只写修改项：

```json
{
  "nodes": {
    "update": [
      { "node_id": 12, "cluster_id": 3, "is_cluster_head": 1 }
    ]
  },
  "links": {
    "upsert": [
      { "node1_id": 12, "node2_id": 25, "delay": 8000, "link_bandwidth": 10000000 }
    ],
    "remove": [
      { "node1_id": 8, "node2_id": 19 }
    ]
  }
}
```

`nodes.update` 只覆盖显式写出的字段，未写字段保持上一状态。`links.upsert`
表示新增、恢复或更新链路；`links.remove` 表示断开链路。链路按无向边处理，
因此 `12-25` 和 `25-12` 表示同一条链路。

## 传统 JsonTopo 字段与单位

节点字段：

```text
node_id           必需，唯一节点 ID
node_type         sat、ground 或 gs；不写时默认为 sat
is_cluster        是否参与分簇，0 或 1
cluster_id        簇编号
is_cluster_head   是否为簇首，0 或 1
```

链路字段：

```text
node1_id/node2_id  必需，必须存在于 nodes_0s.json
type               sat、feeder、ground 或其他链路类型标签
delay              微秒 us
delay_ms           毫秒 ms，可替代 delay
link_bandwidth     kbps
bandwidth_gbps     Gbps，可替代 link_bandwidth
link_load_up       kbps
link_load_down     kbps
hold_time          秒；仅正数 feeder 生效，从链路安装、恢复或字段更新时开始计时。
                   到期后链路自动断开并重算 OSPF；未填写、0 或非 feeder 不自动到期。
```

## time_slices.json

`time_slices.json` 是可选索引文件，当前不推荐作为常规交付文件。默认情况下，
程序会直接扫描 `input/topology/json/` 文件名并按时间排序加载。

只有在文件名无法遵守上述命名规则，或必须显式控制加载顺序时，才建议使用
`--timeSlicesJson=<path>` 指定索引文件。全量快照索引项使用 `nodes_file` 和
`links_file`，patch 模式索引项使用 `patch_file`。

示例目录：

```text
examples/snapshot/                  最小全量快照示例
examples/patch/                     最小 patch 示例
examples/constellation-66sat-5gs/   66颗卫星 + 5个地面站的星座级示例
examples/customer-73sat-6gs/        73颗卫星 + 6个地面站的客户尺度示例
examples/link_output/               绝对时间命名的甲方完整快照示例
```
