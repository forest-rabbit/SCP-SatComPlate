# CompFRR-P candidate coverage：Stage 2A

## 基线与范围

Stage 1 已人工通过。从 `ebd8a4748893d538c85f0602ab207b74971c93ac` 按指定 detach / switch 创建
`fix/compfrr-p-candidate-coverage`。操作前 HEAD 与审计执行版本一致，没有未提交的生产代码或场景变更；
上一阶段文档、审计脚本及测试的未提交内容原样保留，未做 stash、rebase、cherry-pick 或覆盖。
Stage 1 六份产物复现一致，两组原始账本通过；旧实验均不改写。

本阶段只修 OFF 的固定 local 候选覆盖：

1. 原始节点/路径初筛、确定性顺序和首个 reference local 不变。
2. reference remote 被现有 `DEADLINE_INFEASIBLE` / `STORAGE_INFEASIBLE` /
   `INITIALIZATION_TOO_LATE` 硬拒绝时，继续同 local 的后续 remote。
3. 找到首个硬可行 anchor 后停止搜索；即使收益不足也不继续挑选其他 remote。
4. 如果 START 成立，冻结该 anchor 的 `(δ,n)`，进入原有 P feasibility/ranking 和完整故障批次后重验。

没有新增 local 搜索、1 Gbps 特判、目标函数、阈值或 RNG。Selective 顺序/SER、INPUT 发送源、
Frequency 数学实现/搜索空间、P 评分、ON、恢复、故障、场景及其他 baseline 保持不变。
一次 reference 求解的旧限制仅在硬拒绝时被本次明确授权的 remote 前缀搜索替代，不是全 pair 联合优化。

## 审计与语义

新增 `compfrr-candidate-coverage.csv`，原指标文件 schema 不变，非 P 不生成此文件。
按 task/time/trigger 记录 original reference local/remote、拒绝原因、固定 local 候选数量、
实际检查数、首个可行 anchor index/remote、最终 P remote、全体不可行以及 fault-hit/commit。
`n5c-placement-decisions.csv` 的 reference 此时是可行 anchor；原始 reference 在新表中另列。

index 从 1 起算；fallback depth 为 reference 之后额外检查的 remote 数（checked−1）。
固定 local 指每次 OFF 决策内保持原 reference local；不跨不同时间的独立重评估强行固定尚未准入的节点。
主汇总覆盖全部实际 fallback，包括最终全体不可行的事件；成功 anchor 的深度另外统计。
事件数与独立任务数分别给出；hard-feasible、START proposal、真实 committed START、故障命中不是同一个量。
已硬可行但无收益，或者原 P 排名/批次后重验拒绝，仍可保持 OFF，不放松这些原有合同。
附加的 `anchor_delta_permille/anchor_n` 透传 solver 的 proposed config；只有 anchor index 非空才表示
存在硬可行 anchor，不能把初始化过晚时遗留的 proposal config 当作可执行 anchor。

## 小场景门禁

证据：[small-gates.json](../../../output/compfrr/candidate-coverage-stage2a/small-gates.json)。

- 项目定向 build 通过；仅有原有 `__int128` pedantic 警告。
- Python 测试集 260 项：1 项既有跳过，其余通过。
- Frequency runtime 12,187 检查通过；新增 8 个 coverage 场景：首对成功、deadline fallback、storage fallback、
  全部 deadline 拒绝、全部 storage 拒绝、其他 local 可行但禁止更换、收益不足不 fallback、fallback 同批次故障不准入。
- 改动前后旧 fixture 的 **582 份文件逐字节一致**；只新增独立 coverage 文件及新场景。
- 原 Frequency policy 215,029 检查、P ranking 8,574 不变量、10 组/14 提案的旧空间审计通过。
- Recompute 395、1+1 855、Multi-tree 517、CB-SAT recovery 5,099 检查通过；CB 26 场景账本及 13 种损坏拒绝通过。

## 单组 development run

