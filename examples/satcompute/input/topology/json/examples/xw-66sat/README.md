# xw 66 星纯星上拓扑

本目录采用 xw 标签 `customer-jsontopo-v2.1` 的分离式 JSON 协议，并由其中的
`constellation-66sat-5gs` 输入清洗而来，只保留卫星 `0–65` 和星间链路。

0–110 秒每隔 10 秒提供一对文件：

```text
nodes_{0,10,...,110}s.json      # 每份均为相同的 66 颗卫星
topology_{0,10,...,110}s.json   # 每份均为相同的 132 条 ISL
```

12 个时间片中的节点和链路均不发生变化。

清洗规则：

- 删除 5 个地面站及全部 feeder、ground 链路；
- 删除所有 cluster 字段；
- 节点保留 `node_id`、`node_type`；
- 链路保留 `node1_id`、`node2_id`、`type`、`delay`、
  `link_bandwidth`；
- `delay` 使用 µs，带宽使用 kbps。
