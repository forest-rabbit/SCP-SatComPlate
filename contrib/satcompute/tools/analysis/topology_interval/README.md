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

生成结果用于本地实验或审查，应放在 `/tmp` 或仓库外的实验目录，不得提交。