入口：[run-candidate-coverage-development.py](../../../contrib/satcompute/tests/integration/regression/run-candidate-coverage-development.py)。
仅运行 **1 Gbps / seed1 / randomRun11 / 800 tasks / 1300 s / CompFRR-P CUMULATIVE / Selective / Relocate**。
逐参数对照旧 P 执行，仅替换输出目录及其 fault-trace 路径，不修改 production para 默认值。

本轮未获单独提交/推送指令，因此如实记录 `worktree_dirty=true`、基线 commit 和完整 tracked SatCompute 补丁
`source-diff.patch`；不是 clean-commit 正式性能结果。原正式审计默认仍拒绝 dirty execution，只有显式
development 审计入口且具备补丁证据时才允许复用同一 actual-ledger 检查。

输出：[candidate-coverage-stage2a](../../../output/compfrr/candidate-coverage-stage2a/)。
本轮已完整运行 **1300 s**，退出码 0，墙钟 **1139.33 s（约 19 分钟）**，actual-ledger 与 coverage 审计 PASS。
结果：[development-comparison.json](../../../output/compfrr/candidate-coverage-stage2a/development-comparison.json)。

| 指标 | pre-fix | Stage 2A development |
|---|---:|---:|
| 完成任务 | 730/800（91.25%） | 785/800（98.125%） |
| committed START 任务 | 69 | 367 |
| fault→catch mean / P50 / P90，ms | 2410.76 / 2541.23 / 3972.44 | 524.64 / 161.71 / 1447.75 |
| 有观测 catch / 主计算故障任务 | 34/80 | 72/80 |
| FT 额外发送，GB | 46.458523 | 88.209594 |
| 其中 proactive 全生命周期 / recovery，GB | 23.914789 / 22.543733 | 83.857121 / 4.352474 |
| 常态保护，百万 eq-WU | 0.123970 | 0.484260 |
| 恢复保留空闲，百万 eq-WU | 18.082830 | 3.534325 |
| 任务执行浪费，百万 WU | 30.731979 | 5.372000 |
| total equivalent waste，百万 eq-WU | 48.938779 | 9.390585 |
| 平均全程链路利用率 | 3.8064% | 4.1349% |
| 最忙单链路全程平均利用率 | 12.5428% | 11.7932% |

完成数净增 **55**（+6.875 个百分点），等效浪费下降 **80.81%**；FT 流量增加 **41.751072 GB（89.87%）**。
不能将更大的保护覆盖包装成无流量代价的提升。GB 使用十进制；FT 按实际发送字节统计完整 flow 生命周期，
不是只计故障前片段；最忙链路值不是瞬时或 1 s 窗口峰值。

全体 observed-catch 均值下降 78.24%，但样本从 34 增至 72，不能视为同任务配对改善。
严格限定两侧故障签名相同且都有真实 catch 的 **33 个任务**，mean 为 **2372.65→794.58 ms（下降 66.51%）**。
未观测到 catch 的任务是 missing，不是 0；部分任务实际 catch 后仍未满足完整计算 deadline。

### Candidate coverage 机制证据

| 审计量 | 结果 |
|---|---:|
| OFF candidate-search 事件 / 独立任务 | 2462 / 799 |
| first-reference hard-feasible 事件（含无收益） | 225 |
| fallback 事件 / 独立任务 | 2235 / 735 |
| fallback depth P50 / P90 / max | 38 / 58 / 62 |
| 成功 anchor 的 fallback depth P50 / P90 / max | 27 / 50 / 62 |
| reference 失败但 alternate 硬可行：事件 / 任务 | 1590 / 656 |
| 经 fallback 真正 committed START 的任务 | 298 |
| first-reference 路径 committed START 的任务 | 69 |
| all-remotes-infeasible 事件 | 647 |

