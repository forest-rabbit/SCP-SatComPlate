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
旧数据不覆盖。正式重跑及最终比较尚待完成，不据此宣称任务 140 或其他任务必然恢复成功。
