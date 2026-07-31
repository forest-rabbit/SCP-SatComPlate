# N2 快照间隔研究

本报告比较同一条 1 秒参考轨迹的 1/2/5/10/20 秒 zero-order-hold 快照。
实验固定使用 8000 µs 单向 ISL 时延、2 Gbps 链路、plus-grid 候选图和无权 hop-count 路由；没有人为制造拓扑变化。

## 结论

| 星座 | 已测窗口 | 主窗口推荐 | 已测窗口稳健推荐 | 已找到上界 | 全部候选等价 |
| --- | ---: | ---: | ---: | --- | --- |
| synthetic-351-telesat-t1-compute-117 | 3 | 20s | 20s | 否 | 是 |
| synthetic-66-compute-22 | 3 | 20s | 20s | 否 | 是 |
| synthetic-720-oneweb-compute-240 | 1 | 20s | 20s | 否 | 是 |

若“已找到上界”为否，推荐值只表示当前模型下最大已测试且成本最优的间隔，不表示真实星座的普适最优值，也不支持外推到 20 秒以上。
“已测窗口稳健推荐”只聚合表中实际完成的窗口；单窗口结果不构成跨轨道相位稳健性证明。

## 证据范围

- 结果行数：105（每个组合分别记录三种路由模式）。
- 窗口覆盖：synthetic-351-telesat-t1-compute-117=3 个；synthetic-66-compute-22=3 个；synthetic-720-oneweb-compute-240=1 个。
- topology-only 重复次数：3。
- `global-size-aware-hrw` 依赖活动流预留状态，不属于本次纯拓扑间隔审计。
- 完整快照、原始路由 JSONL 和运行日志位于实验工作目录，未提交。

详细逐项数据见 `interval-results.csv` 与 `interval-results.json`；机器可读决策见 `recommendations.json`。
