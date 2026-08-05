# 拓扑切片间隔分析

本目录比较同一份 1 秒参考 topology trace 在 2、5、10、20 秒等持有间隔下的
链路集合、连通性和无权最短路 ECMP 候选。它只消费
`tools/generation/topology/` 生成并校验过的 v0.3 JSON 切片，不计算轨道、不改变
候选 ISL，也不生成完整 scenario 配置。

## 降采样

为保持 ns-3.33 目录与入口对应，文件名仍为 `downsample_scenario.py`；在 v0.3 中
它的输入和输出都只是 topology trace：

```bash
python3 -m \
  contrib.satcompute.tools.analysis.topology_interval.downsample_scenario \
  --reference-dir /tmp/reference-trace \
  --interval-s 20 \
  --output-dir /tmp/held-20s
```

参考 trace 必须从 0 秒开始、每 1 秒一份切片，仿真时长必须能被目标间隔整除。
工具只逐字节复制被选中的 `nodes_*.json` 和 `topology_*.json`，然后重建
`manifest.json`。输出目录使用同目录临时目录后原子改名；已存在的目录不会被
覆盖。

## 比较

```bash
python3 -m \
  contrib.satcompute.tools.analysis.topology_interval.compare_intervals \
  --reference-dir /tmp/reference-trace \
  --held-dir /tmp/held-20s
```

`edge_state.py` 按 last-snapshot-held 语义统计边集合差异、遗漏/虚假活动边、事件
延后、最长陈旧时间、连通分量和不可达节点对。`ecmp_candidates.py` 对每个唯一
边集合和每个目的卫星做一次反向 BFS，比较全部有序源宿对的最短 hop 数和排序后
的等价下一跳 ID。两者都只使用 Python 标准库。

ns-3.33 版的 C++ route audit、成本实验编排器和冻结报告依赖已删除的完整
scenario 合同以及旧 metrics recorder，不能作为 v0.3 结果直接复用。候选与实际
选路的 C++ 门禁在阶段 5 路由接口恢复后接回；历史报告生成器不迁移，避免把
fixed-delay 静态结论误用于 distance 距离门控拓扑。
