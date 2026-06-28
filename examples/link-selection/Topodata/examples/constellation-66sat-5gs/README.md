# 66星5地面站示例

本目录给出一个接近真实交付规模的 JsonTopo 示例：66 颗卫星、5 个地面站、5 个簇。
示例文件位于 `Topodata/examples/` 子目录，不会被程序默认扫描。

## 结构

```text
nodes_0s.json      # 初始节点：66 sat + 5 gs
topology_0s.json   # 初始完整拓扑
topology_5s.json   # 5s 仅链路变化的全量快照示例
nodes_10s.json     # 10s 仅节点/簇变化的全量快照示例
patch_15s.json     # 15s 增量 patch 示例
```

## 编号约定

```text
卫星节点：0-65
地面站：1000-1004
簇编号：1-5
```

5 个地面站分别作为 5 个簇首；卫星按连续编号划分到 5 个簇。
链路包括星间链路、馈电链路和地面站环形链路。

## 使用方式

若要用该示例测试，请将需要的 JSON 文件复制到 `examples/link-selection/Topodata/` 顶层。
顶层目录才会被默认扫描；本目录只作为交付格式示例。

全量快照模式可以测试：

```text
topology_5s.json   # 只有链路变化，节点保持上一状态
nodes_10s.json     # 只有节点变化，链路保持上一状态
```

patch 模式可以测试：

```text
patch_15s.json     # 只写变化项，未出现的节点和链路保持上一状态
```
