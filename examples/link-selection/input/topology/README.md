# input/topology 拓扑数据目录

`input/topology/` 是 `link-test` 的拓扑数据总目录，用于统一放置甲方提供的拓扑输入。

## 目录分工

```text
input/topology/
├── json/    # JsonTopo 数据交付目录
└── csv/     # 传统 CSV 拓扑数据目录
```

`json/` 和 `csv/` 是两种不同拓扑输入方案。当前主线优先使用 JsonTopo；传统 CSV 模式主要保留兼容旧实验流程。

## JsonTopo

JsonTopo 默认扫描：

```text
examples/link-selection/input/topology/json/
```

详细命名规则、snapshot/patch 格式和示例见：

```text
examples/link-selection/input/topology/json/README.md
```

## 传统 CSV

传统拓扑模式默认读取：

```text
examples/link-selection/input/topology/csv/topo(324).csv
```

说明见：

```text
examples/link-selection/input/topology/csv/README.md
```
