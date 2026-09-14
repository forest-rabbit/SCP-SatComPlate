# N5R：历史 V7 run11 离线诊断

## 结论与范围

V7 的主要代价来自**对最终未故障任务的预取**，不是预取流失败或大量传到错误节点。
主要覆盖缺口是故障发生时仍然 ABSENT；本轮没有 IN_FLIGHT-at-fault 样本，不能据此评价原流续传的普遍效果。
成功预取有恢复收益，但集中在少数任务，不能把 used/non-critical 误读为无收益。

只读取 `output/v7-cbsat-adjustment/20260913-jit-formal/` 下 `CompFRR-JIT-V7` 与
`reference-R7-deferred-relocate`。两者都在干净执行提交 `367f23f393c45205cf87fbee003bfb668f453d5e`
完成 seed 1 / run 11 / 800 tasks / 1300 s。工作负载、故障配置、FA-LRL、relocate、deadline 等非策略输入一致；
V7 含历史 JIT START benefit，Deferred 不含，故并非只改变发送时机的消融。
它们早于 corrected recovery/maintenance/N5R；本报告不认证当前平台性能，也不比较新 N5R 与旧 Deferred。

本轮没有启动仿真、引入 production JIT、调整公式/参数或设计下一版模型。

## 字节守恒

口径为实际发送的应用层 payload，十进制 GB；不是 declared bytes、链路字节跳数或带宽利用率。
V7 总额外流量为 **193.255484178 GB**，其中 prefetch 为 **89.096214542 GB**；
其余为 checkpoint 83.212331127 GB、故障恢复 20.946938509 GB。

| 互斥类别 | 生命周期数 | 实际 bytes | GB | 占 prefetch |
| --- | ---: | ---: | ---: | ---: |
| No fault | 330 | 84,338,660,904 | 84.338661 | 94.6602% |
| Wrong target | 1 | 489,034,056 | 0.489034 | 0.5489% |
| Used + critical | 0 | 0 | 0 | 0% |
| Used + non-critical | 16 | 4,268,519,582 | 4.268520 | 4.7909% |
| Residual failed/cancelled | 0 | 0 | 0 | 0% |
| **TOTAL** | **347** | **89,096,214,542** | **89.096215** | **100%** |

优先级为 USED → NO_FAULT → WRONG_TARGET → 其余 FAILED/CANCELLED；失败等标志独立保留，
这是互斥记账而非互斥因果。347 个生命周期包含 5 个 LocalDelivery，其网络字节为零，未创建伪 UDP。
342 条网络预取流全部完成；24 次 NO_ADMISSIBLE_PATH 是尚未准入的评估，不是传输失败，不能记作失败流量。

全生命周期 used + unused = 4,268,519,582 + 84,827,694,960 = 89,096,214,542 bytes。
故障前/后账本分别为 89,096,214,542 / 0 bytes；无故障生命周期沿用原账本的 before 桶。
当前样本无故障后续传；合成测试另外验证了 IN_FLIGHT 中 before + after 与 used/unused 的相同生命周期范围。
以上总数与原独立 V7 审计完全一致，且重新逐对象/物理流核对，不只引用旧汇总。

## 全部故障任务，而不只是有预取的任务

`critical wait = max(0, recovery INPUT dependency ready - state ready)`。
READY 预取对象的物理接收时间通常早于 fault，恢复依赖接纳时间则为 recovery acceptance；两者分列。
均值/P95 在该状态下全部已知依赖 join 的任务中计算，包含零等待；P95 使用线性插值。

| 故障时 INPUT 状态 | 任务数 | 故障前预取 bytes | 实际使用数 | critical 数 | critical wait 均值 / P95（ms） |
| --- | ---: | ---: | ---: | ---: | ---: |
| READY | 17 | 4,757,553,638 | 16 | 1 | 22.838 / 77.648 |
| IN_FLIGHT | 0 | 0 | 0 | 0 | — / — |
| ABSENT | 66 | 0 | 0 | 55 | 218.829 / 464.329 |

全部 83 个依赖 join 可核对，无 unknown。READY 中的 1 个 critical 是 **task 114**：
预取到 node 0，但其故障时忙，实际 MIGRATE_TAIL 到 node 6；新 INPUT 带来 388.242327 ms 的额外依赖等待。
这是“原 remote 上 READY”，不是“最终 recovery node 上 READY”。

