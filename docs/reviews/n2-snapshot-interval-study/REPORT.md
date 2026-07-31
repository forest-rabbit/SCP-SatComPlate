# N2 快照间隔研究

本报告比较同一条 1 秒参考轨迹的 1/2/5/10/20 秒 zero-order-hold 快照。
实验固定使用 8000 µs 单向 ISL 时延、2 Gbps 链路、plus-grid 候选图和无权 hop-count 路由；没有人为制造拓扑变化。

## 结论

| 星座 | 已测窗口 | 主窗口推荐 | 已测窗口稳健推荐 | 已找到上界 | 全量 Python 候选等价 |
| --- | ---: | ---: | ---: | --- | --- |
| synthetic-351-telesat-t1-compute-117 | 3 | 20s | 20s | 否 | 是 |
| synthetic-66-compute-22 | 3 | 20s | 20s | 否 | 是 |
| synthetic-720-oneweb-compute-240 | 1 | 20s | 20s | 否 | 是 |

20 秒是当前固定时延、固定 plus-grid、无权 hop-count 模型下，最大已测试且成本最低的通过间隔。`upper_bound_identified=false`，不表示真实星座或动态断链模型的普适最优值，也不支持外推到 20 秒以上。
“已测窗口稳健推荐”只聚合表中实际完成的窗口；单窗口结果不构成跨轨道相位稳健性证明。

## 主窗口成本

下表只取 `window_index=0`，并将同一组合的三种 routing mode 重复记录去重。

| 星座 | snapshot count（1s → 20s） | route recomputation count （1s → 20s） | topology-only median wall time（1s → 20s） | output bytes（1s → 20s） | wall-time speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| synthetic-66-compute-22（66 星） | 1001 → 51 | 1000 → 50 | 31.384262414s → 1.483224877s | 19,769,955 → 1,012,204 | 21.16× |
| synthetic-351-telesat-t1-compute-117（351 星） | 1001 → 51 | 1000 → 50 | 3019.88620133s → 141.885884175s | 112,333,676 → 5,734,175 | 21.28× |
| synthetic-720-oneweb-compute-240（720 星） | 1001 → 51 | 1000 → 50 | 26875.25892482s → 1183.333502703s | 229,188,387 → 11,695,386 | 22.71× |

## 证据范围

- 结果行数：105（每个组合分别记录三种路由模式）。
- 窗口覆盖：synthetic-351-telesat-t1-compute-117=3 个；synthetic-66-compute-22=3 个；synthetic-720-oneweb-compute-240=1 个。
- topology-only 重复次数：3。
- Python：对每个已测参考秒，比较全部有序源宿节点对的可达性、最短跳数和 ECMP 候选下一跳集合；所有已测组合结果一致。
- C++：每个星座使用 64 个确定性分层 probe pair，在 `global-first`、`global-hash-per-flow`、`global-hrw-per-flow` 三种模式下比较实际选中下一跳；所有已测组合结果一致。
- `global-size-aware-hrw` 依赖活动流预留状态，不属于本次纯拓扑间隔审计。
- 完整快照、原始路由 JSONL 和运行日志位于实验工作目录，未提交。

详细逐项数据见 `interval-results.csv` 与 `interval-results.json`；机器可读决策见 `recommendations.json`。
