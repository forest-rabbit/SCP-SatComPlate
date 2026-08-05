# 历史拓扑回放快照

本目录保存迁移回归仍使用的旧版 `nodes_<time>s.json` 与
`topology_<time>s.json` 读取逻辑。它与 `--topologyOnly=1` 生成的
`nodes_<time>s.json`、`links_<time>s.json` 是两个不同合同：正式在线仿真不会
读取 topology-only 切片，未来故障模型也只从这些切片生成故障事件。

旧版回放目录的每个时间点必须同时存在节点和链路文件。时间片按
`0, networkUpdateInterval, 2 * networkUpdateInterval, ...` 选择，并严格早于仿真
终点。节点集合必须非空、唯一且在所有时间点保持一致；链路端点必须引用节点
集合，同一无向链路不能重复。

legacy 节点文件只含 `nodes` 数组，每项为 `node_id` 和固定值 `sat` 的
`node_type`。legacy 链路文件只含 `links` 数组，每项为 `node1_id`、`node2_id`、
`type`、微秒制 `delay` 和 kbps 制 `link_bandwidth`。读取时只转换一次为整数
纳秒和 bit/s；fixed 模式仍由平台 fixed delay 覆盖。

迁移期的 0.2 manifest/切片解析暂时保留给历史 fixture，但平台不再生成这种
格式，也不再维护对应 schema、SHA-256 或 manifest 生成工具。
