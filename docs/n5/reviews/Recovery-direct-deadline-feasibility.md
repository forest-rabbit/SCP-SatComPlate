# 恢复准入：direct deadline 可行性修正

用户于 2026-09-14 接受专项任务书及四项修订。基线 `c1a8704cd`，独立分支
`fix/recovery-deadline-feasibility`；不改变 U/Frequency/Fault/路由/输入/任务/原 deadline。

## 合同与审计

原节点优先，但 direct 与 migration 均检查完整计算完成时间。任一 direct 路径可行就不迁移；
平局 REDO。两条都不可行时，relocate 搜索既有 stable-ID 候选，recompute 保留原兜底。
INPUT 路径不可用不再提前绕过迁移检查。仍是单次恢复，不在传输失败后重启 attempt。
恢复接口和指标说明见 [protection README](../../../contrib/satcompute/protection/README.md)。

历史审计入口 `tests/integration/regression/audit-direct-recovery-deadline.py`，结果位于
`output/audits/recovery-direct-deadline-impact-complete.json` 及逐事件 CSV。
覆盖 24 组 CompFRR（V4 七组、Gate A 十二组、Rational 五组），共 1,917 次 direct，
24 次在当时估计下已无法按时完成。加上 INPUT 路径早退，共 16 组存在受影响分支，8 组未触发。
迁移候选的历史完整同事件状态未被日志证明，24 次均标记 UNKNOWN，不冒充“没有候选”。
行为变化本身足以要求重新评价，不以是否成功救回任务作为筛选条件。
第一轮正式实验缩减到 run11 FULL/noU/Rational-U 三组；之后用户授权只补齐 U 的五轮比较，
见下文。其他基线/消融的受影响旧结果仍保留为历史，不宣称已经修正。

CB 单独审计纠正后的四组 relocate sensitivity，共 289 次 direct，零 deadline 不可行；
主 CB 与增强版运行代码均不修改，不重复这四组正式仿真。

## 验证与执行

纯 estimator 覆盖并行 join、inclusive deadline、单一路径可行、缺失路径、负预算和溢出；
运行测试覆盖空闲但超时的 Eager/Deferred 迁移成功、无目标、recompute 开关及原忙时行为。
保留两类失败原因：当前 fallback 可以在接管成功后清空，direct 原因及 relocation trigger 持久保存。

重跑入口 `tests/integration/regression/run-recovery-deadline-reruns.py`：限定主场景三种 U，
固定干净执行提交；与原命令逐项比较，仅输出目录不同。三组均在线 generate、完整 1300 s，
旧数据不覆盖。执行提交 **`bc721ed42`**，三组均返回 0，墙钟分别为
FULL 1031.135 s、noU 1007.838 s、Rational-U 1010.564 s。
结果在 `output/recovery-deadline-reruns/`，`comparison.json/csv` 为三方及修正前后比较。

验证通过：定向构建；完整项目自有 C++ 与 smoke；最终 recovery 2,745 项检查；
Python 175 项（174 通过、1 跳过）。三组 Frequency、N5C、实际 WU、物理流量及资源清理审计通过。
Rational-U 额外独立重建 23,107 个候选历史、409 个可行提案，未读取未来故障。
noU 的 28 个 CSV、Rational-U 的 29 个 CSV 全部旧字段一致（仅新增诊断列）；
其故障/清理/容量 JSON 也一致。FULL 在首个受影响故障 316 s 之前，任务、保护与选点负载事件一致。

## run11 结果

三组完整故障 trace 与各自旧运行一致，F1/F2/F3=84/2/1，83 个共同恢复事件均真实追平。

| 指标 | FULL | noU | Rational-U |
| --- | ---: | ---: | ---: |
| 完成任务 | 800/800 | 800/800 | 800/800 |
| direct / relocate / recompute | 81 / 2 / 0 | 83 / 0 / 0 | 82 / 1 / 0 |
| busy-at-fault | 1/83 | 0/83 | 1/83 |
| 平均 / P50 / P95 catch（ms） | 314.285 / 279.189 / 604.950 | 316.818 / 278.197 / 604.387 | 314.684 / 278.197 / 604.387 |
| 实际执行浪费（WU） | 634,161 | 627,361 | 634,161 |
| 常态保护（eq-WU） | 436,710 | 436,920 | 437,370 |
| 预留等待（eq-WU） | 1,974,401.132 | 2,002,229.670 | 1,977,712.366 |
| 总等效浪费（eq-WU） | 3,045,272.132 | 3,066,510.670 | 3,049,243.366 |
| 额外应用发送（GB，十进制） | 107.663198 | 107.799429 | 107.584887 |
| 全网平均链路利用率 | 0.413542% | 0.415168% | 0.416057% |
| assignment / storage HHI | 0.042557 / 0.051704 | 0.071239 / 0.050051 | 0.041290 / 0.049286 |

