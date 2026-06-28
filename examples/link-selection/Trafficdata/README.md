# Trafficdata 流量数据说明

本目录用于保存 `link-test` 的业务流量输入文件。拓扑数据放在 `Topodata/`，
流量矩阵放在 `Trafficdata/`，两类数据不要混放。

## 当前默认文件

```text
traffic_matrix(324).csv
```

热点流量模式下（`_trafficMode=0`），程序默认读取：

```text
examples/link-selection/Trafficdata/traffic_matrix(324).csv
```

当前 JsonTopo 示例为 66 颗卫星和 5 个地面站。程序会按实际卫星数量读取矩阵前
`sateNum` 列，因此现阶段仍可复用该 324 星流量矩阵做快速验证。

## 后续交付建议

如果甲方提供新的星座规模流量矩阵，建议命名为：

```text
traffic_matrix(<satellite_count>).csv
```

例如 `traffic_matrix(66).csv`。新增默认流量文件后，需要同步修改
`examples/link-selection/link-test.cc` 中的读取路径。
