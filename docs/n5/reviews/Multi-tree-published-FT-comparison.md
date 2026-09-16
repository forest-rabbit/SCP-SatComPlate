# Multi-tree 与六方案整体实验结果

2026-09-15。本轮实现和实验已完成，等待人工审阅；尚未推送、建立 PR、运行阶段 CI 或合并。
未调整算法以改善实验结果，生产 para.cc 默认值不变。

## 范围与验收

- 10 Gbps：Run A/B/C = randomRun 11/12/13，各六方案，共 18 次完整执行。
- 1 Gbps：仅 Run A，六方案全部完成。
- 100 Gbps：仅 Run A，完成五方案；CB-SAT 按用户要求停止，保留约 946 s 的部分记录，不补跑、不计入结果。
- 共 **29 次完整 1300 s 仿真、1 次取消**。全部完成项通过原始账本审计，不能称 100 Gbps 六组矩阵完整通过。
- 固定 randomSeed=1、ecmpHashSeed=1、66 星、800 任务、352513119 WU、1 ms fixed delay。
  三轮只改 randomRun，带宽对照只改 islBandwidthBps；任务、故障配置、FT tree、标定和 CompFRR 参数冻结。

四个 baseline 均 FA-FFP；CB-SAT 的 busy policy 为 Recompute。
1+1 不隐含 Recompute fallback；Multi-tree 按公开树一次选择 RS/RP，RP 拒绝不转 RS。
我们的正式配置为 CompFRR adaptive + CompFRR-P CUMULATIVE/no ablation + Selective + Relocate；
FA-FFP 对照只替换 CompFRR 的 placement，其余保持相同。

Stage A：200 项映射检查、校准重建一致、18 文件 passive 等价；
OFF 映射检查为 584 RS / 216 RP，全部八个叶分支可达。
Stage B：七类真实网络 runtime fixture 共 517 项检查，129 项配置检查及六组平台 smoke 通过。
同纳秒故障批次派发边界经用户单独批准修复；修复后旧 11 组的 1984 文件 / 1723 CSV 严格等价。
阶段构建及维护的 C++、Python、smoke、regression 均通过。最终仅汇总工具变化：
9 项专项测试、242 项 Python 测试通过（1 项 skip），不重复构建未变的 C++ 或运行额外 CI。

## 统计口径

GB 为十进制实际应用载荷；每条物理流计一次，不按经过的跳数重复计量。
额外流量包含常态保护和恢复的整个流生命周期，含故障后的 proactive 后缀。
catch 指从 primary 计算中故障到幸存/恢复任务实际追平故障前 WU；不是直接用接管时间。
未追平为缺失，不填零；已追平也可能最终 deadline 失败。因此必须同时报告样本数与完成数。
总浪费 = 实际执行浪费 WU + 常态保护等效成本 + 实际预留空等等效成本；
失败任务的实际执行不算有效产出，预留空等不冒充 CPU 执行。
逐次明细及资源分账见 [29 次执行 CSV](Multi-tree-published-FT-comparison.csv)。

## 10 Gbps 三轮结果

三轮固定 randomSeed=1、ecmpHashSeed=1，仅 randomRun=11/12/13。每轮均 800 任务、1300s。

| 方案 | A/B/C 完成 | 合计完成 | 平均每轮额外 GB | pooled catch 均值 / P90 ms | 已 catch / 样本 | 平均每轮总浪费 M eq-WU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Recompute / FA-FFP | 742/739/749 | 2230/2400 | 5.797623 | 1163.739 / 2291.265 | 75/245 | 23.790006 |
| 1+1 / FA-FFP | 800/800/799 | 2399/2400 | 192.424972 | 240.717 / 426.121 | 244/245 | 337.219619 |
| CB-SAT / FA-FFP / Recompute | 795/796/795 | 2386/2400 | 299.978125 | 293.076 / 479.835 | 234/245 | 6.494433 |
| Multi-tree (Published FT Rule) / FA-FFP | 766/765/773 | 2304/2400 | 55.108600 | 437.834 / 1106.883 | 150/246 | 92.746231 |
| CompFRR / CompFRR-P / Selective / Relocate | 800/800/800 | 2400/2400 | 111.998470 | 178.877 / 429.040 | 245/245 | 1.897999 |
| CompFRR / FA-FFP / Selective / Relocate | 800/800/800 | 2400/2400 | 111.573396 | 178.233 / 411.251 | 245/245 | 1.890054 |

catch 均值只包含真实追平样本；未追平不填零，必须与完成数一起看。
总浪费含实际执行浪费、常态等效成本、实际预留空等等效成本，不等于纯 CPU 消耗。

### 相同故障且双方均追平的配对比较

