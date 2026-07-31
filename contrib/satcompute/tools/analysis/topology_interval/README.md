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
  candidate-gate \
  --topology-dir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --candidate-audit=/tmp/diamond-candidates.jsonl
```

门禁覆盖 static diamond、dynamic diamond 的全部快照，以及 synthetic-66
的 0 秒快照。任一候选 ID、可达状态或有序节点对缺失都会失败，不能继续用
Python ECMP 指标给出快照间隔结论。

## 实际选中下一跳

候选集合一致不自动等于实际选择一致。`route_probe.py generate-pairs` 从统一
场景生成最多 64 个确定性有序节点对，分层覆盖同轨相邻/远距离、相邻/非相邻
轨道面、边界到内部，以及计算/普通节点组合。每对节点使用固定且唯一的 UDP
端口，`probe-pairs.json` 的 SHA-256 随报告保存。

```bash
uv run --locked python -m \
  contrib.satcompute.tools.analysis.topology_interval.route_probe \
  generate-pairs \
  --scenario-dir=/tmp/satcompute-reference \
  --output=/tmp/probe-pairs.json
```

`satcompute-route-selection-audit` 不发送业务数据，而是用这些真实五元组调用
当前 C++ `RouteOutput`。对 hash 和 HRW，它同时捕获既有
`EcmpRouteDecision` 事件；对 `global-first`，记录原生首条路由。审计覆盖：

```text
global-first
global-hash-per-flow
global-hrw-per-flow
```

不覆盖依赖活动流预留状态的 `global-size-aware-hrw`。每个快照在拓扑更新后
1 ns 触发一次审计，输出有效候选、实际物理下一跳、route epoch、事件候选数、
gateway、interface、hash 和 selection reason。审计调用不发送 packet，
不改变链路、metric、路由表或选择算法。

```bash
./waf --run-no-build "satcompute-route-selection-audit \
  --topologyDir=/tmp/satcompute-reference/topology \
  --simulationDuration=20.1 \
  --auditTimes=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20 \
  --routingMode=global-hrw-per-flow \
  --ecmpHashSeed=1 \
  --probePairs=/tmp/probe-pairs.json \
  --outputFile=/tmp/reference-hrw.jsonl"
```

`compare-selection` 将低频审计按 last-audited snapshot 展开到每个参考秒，比较
实际下一跳、参考选择在 held 候选中的存活、有效候选数和原始事件候选数。
route epoch 只作为证据记录，不用“epoch 次数”冒充下一跳变化：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.analysis.topology_interval.route_probe \
  compare-selection \
  --reference-audit=/tmp/reference-hrw.jsonl \
  --held-audit=/tmp/held-hrw.jsonl \
  --node-count=66
```

生成结果用于本地实验或审查，应放在 `/tmp` 或仓库外的实验目录，不得提交。

## 三规模完整研究

`run_interval_study.py` 编排 66、351、720 星三个配置。66 星和 351 星使用
WGS72 近圆轨道周期的 `0`、`P/3`、`2P/3` 三个窗口；720 星只保留
`offset=0` 主窗口。周期和 offset 均由配置高度确定，`P/3` 与 `2P/3`
取最近整数秒。当前冻结范围为：

| 星座 | 轨道周期（秒） | 实际测试 offset（秒） |
| --- | ---: | --- |
| 66 | 6027.130743814793 | 0, 2009, 4018 |
| 351 | 6326.357647436802 | 0, 2109, 4218 |
| 720 | 6565.2957073948755 | 0 |

66 星和 351 星的三个窗口都得到唯一边集合、唯一 ECMP 候选指纹和完全一致的
1/2/5/10/20 秒结果；720 星主窗口也得到同样的静态导出合同。因此正式研究不
重复运行 720 星的另外两个轨道相位。报告会把每个星座实际完成的窗口数写入
结果，并明确单窗口结果不构成跨轨道相位稳健性证明。该范围只适用于当前固定
时延、固定 plus-grid 邻接和无权 hop-count 模型；不能外推到按距离启停 ISL
的动态链路模型。

每个窗口先生成唯一的 1 秒 reference，再以逐字节复制方式得到
1/2/5/10/20 秒 held 场景。实际下一跳覆盖 `global-first`、
`global-hash-per-flow` 和 `global-hrw-per-flow`；纯拓扑成本只在每个星座
主窗口重复三次。

```bash
uv run --locked python -m \
  contrib.satcompute.tools.analysis.topology_interval.run_interval_study \
  --work-dir /tmp/satcompute-n2-interval-study \
  --report-dir docs/reviews/n2-snapshot-interval-study
```

工作目录保存完整快照、C++ JSONL、成本原始值和日志，不能提交。编排器会严格
检查已有场景的配置与 provenance；有效的场景、route audit 和成本重复会被
复用，因此中断后执行同一命令即可续跑。Python comparison 证据也带版本和
场景哈希，重复续跑会生成相同报告字节。

如果复用的原始证据由较早提交生成，必须增加：

```text
--evidence-commit <full-or-resolvable-commit>
```

报告中的 `satcompute_commit` 记录原始实验证据提交，
`report_generator_commit` 记录当前报告生成器提交。全新运行不传该参数时，
二者都使用当前 `HEAD`。

`satcompute-topology-cost-audit` 只创建卫星、加载 ISL 快照并执行原生全局
路由重算，不安装 NetworkTransfer、任务、probe 或 FlowMonitor。内部 wall
time 覆盖初始化、全部快照加载和路由重算；GNU time 单独记录进程 peak RSS。
最终 route epoch 必须等于 `snapshot_count - 1`，否则该次成本证据失败。

完整实验默认最多并行三个相互独立的 C++ 进程，并把它们固定到三个分离的
allowed logical CPU；同一成本组合的三次 repeat 各占一个 CPU，route audit
也采用相同上限。单次 ns-3 仿真仍是单线程，场景、seed、快照与统计口径均不
改变。报告记录 waf build profile、CPU 列表、并行上限与成本协议版本。需要
完全串行的机器可显式使用：

```text
--maximum-parallel-cpp-runs 1
```

已有成本证据只有在 build profile、CPU、并行上限和协议版本全部相同时才会
复用，避免把不同测量条件混入同一组最小值/中位数/最大值。

最终只提交：

```text
docs/reviews/n2-snapshot-interval-study/
├── REPORT.md
├── interval-results.csv
├── interval-results.json
├── recommendations.json
└── probe-pairs.json
```

CSV 每个星座、窗口、间隔和 routing mode 各有一行。只有主窗口含三次
topology-only wall time 与 peak RSS；其他窗口的这些字段为空，但仍记录
快照数和由调度合同验证的路由重算次数。

推荐严格选择同时满足边状态、连通性、全量 Python ECMP 候选以及三种 C++
实际选路 gate 的最大已测试间隔。若所有候选都等价，返回 20 秒并写明
`upper_bound_identified=false`；这只表示当前 fixed-delay、plus-grid、
无权 hop-count 模型下的最大已测试成本最优值，不支持外推到 20 秒以上。