16 个实际使用的 INPUT 均已提前 READY，恢复时不再额外等待它；这恰好可能体现提前发送的价值。
ABSENT 的 66 个任务最后一次故障前 JIT 评估均为 WAIT_NEXT_KNOWN_EVENT；其中 55 个恢复确实受 INPUT 限制，
合计 14.442724814 s，占全部 INPUT critical wait 的 97.3822%。另外 11 个虽 ABSENT，INPUT 没有延迟最终依赖 join。
这不能直接推导“更早发送一定更优”，也不构造新阈值或调度器。

## 与同批 Deferred 严格配对

按 task、fault timestamp/type、实际 recovery target/path 与原 deadline 逐项核对，
**83/83** 可配对，0 个拒绝；两组均完成 800/800。

| 指标 | 同批 Deferred | V7 | V7 相对变化 |
| --- | ---: | ---: | ---: |
| 实际 catch 平均值 | 315.176 ms | 274.939 ms | −40.238 ms（−12.7667%） |
| 总额外应用层流量 | 107.724771206 GB | 193.255484178 GB | +85.530712972 GB（+79.3974%） |

逐任务为 15 个 catch 改善、67 个完全相同、1 个变差；配对差值中位数为 0。
16 个实际使用任务的平均配对改善为 209.468 ms，其中 15 个改善、1 个相同。
非使用任务中 task 103 变慢 11.762370 ms：该值保留为实际轨迹差异，不无证据归因于 INPUT 拥塞。
因此“平均更快”不是普遍每任务改善，更不是从单 trace 重建的精确无预取反事实。

额外流量增加并不恰好等于 unused prefetch：V7 还改变历史 START/维护轨迹。
账本差为 prefetch +89.096214542 GB、新 recovery INPUT −4.268519582 GB、
其他 checkpoint/recovery 流量净增 0.703018012 GB，总计 +85.530712972 GB。

## 五类因素的证据排序

不把不同量纲强凑成单一因果分数；以下分别给出成本/覆盖的排序依据。

1. **No-fault over-prefetch：首要已观察成本。** 330/347 生命周期，84.339 GB，占预取 94.66%。
   这是事后未使用，不意味着预测器当时应当知道任务不会故障。
2. **时机/覆盖缺口：首要剩余等待来源。** 66/83 为 ABSENT，55 个 INPUT-critical，合计 14.443 s；
   无 IN_FLIGHT，不能声称大量流“发晚了传不完”。最后一次评估为等待下一事件。
3. **Wrong target：次要已观察损失。** 仅 task 114，0.489 GB、0.388 s critical wait。
4. **Non-critical INPUT：是状态解释，不是负收益排名。** 16 个 used/non-critical，4.269 GB；
   其中 15 个有配对 catch 改善。不能把这部分与 no-fault 一同算作 useless bytes。
5. **Flow failure/cancellation：本轮无实际贡献。** 342 个网络流全部完成，失败/取消流量为零；
   24 次准入拒绝独立统计，后续合法事件可重试，不是自动重传已失败流。

## 复核与停止点

实现位于 `tests/support/protection/jit_offline_audit.py`，薄入口与命令见
[测试 README](../../../contrib/satcompute/tests/README.md#历史-v7-离线诊断)。复用既有 INPUT/baseline helper，
没有复制 solver、生产模型或通用账本。新离线单元测试覆盖严格 READY、IN_FLIGHT 全生命周期、
实际消费与 adoption 区分、LocalDelivery、守恒/身份损坏、缺失 join、配对拒绝与输出不可覆盖。
共新增 14 项合成测试；维护 Python 213 项（1 项外部 position-slices 缺失跳过）、C++ 合同、
build no-op 与 1965 文件小型等价复核均通过，唯一差异仍是已批准的两处 CB profile 路径元数据。

本地完整产物：`output/audits/n5r-v7-run11-offline/`（五份 CSV + 完整 JSON，ignored；不覆盖原证据）。
JSON 中保留源执行信息；每个生命周期含触发事件、p_on、T_I、剩余计算时间、发送比例、对象/流身份与恢复依赖。
模型代表故障时刻保留原小数文本；实际事件仍为整数 ns。

目录收口、等价门禁与提交见 [N5R 实施记录](N5R-implementation.md)。本轮止于离线证据，
等待人工审阅；不启用 JIT、不新增 optimizer/budget/阈值/timer，不继续 INPUT timing 第二阶段。
