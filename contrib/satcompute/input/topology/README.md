# 星座与算力输入

`input/topology/` 保存相对稳定的物理结构和计算资源，不保存一次实验的运行策略：

```text
topology/
├── constellations/
│   └── synthetic-66.csv
└── resources/workload/
    ├── xw-66sat-static-2g-compute-profile.json
    └── xw-66sat-static-2g-all-compute-profile.json
```

仿真时间、更新周期、距离门限、时延、带宽、路由和输出目录属于 `para.cc`/CLI，
不能写进这些文件。

## Constellation

星座 CSV 只描述一个 ns-3.48 原生圆轨道 shell。字段、范围和默认 66 星文件见
[`constellations/README.md`](constellations/README.md)。正式仿真直接从该文件创建
原生 mobility 并在线计算坐标；topology-only 也读取同一文件。

## ComputeProfile

ComputeProfile 根对象只允许 `compute_nodes`：

```json
{
  "compute_nodes": [
    {
      "node_id": 3,
      "compute_rate_work_units_per_second": 1500000
    }
  ]
}
```

| 字段 | 类型 | 约束 |
|---|---|---|
| `node_id` | `uint32` | 必须属于当前星座且在文件中唯一 |
| `compute_rate_work_units_per_second` | `uint64` | 必须大于 0 |

数组必须非空，不接受未知字段，也不包含 schema/version/hash。reader 会按 `node_id`
排序，因此数组原始顺序不影响运行。

## 正式资源文件

| 文件 | 计算节点 | 每节点速率 |
|---|---:|---:|
| `xw-66sat-static-2g-compute-profile.json` | 22 个，ID 为 `0,3,...,63` | 1,500,000 work units/s |
| `xw-66sat-static-2g-all-compute-profile.json` | 全部 66 个卫星 | 1,500,000 work units/s |

前者与 `input/traffic/workload/stress-40.json` 以及 20 任务完整示例配套；后者与
`size-aware-60.json` 配套。TaskTrace 引用的每个 `compute_node_id` 必须出现在所选
ComputeProfile 中。

## 与拓扑切片的区别

`nodes_<time>s.json` 和 `links_<time>s.json` 是 `topologyOnly` 写入 outputDir 的
运行输出，不属于本目录输入。它们供前端可视化和未来故障生成使用，正式仿真不会
回放这些 JSON。
