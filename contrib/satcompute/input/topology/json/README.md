# 分离式卫星 JSON 全量快照

本协议参考 xw `customer-jsontopo-v2.1`。每个时间片必须同时提供节点文件和
链路文件：

```text
nodes_<time>s.json
topology_<time>s.json
```

`<time>` 是相对仿真起点的非负秒数。`nodes_0s.json` 与
`topology_0s.json` 必须存在；程序按时间升序加载
`simulationDuration` 闭区间内的完整快照。

所有节点快照必须列出完全相同的卫星集合。所有链路快照都是完整活跃 ISL
集合：上一时间片存在、本时间片缺失的链路会被关闭。

## 节点文件

```json
{
  "nodes": [
    {
      "node_id": 0,
      "node_type": "sat"
    }
  ]
}
```

- `node_id`：唯一的非负卫星 ID；
- `node_type`：必须为 `sat`。

## 链路文件

```json
{
  "links": [
    {
      "node1_id": 0,
      "node2_id": 1,
      "type": "sat",
      "delay": 8000,
      "link_bandwidth": 10000000
    }
  ]
}
```

- `node1_id`、`node2_id`：配对节点快照中的卫星 ID，不能相同；
- `type`：必须为 `sat`；
- `delay`：单向传播时延，单位 µs；
- `link_bandwidth`：链路带宽，单位 kbps，必须大于 0；

时延和带宽用于 PointToPoint 链路配置。业务到达不写入拓扑文件，而由独立的
legacy CSV、NetworkTransfer 或 TaskTrace 提供。

同一链路文件中不能重复声明同一条无向 ISL。仓库样例位于
[`examples/xw-66sat/`](examples/xw-66sat/)。

## ComputeProfile 静态资源

节点的静态计算能力属于 topology side，放在 `resources/`，不写入
`nodes_<time>s.json`，也不与任务到达混合。任务模式通过
`--computeProfile=<file>` 显式读取一个文件：

```json
{
  "schema_version": "0.1",
  "compute_nodes": [
    {
      "node_id": 3,
      "compute_rate_work_units_per_second": 1000000
    }
  ]
}
```

根对象只允许 `schema_version` 和 `compute_nodes`；每项只允许 `node_id` 和
`compute_rate_work_units_per_second`。`node_id` 必须引用拓扑中存在的卫星且
不能重复，速率是正整数，单位为 work units/s。数组按 `node_id` canonical
sort，因此 JSON 中的排列不影响运行与结构化输出。

测试资源位于 [`resources/test/`](resources/test/)。与之配对的任务到达属于
traffic side，位于 `input/traffic/json/task/`，其格式见
[`../../traffic/json/README.md`](../../traffic/json/README.md)。
