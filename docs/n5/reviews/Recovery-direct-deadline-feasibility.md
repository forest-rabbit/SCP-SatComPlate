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
用户随后将正式实验缩减到 run11 FULL/noU/Rational-U 三组，统一使用修正版本；
其余受影响旧结果保留为历史，不重跑、不宣称已经修正。

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

本轮只完成用户收缩后的三组主场景，未推送、未开新 PR、未 CI/合并/打标签，默认 FULL 不变。
