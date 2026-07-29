# 快照间隔分析

本目录比较同一条 1 秒参考轨迹在不同快照间隔下的拓扑与 ECMP 语义。分析只
读取纯卫星 unified scenario，不改变轨道模型、ISL 候选图、链路 metric 或
N1 路由算法。

## 降采样合同

`downsample_scenario.py` 从一个已经通过 checker 的动态 1 秒场景复制
`t = 0, interval, ... duration` 的完整节点和链路快照。它不会重新生成 TLE、
重新传播轨道、重新判断链路状态或重新选择计算节点。低频场景在 C++ 中沿用
现有 zero-order hold 语义：下一张快照到达前继续使用最近一次状态。

```bash
uv run --locked python -m \
  contrib.satcompute.tools.analysis.topology_interval.downsample_scenario \
  --reference-dir /tmp/satcompute-reference \
  --interval-s 20 \
  --output-dir /tmp/satcompute-interval-20
```

输入必须满足：

- `topology.mode=dynamic`；
- `schedule.step_s=1`；
- `duration_s` 能被目标间隔整除；
- 输入是原始参考场景，而不是另一个降采样结果。

输出仍是完整 unified scenario，并重新计算 topology manifest、scenario
manifest 及全部聚合哈希。`compute-profile.json` 和选中快照逐字节复制。
scenario manifest 以 `reference_scenario_sha256` 记录参考场景的
`aggregate_scenario_sha256`，以 `downsample_interval_s` 记录目标间隔。

## 状态与 ECMP 指标

`edge_state.py` 在每个参考秒把低频场景解释为 last-snapshot-held，统计边集合
对称差、missed/spurious active edges、不可见的短暂 up/down、事件延后、
最长陈旧时段、连通分量和不可达无序节点对。连通性只使用 Python 标准库
DFS，不引入 `networkx`。

`ecmp_candidates.py` 使用当前平台的无权 hop-count 语义。对每个唯一活动边
集合、每个目的节点只运行一次反向 BFS，再为所有源节点推导排序后的等价
next-hop 卫星 ID。两边都不可达的节点对单独计数，不作为普通候选完全匹配来
提高比例。`compare_intervals.py` 将拓扑状态与 ECMP 结果合并：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.analysis.topology_interval.compare_intervals \
  --reference-dir /tmp/satcompute-reference \
  --held-dir /tmp/satcompute-interval-20
```

## Python/C++ 一致性门禁

`satcompute-route-candidate-audit` 只读枚举 ns-3 已安装路由的有效物理
下一跳；`route_probe.py` 对照 Python BFS 的全部有序节点对。该接口不修改
路由表、metric 或选路。

ns-3 会对只有一个邻居的叶子路由器跳过完整 SPF，并安装一条默认路由。
SatCompute 在找不到 exact `/32` host route 时也会回退该原生路由。因此只读
审计把这条默认路由映射为唯一有效物理下一跳；它与图上的唯一最短路候选一致，
但仍不把默认路由加入逐流 ECMP 选择器。

```bash
./waf --run-no-build "satcompute-route-candidate-audit \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --simulationDuration=1 \
  --auditTimes=0 \
  --outputFile=/tmp/diamond-candidates.jsonl"

uv run --locked python -m \
  contrib.satcompute.tools.analysis.topology_interval.route_probe \
  --topology-dir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --candidate-audit=/tmp/diamond-candidates.jsonl
```

门禁覆盖 static diamond、dynamic diamond 的全部快照，以及 synthetic-66
的 0 秒快照。任一候选 ID、可达状态或有序节点对缺失都会失败，不能继续用
Python ECMP 指标给出快照间隔结论。

生成结果用于本地实验或审查，应放在 `/tmp` 或仓库外的实验目录，不得提交。