FULL 两次迁移均完成：140 为 `DIRECT_DEADLINE_INFEASIBLE`，455 为 `REMOTE_BUSY`。
task140 原 remote=15 空闲，但两条 direct 均不可行；按既有 stable-ID 搜索选择 recovery=0，
执行 `MIGRATE_TAIL`。远端持有合法 0 WU 状态，结合 local=11 上 242,885 WU 的合法 tail；
不是凭空增加 remote 进度。故障时 247,785 WU，实际补算 4,900 WU，catch 从 2.779013 s
降到 0.350163 s；计算在 319.379533 s 完成，早于原 deadline 320.681533 s。
旧实际超期任务因此被救回，而不是放宽 deadline。

相对修正前 FULL：完成 799→800，平均 catch −8.515%，实际执行浪费 −51.778%，
总等效浪费 −18.280%，额外应用字节 −0.0021%。额外迁移操作发送并不等于全网新增字节。
noU/Rational-U 指标保持原值。修正后 FULL 比 Rational-U 的 catch 低约 0.399 ms（0.127%），
总等效浪费低约 3,971 eq-WU（0.130%），额外应用发送高约 0.073%；不宣称显著优势。
旧 run11 中 Rational-U 相对 FULL 的主要领先不能继续归因于 U 本身；本轮不外推到未补跑的 run12–15。

以上为首轮三组主场景证据；后续五轮审计如下。

## 补齐五轮 U 比较（2026-09-14）

新增恰好 8 组：FULL run12/14；noU、Rational-U 各 run12/14/15。
复用修正后的三组 run11，以及未触发两类修正分支的 FULL run13/15、noU run13、Rational-U run13。
后四组逐次重查完整历史恢复账本；结合未触发新分支、相同 direct/migration 时间算术、
调度/所有权/RNG 未改及既有正式/fixture 等价证据保留，不只凭完成数相同判断。
特别是 noU/Rational-U run15 的 deadline 不可行计数虽为 0，仍因任务 53 的 INPUT 早退而重跑。

执行提交 `a5b00962cbbc4033814750ed97f21de71f0c22f1`，生产实现与 `bc721ed42` 一致，
仅新增测试/审计与说明。8 组均完整结束 1300 s、返回 0，墙钟各 1355.033–1385.702 s。
新结果在 `output/recovery-u-revalidation/`；`execution-plan.json` 列出全部 15 组路径和提交，
`summary.csv`、`aggregate.json`、`paired-comparison.json`、`leave-one-out.json`、
`paired-catch-differences.csv`、`per-task-comparison.csv`、`recovery-fix-impact.json` 保留逐项证据。
旧输出不覆盖；旧五轮 aggregate 已被本节替代，但未复用的旧正式运行仍只代表旧合同。

验证：180 项 Python（179 通过、1 既有跳过），定向构建无待编译项；复用修正提交的完整 C++/smoke。
15 组 WU/流量/资源释放/Frequency/N5C 审计通过，独立核对 61,394 条 Frequency 决策；
Rational-U 共 112,396 条候选历史、1,991 次可行提案与过去服务记录重建一致。
8 组在各自首个受影响故障前，任务、保护与负载事件均与旧运行一致；
8 组完整故障 trace 也均与各自旧运行一致。三方之间仍只有历史 run13 的 FULL 多一次 F1，
不是强制回放结果。任务输入、WU、速率、到达与 deadline 预算逐项一致。

### 五轮结果

每组总计 4000 个任务、409 次恢复。catch 主表使用三方共同有效的 **406** 次追平；
FULL/noU/Rational-U 实际分别有 406/407/407 次追平，缺失值不补 0。
任务完成统计包含全部任务；“已追平”不等于最终按时完成。

