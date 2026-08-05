# 星座与算力输入

`constellations/` 保存 ns-3.48 原生 LEO shell CSV，只描述星座物理结构；
`resources/` 保存与轨道状态分离的静态算力 JSON。仿真时长、拓扑更新、时延、
路由和输出目录都由 `para.cc`/CLI 控制，不写入这两类文件。

正式仿真根据星座 CSV 在线计算坐标和链路。`topologyOnly=1` 时，平台把每个采样
点的 `nodes_<time>s.json` 与 `links_<time>s.json` 写入输出目录；这些切片用于
可视化和未来故障建模，不作为正常网络仿真的回放输入。

ComputeProfile 示例：

```json
{
  "compute_nodes": [
    {
      "node_id": 3,
      "compute_rate_work_units_per_second": 1000000
    }
  ]
}
```

根对象只允许 `compute_nodes`。节点 ID 必须属于当前星座且不能重复，算力速率是
正整数 work units/s。`resources/workload/` 中分别提供 22 个计算节点和全部
66 个计算节点的示例配置。