647 中 **645** 是固定 local 下实际遍历耗尽（620 deadline、25 initialization-too-late），
另 **2** 次节点/路径初筛后集合为空。不是 647 个失败任务。同一个任务可在多个合法事件反复重评估。
原 reference 硬拒绝计数为 deadline 2207、initialization-too-late 28；storage fallback 由小场景门禁覆盖。
1590 次 alternate 可行不能全部算 START：仍沿用原收益判断和同纳秒故障批次后重验。

P ranking 没有改写。新运行的 367 个 committed START 中，**54** 个最终 remote 与 anchor 不同，
表明 anchor 不是直接指定最终节点。例如 task 79 保持 local 8、anchor remote 0、原配置 `(100‰,9)`，
再由原 ranking 选择 remote 10。旧 first-reference-success 小场景的 582 份文件逐字节一致；
新运行中原先 69 个 START 任务也都仍有 START，但全程资源状态会因新增保护自然发生变化。

### Remaining OFF / deadline 原因

共 **15** 个未完成任务，逐任务原始证据见本轮的 `compfrr-candidate-coverage.csv`、
`frequency-decisions.csv`、`recovery-summary.csv`、`fault-task-impact.csv`。

| 分类 | 任务 | 证据与原因 |
|---|---|---|
| 故障前 OFF，固定 local 全 remote 硬 deadline 不可行 | 5、12、13、193、244、300、302、304、513、584、766 | 每次重评估都 checked=total，不能再归因于只查 reference；无 checkpoint，重计算最终也超时 |
| 找到 alternate，但同批次已命中故障 | 334 | 991.972428154 s 初次检查 52/52 均拒绝；992 s 的第 10 个 remote（12）可行，但 `CURRENT_FAULT_HIT`，不得补发 START |
| 已 ON，故障时直接恢复与迁移均不满足 deadline | 259、548 | `DIRECT_DEADLINE_INFEASIBLE` → relocation `CHECKPOINT_DEADLINE_INFEASIBLE` → 重计算仍超时；不是缺少 candidate search |
| 尚未进入 RUNNING 就发生 F3 | 120 | 初始 INPUT 阶段受整星故障影响，无 START 决策；不属于本轮 candidate coverage 修复范围 |

全场景 433 个没有 START 的任务不等于 433 个失败任务。按它们的**最后一次 proposal**统计：
145 个 `NO_FAULT_AFTER_INIT_READY`、260 个 `DEADLINE_INFEASIBLE`、25 个 `INITIALIZATION_TOO_LATE`、
1 个 `NO_CAPACITY_NOW`、1 个 `START_BENEFICIAL` 但故障批次 veto（334）；另 120 无决策。
此处 proposal 原因和最终 task outcome 分开统计，不将正常完成的未保护任务算作修复失败。

### 非单调任务差异与停止点

**56 个旧失败任务变为完成，1 个旧完成任务（302）变为失败，净增 55。**
本轮物理故障事件的 time/node/type/source/duration 序列与旧运行相同，但任务的执行轨迹不必相同：
105 s、节点 42 的故障，旧运行中打断 task 8，task 302 还在排队；新运行中打断 task 302。
302 的 compute start 从旧 **108.111232314 s** 提前至 **103.605941493 s**，从而暴露于该故障；
新运行的 fixed-local 三次检查分别遍历 51/51、54/54、55/55 remotes，全部 deadline 不可行。
两侧共同的 79 个主故障任务签名一致；旧独有 task 8，新独有 task 302。相同 RNG 配置不保证所有在线运行
都得到相同故障事件，更不保证命中相同任务；本轮物理事件一致是事后核对结果，不是算法假设。

旧实验及 Stage 1 共 **7844** 份文件的大小/mtime 清单保持一致，原 Stage 1 六份产物未改写。
没有清理旧证据，没有更改 Selective、deadline、发送源、固定 local 合同、P score 或 baseline。
**Stage 2A 完成，停在人工审阅点。** 不自动进入 START–Selective、10/100 Gbps、正式矩阵、CI、提交或推送。