| 对照方案 | 共同故障 / 双方追平 | 对照 catch ms | CompFRR-P catch ms | CompFRR-P 降低 |
| --- | ---: | ---: | ---: | ---: |
| Recompute / FA-FFP | 241 / 73 | 1180.305 | 234.428 | 80.138% |
| 1+1 / FA-FFP | 245 / 244 | 240.717 | 179.456 | 25.449% |
| CB-SAT / FA-FFP / Recompute | 245 / 234 | 293.076 | 173.567 | 40.777% |
| Multi-tree (Published FT Rule) / FA-FFP | 241 / 148 | 436.195 | 203.695 | 53.302% |
| CompFRR / FA-FFP / Selective / Relocate | 245 / 245 | 178.233 | 178.877 | -0.362% |

各对照的配对集合不同，不能拿此表不同列集合的均值直接相互排名。
三轮仅描述该固定场景，不声称统计显著性。各轮、资源分项、流量和链路利用率见 JSON。

## run11 带宽对照

每组均 800 任务；单元格为“完成数；catch 均值 ms [已追平/样本]”。未追平不填零。

| 方案 | 1 Gbps | 10 Gbps | 100 Gbps |
| --- | ---: | ---: | ---: |
| Recompute / FA-FFP | 736; 874.850 [17/80] | 742; 1087.775 [25/83] | 737; 706.119 [22/85] |
| 1+1 / FA-FFP | 793; 0.515 [76/82] | 800; 229.591 [83/83] | 800; 28.195 [85/85] |
| CB-SAT / FA-FFP / Recompute | 743; 1830.161 [51/81] | 795; 260.543 [79/83] | 已取消（不计入） |
| Multi-tree (Published FT Rule) / FA-FFP | 763; 193.962 [46/82] | 766; 439.410 [49/83] | 763; 192.041 [48/85] |
| CompFRR / CompFRR-P / Selective / Relocate | 730; 2410.764 [34/80] | 800; 175.073 [83/83] | 800; 82.024 [85/85] |
| CompFRR / FA-FFP / Selective / Relocate | 787; 438.421 [72/80] | 800; 173.068 [83/83] | 800; 82.515 [85/85] |

单元格为“额外实际流量 GB；总浪费 M eq-WU”。不将少完成任务造成的少发流量视作优化。

| 方案 | 1 Gbps | 10 Gbps | 100 Gbps |
| --- | ---: | ---: | ---: |
| Recompute / FA-FFP | 0.000001; 26.715240 | 6.371650; 24.392504 | 7.167184; 24.537690 |
| 1+1 / FA-FFP | 70.505879; 323.119548 | 192.208802; 337.297072 | 194.977877; 328.260176 |
| CB-SAT / FA-FFP / Recompute | 290.922673; 35.246113 | 300.984885; 6.340062 | 已取消（不计入） |
| Multi-tree (Published FT Rule) / FA-FFP | 17.228248; 96.695599 | 54.589650; 94.031228 | 55.282071; 91.826588 |
| CompFRR / CompFRR-P / Selective / Relocate | 46.458523; 48.938779 | 113.321868; 1.896998 | 97.192825; 1.082422 |
| CompFRR / FA-FFP / Selective / Relocate | 89.154119; 8.803625 | 112.197401; 1.876345 | 96.324498; 1.086606 |

带宽对照仅改变链路容量；三组均 seed1/run11，不重新拟合树或 MTBF，不调整 deadline。
各带宽的实际故障集合可能不同。组内配对和全部已完成执行分项见 JSON / cases.csv；取消项不补零。

## 结论与失效边界

**10 Gbps：CompFRR 的整体保护方案有效，但自有 placement 不全面优于 FA-FFP。**
以每轮平均实际额外流量和总 eq-WU 浪费计，自有方案相较 1+1 分别降低 41.80% / 99.44%，
相较 CB-SAT 降低 62.66% / 70.77%。相同故障且双方实际追平的子集上，catch 分别降低
25.45% / 40.78%；配对子集不同，不是全任务无偏反事实。

相较 Multi-tree，总等效浪费下降 97.95%，但额外流量增加 103.23%，完成数由 2304 提高到 2400。
相较 Recompute 也需要更多常态网络资源；不能忽略 baseline 的未完成任务而只比平均时延。
两种 CompFRR 都完成 2400/2400；自有 placement 的 catch、额外流量、总等效浪费较 FA-FFP
分别高约 0.36% / 0.38% / 0.42%，但全网平均链路利用率低 3.44%，
“每轮最高单链路全程平均利用率”的三轮均值低 26.57%。没有全面占优或统计显著性的结论。

