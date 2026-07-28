# 分离式卫星 JSON 全量快照

SatCompute 只接受分离式 JSON 卫星全量快照。每个时间片必须同时提供节点文件
和链路文件：

```text
nodes_<time>s.json
topology_<time>s.json
```

`<time>` 是相对仿真起点的非负秒数。`nodes_0s.json` 与
`topology_0s.json` 必须存在；程序按时间升序加载
`simulationDuration` 闭区间内的完整快照。

所有节点快照必须列出完全相同的卫星集合。所有链路快照都是完整活跃 ISL
集合：上一时间片存在、本时间片缺失的链路会被关闭。CSV 建图、增量 patch、
地面站和 cluster 格式均不属于项目输入契约。

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
- `link_bandwidth`：链路带宽，单位 kbps，必须大于 0。

时延和带宽用于 PointToPoint 链路配置。业务到达不写入拓扑文件，而由独立的
NetworkTransfer 或 TaskTrace JSON 提供。

同一链路文件中不能重复声明同一条无向 ISL。仓库样例位于
[`examples/xw-66sat/`](examples/xw-66sat/)。
`tests/fqcodel-bottleneck/` 是四颗纯卫星组成的两入口单出口测试拓扑：
入口各为 1 Gbit/s，出口为 10 Mbit/s，仅用于区分默认 FqCoDel QueueDisc
与 PointToPointNetDevice DropTail 队列的丢弃原因。
`tests/diamond-4-hrw-dynamic/` 是 HRW 路由回归拓扑：完整快照依次保持候选
但改变记录顺序、删除一条等价支路、再恢复该支路，用于验证跨 route epoch
稳定性和候选增删时的最小 flow 迁移。

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

测试资源位于 [`resources/test/`](resources/test/)。正式 2 Gbit/s 静态
66 星压力配置位于 [`resources/workload/`](resources/workload/)：不带
`all` 的文件保留 22 个计算节点对照，带 `all` 的文件覆盖节点 0–65。
与之配对的任务到达属于 traffic side，位于
[`../traffic/task/`](../traffic/task/)，其格式见
[`../traffic/README.md`](../traffic/README.md)。
