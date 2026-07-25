# xw 66 星纯星上拓扑

本目录由 xw `customer` 分支的 `constellation-66sat-5gs` 输入清洗而来，只保留
卫星 `0–65` 和星间链路。

时间片映射：

```text
2024-01-02_00-00-00.json  -> 原 topology_0s.json，132 条 ISL
2024-01-02_00-00-15.json  -> 应用原 patch_15s.json 后，131 条 ISL
```

原 `topology_5s.json` 的星间链路没有变化，`nodes_10s.json` 只修改 cluster
状态，因此没有生成无效的 5 秒和 10 秒快照。15 秒时关闭 `6↔17`，并把
`4↔15` 更新为 14 ms、12 Gbps。

清洗规则：

- 删除 5 个地面站及全部 feeder、ground 链路；
- 删除 cluster、链路负载和 `type` 字段；
- 将原始 `delay` 从 µs 转换为当前格式的 ms；
- 原始 10 Gbps 链路使用项目默认带宽，只有 12 Gbps 链路显式写入
  `bandwidth_bps`。