10 Gbps 的 1+1 在 Run C 失败 task 12：primary 8 与 replica 1 同在 971 s F1 批次失效，
正确触发 NO_SURVIVING_ATTEMPT。CB-SAT 三轮共失败 14 个任务；不能统称 remote busy，
其中 Run B task 351 在最初 INPUT/检查点未就绪时故障。部分任务追平后仍超 deadline。
Multi-tree 的 Run A/B/C 实际 RS/RP 为 583/217、582/218、595/205；全部 RP 申请准入，
没有重新标定树。差异来自真实运行轨迹，不要求等于 Stage A 的 passive 分布。

**1 Gbps：自有方案暴露了 reference-pair 准入限制，不能仅归因为低带宽或 U 排名。**

- CompFRR-P 的 69 个计算中失败任务都在 OFF 遭遇故障；全部先前被 DEADLINE_INFEASIBLE 拒绝。
  首次检查时，单独的完整 INPUT 传输估计就超过 deadline 恢复余量。
  任务 5 为 103067352 Byte INPUT、1.54602 s 纯计算、0.463806 s 余量，
  在 1 Gbps 下 INPUT 估计 0.824539 s。另 11 个 ON 故障全部恢复成功。
- 自有方案只在一个 reference pair 上求一次 Frequency；失败即返回，尚未进入实际 remote 评分。
  FA-FFP 继续寻找首个 frequency-hard-feasible pair。因此前者只启动 69 个保护，后者启动 373 个；
  FA-FFP 的 316 个已准入 remote 正是 INPUT source，可合法 LocalDelivery。
- 同一 task 53 的首次 START，前者在 local31/remote0 检查一次后拒绝；后者检查 40 对，
  在 local31/remote43（source43）准入。两组该任务的实际 primary 故障相同。
  P 失败而 FA 完成的 58 个任务，都在 FA 中使用 source=remote；其中 57 个具有相同 primary 故障身份。
  反向存在 task 302：FA 失败而 P 完成，不能隐去。
- Selective 仍按 Deferred 的硬约束批准 START，之后才判断主动 INPUT；这限制了预取挽救准入的可能性。
  上述差距发生在最终评分之前，不等于已证明 U 公式错误，也没有证明修改顺序必然救回所有任务。
  一次 Frequency、reference 可行性和 INPUT 准入的衔接需另行审计，本轮不修改冻结合同。
- 1+1 获准 765/799 次申请，34 次拒绝；其中 686 个副本使用同星 INPUT，解释了低网络等待。
  拒绝副本不是立即终结 primary。CB-SAT 的 51 次 Recompute 均超 deadline：
  31 次 STATE_MISSING、13 次 COMPLETE_INPUT_MISSING、7 次 REMOTE_BUSY；另 5 次 DIRECT 超时。
- task 120 在所有 1 Gbps 组均于初始 INPUT 传输期间遭遇固定 F3，尚未 RUNNING；
  因而 Multi-tree 为 UNDECIDED，1+1 未申请副本，不能将它纳入计算中 catch 样本。
  不移动 F3 时刻以制造更好的完成率。
- Recompute 额外网络仅 1352 Byte：63 次运行中恢复被准入拒绝；17 次成功中 15 次本地 INPUT，
  2 次小输入网络传输。少发流量不是总体性能优势。

100 Gbps 已完成五组仅作带宽敏感性观察。两种 CompFRR 与 1+1 均 800/800；
1+1 catch 更短但重复计算代价高。CB-SAT 缺失，不能声称该带宽下完成了六方案比较。

## 证据与复核

| 执行集合 | clean execution commit | 原始输出 |
| --- | --- | --- |
| 10 Gbps Run A | `1fd8348bd` | `output/multitree/stage-c/run11-1fd8348bd/` |
| 10 Gbps Run B/C | `363c2092f` | `output/multitree/stage-c/run-B-run12/`、`run-C-run13/` |
| 1 / 100 Gbps Run A | `ebd8a4748` | `output/multitree/bandwidth/{1Gbps,100Gbps}-run11/` |

执行提交之间仅文档和测试/审计工具变化；仿真 C++、输入、模型与标定没有变化。
最终原始记录复核与完整分账见
[comparison.json](../../../output/multitree/comparison-final/comparison.json)；
100 Gbps 取消凭据保留于该执行根目录的 cancellation.json，原始部分记录不删除。

在项目虚拟环境中运行下面的命令可只读重建汇总；目标必须使用新目录，不启动仿真：

```bash
python contrib/satcompute/tests/integration/regression/summarize-multitree-rounds.py \
  --run-a output/multitree/stage-c/run11-1fd8348bd \
  --run-b output/multitree/stage-c/run-B-run12 \
  --run-c output/multitree/stage-c/run-C-run13 \
  --bandwidth-1gbps output/multitree/bandwidth/1Gbps-run11 \
  --bandwidth-100gbps output/multitree/bandwidth/100Gbps-run11 \
  --output-root output/multitree/comparison-recheck
```