| 指标 | FULL | noU | Rational-U |
| --- | ---: | ---: | ---: |
| 完成任务 / compute deadline 成功 | 3996/4000 | 3998/4000 | 3997/4000 |
| run11–15 完成数 | 800/798/799/799/800 | 800/799/800/799/800 | 800/798/800/799/800 |
| busy-at-fault | 5/409 | 5/409 | 9/409 |
| direct / relocate / recompute | 398 / 7 / 4 | 397 / 10 / 2 | 393 / 13 / 3 |
| 共同 catch 均值 / P50 / P95（ms） | 338.266 / 283.187 / 833.200 | 341.329 / 285.447 / 864.741 | 353.387 / 286.050 / 866.252 |
| 实际执行浪费（WU） | 6,713,764 | 5,498,770 | 6,589,411 |
| 常态保护 / 预留等待（eq-WU） | 2,142,030 / 10,051,569.717 | 2,147,300 / 10,088,864.050 | 2,140,630 / 10,077,699.518 |
| active 等效开销 / total 等效浪费（eq-WU） | 8,855,794 / 18,907,363.717 | 7,646,070 / 17,734,934.050 | 8,730,041 / 18,807,740.518 |
| 额外应用实际发送（GB） | 527.354 | 530.071 | 528.218 |
| 迁移操作实际发送（GB） | 2.248 | 1.275 | 3.751 |
| 全网平均链路利用率 | 0.414549% | 0.416177% | 0.416672% |
| assignment HHI / storage HHI | 0.040789 / 0.050996 | 0.067528 / 0.048417 | 0.040342 / 0.047457 |
| assignment Top1 / Top5 | 7.885% / 33.202% | 12.306% / 47.736% | 8.432% / 34.105% |
| assignment Gini / storage Top1 | 0.634465 / 11.029% | 0.763476 / 10.244% | 0.618257 / 10.133% |

HHI、占比、利用率是五轮等观测期的逐轮均值，不是合并事件后的 HHI。
单星 active backup 峰值为 3/1/1，三组单星物理存储峰值均 966,794,849 B。
GB 为十进制；网络不折算进 eq-WU。迁移操作流量含 INPUT，不等于相对 direct 的净新增流量。

### 两类修复的实际影响

迁移尝试 FULL/noU/Rational-U 共 11/12/16 次；触发原因分别为
busy 5/5/9、direct deadline 6/5/5、INPUT 路径 0/2/2。
其中成功迁移 7/10/13 次，其余按既有合同转重算，不能把尝试数当成功数。

- run15 任务 53：两组原 remote=41 空闲；旧 INPUT 早退后在节点 0 从零重算并超期。
  新版从 41 向 0 执行 `MIGRATE_TAIL`，追平 3.128959 s → 0.329249 s，
  计算于 67.695179 s 完成，早于原 deadline 68.929710 s。改变的是状态复用，不是放宽 deadline。
- run12 任务 351：FULL 从 0 迁到 6（deadline 触发），noU/Rational-U 从 10 迁到 5
  （INPUT 触发），三组均从失败改为完成。run14 的任务 334，以及 noU 的 265、
  noU/Rational-U 的 791 也被救回。
- 并非每次迁移搜索都成功：run12 的 FULL/Rational-U 任务 119、三组任务 573，
  run14 三组任务 573，搜索未找到可在原 deadline 内恢复完成的候选，转重算后仍失败。
  FULL run13 的任务 427 是旧有 busy/恢复失败，未触发本次两类修正。
- 修复可反馈到后续选点：Rational-U run12 任务 559 的指定 remote 从旧 18 变为 0，
  故障时 0 忙，迁到 63；这增加了一次 busy，不能沿用旧汇总的 8 次。

### 长尾与判定

三对方案均提供逐轮及合并的正/负最大单任务排除；只从双方统计移除一个 `(run,task)`，
不重跑、不伪造新轨迹，不重新计算链路/HHI。catch 排除与主表使用同一三方共同样本。

Rational-U 相对 FULL 多完成 1 个任务，总等效浪费低 0.527%、实际执行浪费低 1.852%；
但共同 catch 慢 4.470%、额外应用字节高 0.164%，busy 为 9 对 5。
其最大浪费正贡献是 run13 task427（711,851.633 eq-WU）；移除该实例后，
Rational-U 反而多 612,228.434 eq-WU。移除最大负贡献 run15 task182 后，
Rational-U 仍慢 10.027 ms；移除 catch 最大正贡献 run11 task592 后，仍慢 15.816 ms。

noU 相对 FULL 多完成 2 个任务，总等效浪费低 6.201%，但 catch 慢 0.906%、
额外应用字节高 0.515%，assignment HHI 更集中。移除最大正贡献 run12 task119 后，
仍少 184,370.544 eq-WU，但慢 12.938 ms；移除最大负贡献 run15 task182 后，catch 方向反转。

**建议保留默认 FULL，并停止本轮 U 公式探索，等待用户确认。** 这不是累计 U 全面最优的证明：
可靠性与资源指标存在取舍，五轮仍不足以宣称统计显著性；Rational-U 没有稳定综合替代优势。
U 应解释为长期历史计算压力，不是未来忙碌的准确预测器。noU 仍仅为消融。
本轮未改生产代码/参数、未推送/合并 PR #100/#101、未 CI/tag，也未开始架构清理或 INPUT 下一阶段。
