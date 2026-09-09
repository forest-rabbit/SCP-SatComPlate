# N4B F1 参数标定（历史归档）

以下保留旧模型的标定记录，不是当前默认值或可重跑的当前工具合同。
G3 已移除 lambdaMax/NOTICE，改用直接 1 秒概率、指数升温和动态线性冷却恢复；
当前模型见 [fault README](../../../contrib/satcompute/fault/README.md)，
新结果见 [G3 冻结索引](../../n4c/reviews/G3-final-freeze.md)。

本目录保存 N4B 第一阶段自身状态计算故障的可复现标定证据。这里选择的是适配
SatCompute 1000 秒实验窗口的功能场景参数，不是现实卫星热常数或真实故障率。

## 方法

热模型使用 1 秒检查间隔，并比较：

- `heating_tau_s = 39 / 43 / 47`；
- `cooling_tau_s = 20 / 30 / 40 / 60`；
- `max_failure_intensity_per_s = 0.005 / 0.01 / 0.02 / 0.05`。

温度候选直接调用平台的 `SelfStateFaultModel`，而不是在外部脚本中重新实现公式。
故障强度使用 66 个节点、1000 秒、3 个热点节点的受控负载：每个热点连续忙碌
50 秒、空闲 290 秒，其他节点只执行不会进入风险区的短任务。每个强度候选使用
相同的 seed 和 30 个固定 run，节点随机流按稳定卫星 ID 分离。

## 选择结果

- `heating_tau_s=43`：10 秒任务后约 20.73 ℃，44 秒达到风险阈值，56 秒达到
  30 ℃，对应第 6 个连续的约 10 秒任务；
- `cooling_tau_s=40`：沿用当前版本的候选值，本阶段暂不采纳后续讨论的快速降温或
  100 秒过热调整；
- `recoverable_compute_duration_s=8`：这是故障后的保护停机时长，不改变 40 秒降温
  时间常数；从 30 ℃ 开始停机 8 秒后约为 27.64 ℃；
- `max_failure_intensity_per_s=0.005`：30 个 run 平均约 0.87 次 F1 故障，并保留
  平均约 8.27 个风险-only episode。

故障次数目标仅用于避免 1000 秒功能场景“几乎永不发生”或“过度频繁发生”，不构成
客观航天器失效率结论。若后续改变任务时长、热点数量、升降温时间或仿真窗口，必须
重新执行标定，不能沿用当前故障次数结论。

## 复现

在仓库根目录构建后执行：

```bash
./ns3 run "satcompute-f1-calibration \
  --outputDir=/tmp/satcompute-n4b-f1-calibration"
```

工具使用 [`fault-para.cc`](../../../contrib/satcompute/fault/fault-para.cc) 的内置参数；
不存在单独的故障模型配置 JSON。

生成：

- `n4b-f1-calibration.csv`：逐秒热模型候选以及每个 Monte Carlo run 的结果；
- `n4b-f1-calibration-summary.json`：候选汇总、分布和最终选择。

本目录中的同名文件由上述命令生成，作为阶段验收快照；正式 generate/replay 不读取
这些标定输出。
