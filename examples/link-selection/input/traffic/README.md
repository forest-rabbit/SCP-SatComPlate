# input/traffic 流量数据说明

本目录用于保存 `link-test` 的业务流量输入文件。拓扑数据放在 `input/topology/`，
流量矩阵放在 `input/traffic/`，两类数据不要混放。

## 当前默认文件

```text
traffic_matrix(324).csv
```

热点流量模式下（`_trafficMode=0`），程序默认读取：

```text
examples/link-selection/input/traffic/traffic_matrix(324).csv
```

客户尺度 JsonTopo 示例应使用：

```text
traffic_matrix(73).csv
```

运行时显式指定：

```bash
--trafficMatrix=examples/link-selection/input/traffic/traffic_matrix(73).csv
```

## 后续交付建议

如果甲方提供新的星座规模流量矩阵，建议命名为：

```text
traffic_matrix(<satellite_count>).csv
```

例如 `traffic_matrix(66).csv`。运行时通过以下参数选择，不需要修改源码：

```bash
--trafficMatrix=examples/link-selection/input/traffic/traffic_matrix(66).csv
```
