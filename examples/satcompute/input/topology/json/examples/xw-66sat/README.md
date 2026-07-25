# xw 66 星纯星上拓扑

本目录采用 xw 标签 `customer-jsontopo-v2.1` 的分离式 JSON 协议，并由其中的
`constellation-66sat-5gs` 输入清洗而来，只保留卫星 `0–65` 和星间链路。

初始快照由一对文件组成：

```text
nodes_0s.json      # 66 颗卫星
topology_0s.json   # 132 条 ISL
```

清洗规则：

- 删除 5 个地面站及全部 feeder、ground 链路；
- 删除所有 cluster 字段；
- 节点保留 `node_id`、`node_type`；
- 链路保留 `node1_id`、`node2_id`、`type`、`delay`、
  `link_bandwidth`、`link_load_up` 和 `link_load_down`；
- `delay` 使用 µs，带宽与链路负载使用 kbps。
