# topology-only 切片

`TopologySliceExporter` 是平台 `--topologyOnly=1` 使用的轻量输出组件。它只读取
ns-3.48 原生轨道坐标，并调用与正式在线仿真相同的固定 plus-grid 候选、距离
门控和 fixed/distance 时延策略；它不会创建 InternetStack、NetDevice、路由、
FlowMonitor、任务或指标对象。

切片写入 `outputDir/topology/`：

```text
nodes_0s.json
links_0s.json
nodes_1s.json
links_1s.json
...
```

节点文件只包含精确仿真时刻和稳定卫星 ID 对应的 ECEF 米制坐标：

```json
{
  "simulation_time_ns": 0,
  "nodes": [
    {"node_id": 0, "node_type": "sat", "x": 0.0, "y": 0.0, "z": 0.0}
  ]
}
```

链路文件包含全部固定候选，包括因距离门限暂时无效的候选：

```json
{
  "simulation_time_ns": 0,
  "links": [
    {
      "node1_id": 0,
      "node2_id": 1,
      "type": "sat",
      "active": false,
      "distance_m": 1000.0,
      "delay_ns": 3336,
      "link_bandwidth_bps": 2000000000
    }
  ]
}
```

`topologySliceInterval` 与正式仿真的 `networkUpdateInterval` 是两个独立的秒制
输入。切片不包含 schema、软件版本、SHA-256 或 manifest；文件名中的时间和
内容中的 `simulation_time_ns` 始终来自同一个整数纳秒采样点。
