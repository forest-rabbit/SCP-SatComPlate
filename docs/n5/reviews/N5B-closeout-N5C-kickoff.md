# N5B 收口与 N5C 启动边界

2026-09-13。依据项目外 `N5B_Closeout_and_N5C_Kickoff_Codex_Taskbook.md`、
`V4_N5C_Complete_Model_and_Backup_Node_Selection_Algorithm (1).md` 及用户确认的八点修订。
本页记录收口与用户随后授权的 N5C 实施，不覆盖历史报告、不改变冻结场景。

## G0 身份与分支

修改前 GitHub 实时检查：两个 PR 均 OPEN、Ready、MERGEABLE，无 status checks。

| 对象 | 修改前 commit | 处理 |
|---|---|---|
| PR #98 / `feature/pre-n5c-cb-sat` | `96ef0d38f4038eae446e00eb78bfe36a3de01bed` | 在此分支修正，base 为 n5 |
| PR #99 / `feature/pre-n5c-compfrr-v7-jit` | `2ff3c5c98d58ed68c192f97cf47dff7a7e45cefd` | 保留，不合并、不关闭 |
| n5 | `2ccfa392e1ae4c639e260fba388afec66300fb2b` | 等待明确合并授权 |
| main | `009788ca9c5042160e50a014c6d657e785f225f3` | 不变 |

初始工作区干净，从 V7 分支切回干净的 PR #98 分支；不把 JIT 合入或复制进正式代码。
N5C 最终从包含 PR #98 修正、且不含 PR #99 的 n5 创建 `feature/n5c-backup-placement-v4`。

## G1–G2：quota 防御性修正与证据

ShareStorage 仍为“已有占用 + 均分剩余空闲”，quota 不会因正常新增 owner 小于已有占用。
统一 `AvailableQuota` 仅防御无符号减法；reserve、源缓存预检、初始化和 relocation 使用
相同语义。H/X、MTBF、节点选择、任务、故障、恢复和 deadline 均不修改。

两类测试：异常 quota/occupied 及 uint64 边界的纯函数验证；真实 manager/pool 的新增
owner、超额 LOG/FULL/relocation 拒绝、边界准入、既有对象保留和完整释放。
审计器另覆盖超额获准记录、跨 CSV 同纳秒歧义、记录缺失/错误归属、容量和最终释放。

只读复用完整 INPUT 修正后的八组 `output/cb-sat-v2/20260913T070155626344Z-formal`，
原执行 commit 为 `367f23f393c45205cf87fbee003bfb668f453d5e`，每组完整 1300 s。
该执行版本与修正前 PR #98 的 CB policy/manager/recovery 源码一致。
旧 `20260912T171143276614Z-formal` 已被 INPUT 修正取代，仅作为历史保留。

最终离线审计覆盖 328909 条 quota decision、981783 条事件、151780 次获准对象申请：
`affected_count=0`、`max_occupied_minus_quota_bytes=0`、`ambiguous_count=0`。
逐 owner 的实际占用与物理池一致，最终全释放。饱和保护不要求重跑八组正式仿真。
未记录的被拒绝 preview 不伪称逐条重建；跨 CSV 同纳秒用所有可能 owner 的上下界，
只有可证明安全时才通过。原输出目录和 execution identity 不改写。

完整 INPUT、strict-before root/log 才能 DIRECT/RELOCATE；否则从零重算。X=0 仍区分
`NO_REMAINING_LOGS` 与 `EMERGENCY_COMPACTION`。CB 主 adaptation 为 busy=recompute，
relocate 为扩展。最新八组完成数：FFP/LRL/FA-FFP/FA-LRL 的 recompute 为
795/797/795/798，relocate 均 800；不把最初缺 INPUT 的恢复记录作为正式结论。

## G3：JIT 保留研究历史

**JIT V7 evaluated, not selected for N5C。** PR #99、分支和原实验均保留。
历史证据：[固定版本联合审计](https://github.com/forest-rabbit/SCP-SatComPlate/blob/2ff3c5c98d58ed68c192f97cf47dff7a7e45cefd/docs/n5/reviews/Pre-N5C-v7-cbsat-joint-audit.md)。
同口径 FA-LRL 的 Eager/Deferred/JIT 额外应用流量约 199.37/107.72/193.26 GB，
平均 catch-up 约 123.31/315.18/274.94 ms，JIT useful ratio 约 4.79%。
这不是 N5C 的 FA-FFP 对照表，不能混用 placement 推断空间选择收益。

Post-N5C 备忘录合并在本页，不另建档案：未来可讨论 SEND NOW 相比 WAIT NEXT
EVALUATION 的首次故障概率加权等待收益；网络成本与时间收益的统一口径尚未确定。
N5C 前后本轮均不实现阈值、预算、JIT-next 或新优化器。

## 已确认的 N5C 接口

- START：现有 FA-FFP 只读 reference pair 提供参考资源，不占用资源；求一次 START/
  `(delta,n)` 后保留 local。START 成立才在可兑现固定配置的 remote 中按 V4 排名；
  严禁针对每个 remote 重跑 Frequency，也不进行 local/remote 联合优化。
- ON：不重新运行 N5C，Frequency 读取 committed 实际节点对的资源更新配置；不能兑现
  时沿用 pause/retry，不自动 re-placement。公式不变不等于不同 placement 的未来决策轨迹相同。
- Ready：Eager 遵守预置 INPUT；Deferred 不要求故障前 INPUT resident，但完整 INPUT
  获取路径与恢复 deadline 必须可行。预测可兑现性不冒充已完成的物理 READY。
- Storage：actual 与 peak quota 不重复，更新替换旧 quota，结束释放；物理池仍最终准入。
- History：普通计算和恢复计算共享一致的存活观测 exposure，不因 recovery immunity
  导致分子分母范围不同，不用截断比值掩盖错误，保持 `[0,1]`。
- V4 恢复时间只用于候选估计，不修改真实 INPUT/state 并发传输、恢复随机流或 deadline。
- 保留 FA-FFP/FA-LRL 等原策略行为；N5C 只选择 CompFRR 的 designated remote，
  不进入恢复策略或 Routing。首轮 Eager/Deferred 各一组，稳定后 Deferred 三项消融。
- 原 32 组 `output/pre-n5c-placement-final`（执行 `b51cc9d63`）保持只读；
  相应 FA-FFP 对照仅在下述完整回归等价门槛通过后复用。

## G4 收口验证

目标模块及维护测试已编译；C++ 全部维护测试通过（CB policy 89262、runtime 881308
次检查，含新增 quota fixture）；Python 136 项，135 通过、1 项可选环境测试跳过。
完整维护 smoke/regression 已通过，包括 16 组 placement smoke、100 任务故障回归及
483 条在线模型/预测概率一致性记录；CB 26 个恢复 fixture、13 类损坏拒绝仍通过。

代码与审计工具提交 `48e5ac4f443bb2e454a5df5b94ebb3572b2ee314`，干净工作区执行：

- CB 四种 placement × 两种 busy 的 8 组 smoke，全部 `AUDIT_PASS`；每组 15 s、4 任务。
  目录 `output/cb-sat-v2/20260913T091407898587Z-smoke`；FA-LRL/relocate 再执行一次，
  26 份 CSV/JSON 确定性一致（仅忽略运行身份、输出路径和墙钟）。这不是新的正式 1300 s 仿真。
- 最终审计 `output/audits/cb-quota-underflow-impact.json`：audit commit 为上述版本，
  `audit_worktree_dirty=false`，source execution 仍为 `367f23f39`，不重写历史运行身份。
- 其他阶段日志在 `output/audits/n5b-closeout-checks/`；`*-working.json` 仅是早期试审计，
  不替代最终文件。本页后续提交只回填证据，不改变已验证代码。

**N5B CLOSED。** 用户随后明确授权合并 PR #98：GitHub 确认已合并，merge commit 为
`17c4414dcd61a34a2068a84fefb8ccda7e6b7079`，含修正 head `9a2f029cf`。
`feature/n5c-backup-placement-v4` 由该新 n5 创建，main 不变，PR #99/JIT 不进入基线。
CB 分支仍作为 PR #99 的 base，暂不删除；不新增 GitHub CI，不合 main、不打 tag。

## N5C V4 实施与验收

V4 评分、固定 reference/实际 remote 适配、ON quota 替换和只读资源积分已接入；
公式与代码字段见 protection README，不在本页重复模型。执行版本为干净提交
`c7f1a91e12f57064646af4f52705724dc27cb990`，七组均完整运行 1300 s：两个 FA-FFP 等价
门槛、Eager/Deferred 两组主实验、Deferred noR/noU/noM 三组消融。66 星、800 任务、
10 Gbps、1 ms、seed=1/run=11、352513119 WU 及故障参数均未修改。

新 FA-FFP 对旧执行 `b51cc9d63` 的每组 26 份 CSV 逐字节一致，业务/状态 JSON 及其余
JSON 等价（仅排除身份、路径、墙钟）。`fa-ffp-equivalence.json` 通过后才运行 N5C；
主实验审计通过后才启动消融。旧证据未覆盖，JIT 未参与。

### 正式主结果

R5=Eager，R7=Deferred，均使用 busy=relocate。流量为额外应用层实际发送的十进制 GB，
不是逐跳字节；waste 为 total compute-capacity equivalent waste，含预留空闲机会成本，
不等同实际 CPU 浪费。catch 为故障到追平 W_f；未追平不填零。

| 组 | 完成/800 | catch 样本 | mean / P50 / P95（ms） | total waste（百万 eq-WU） | 额外应用流量（GB） |
|---|---:|---:|---:|---:|---:|
| R5 FA-FFP | 800 | 83 | 150.59 / 67.24 / 418.55 | 1.666240 | 197.947086 |
| R5 N5C | 800 | 83 | 116.16 / 65.75 / 374.85 | 1.388899 | 198.236345 |
| R7 FA-FFP | 799 | 82 | 340.03 / 282.48 / 734.06 | 4.295718 | 105.397737 |
| R7 N5C | 799 | 83 | 343.54 / 279.19 / 638.71 | 3.726479 | 107.665407 |

R5 的 total waste 降低 16.64%、平均 catch 降低 22.87%，额外流量增加 0.15%。R7 的
total waste 降低 13.25%、额外流量增加 2.15%；双方共同观测到 catch 的 82 个任务，
均值为 340.03→341.68 ms（增加 0.48%），不把样本不同的总体均值当作逐任务提速。
实际执行浪费分别为 R5 913174→674949 WU、R7 1871067→1315083 WU；
normal protection、reserved-idle 与 active/total eq-WU 在机器汇总中严格分列。

### 恢复与热点

以下动作比例的分母均为 recovery_attempted=83；busy 的分母是故障时有 designated
backup 的 83 次事件。failed 是结果，可与 direct/relocate/recompute 重叠，不是第四条互斥路径。
所有节点（含零负载节点）均计入集中度；storage HHI 使用实际 byte-time 积分。

| 组 | direct / relocate / recompute | busy/83 | recovery failed | assignment Top-1 | assignment HHI | storage HHI |
|---|---:|---:|---:|---:|---:|---:|
| R5 FA-FFP | 75 / 3 / 5 | 3 | 0 | 62.41% | 0.454566 | 0.336525 |
| R5 N5C | 77 / 1 / 5 | 1 | 0 | 10.69% | 0.057069 | 0.051187 |
| R7 FA-FFP | 81 / 2 / 0 | 2 | 1 | 61.12% | 0.439374 | 0.129259 |
| R7 N5C | 82 / 1 / 0 | 1 | 1 | 8.31% | 0.042079 | 0.051705 |

R7 FA-FFP 失败任务为 574，N5C 为 140：N5C 修复了 574 的远端状态落后，但 140 在
316 s 故障时 remote 15 空闲、tail 路径却为 NO_ADMISSIBLE_PATH，转 REMOTE_REDO；
W_f=247785、W_L=242885、W_R=0 WU，剩余 deadline 4.6815 s，不足以完成从零重算。
它不是 busy 导致，也不是取消 Frequency 更新导致。当前候选可行性不保证未来路径可用，
保留原恢复合同、不为这一任务重新选点或调整 deadline。

R7 FA-FFP 的迁移 state 逻辑字节虽为 0，迁移操作仍实际发送 0.879831 GB INPUT/tail；
N5C 为 0.729411 GB INPUT/state。汇总已区分逻辑 state 与实际三类流量，包含部分发送与取消。
R5/R7 的全网平均链路利用率分别为 0.504055%→0.424468%、0.426223%→0.413902%；
与应用流量变化方向不同并不矛盾，链路指标还取决于经过哪些路径。

### Deferred 消融与停止诊断

| 版本 | 完成/800 | busy / relocate | mean / P95 catch（ms） | total waste（百万 eq-WU） | 额外流量（GB） | assignment HHI |
|---|---:|---:|---:|---:|---:|---:|
| full | 799 | 1 / 1 | 343.54 / 638.71 | 3.726479 | 107.665407 | 0.042079 |
| noR | 799 | 1 / 1 | 343.64 / 638.71 | 3.727208 | 107.665405 | 0.042282 |
| noU | 800 | 0 / 0 | 316.82 / 604.39 | 3.066511 | 107.799429 | 0.071239 |
| noM | 799 | 2 / 2 | 354.33 / 858.27 | 3.818691 | 107.352108 | 0.033563 |

各版本均有 83 次恢复尝试、83 个 catch 样本，无 RECOMPUTE；Deferred 四组生成的故障
JSON 完全一致，恢复任务/故障时刻/类型一致。三项消融均保留所有硬约束。
noU 的 140 选择 remote 13，故障时走 TAIL 并完成；该场景中 noU 的完成数/浪费优于 full，
但 assignment 更集中。noM 更分散却出现更多 busy、更慢追平；不把均衡本身当作可靠性保证。

Eager/Deferred 平均可行候选为 56.90/56.50，不是被硬约束压缩成单候选。Eager 421 次
选择均由 M 主导；Deferred 为 M 370 次、U 39 次。R 在可行候选中最大约 0.955，但大多
为零，full 的最终选择只有 2 次非零 R；因此 noR 影响很小。主导维度不表示其他维度未参与
淘汰候选；noM 的 R 主导标签还包含全零并列，不据此声称竞争风险已充分验证。
U、M 均有动态范围，观测 exposure 与物理账本通过检查。按 G12 保留这些局限，
不调权重、不新增优化器，也不预设优于历史 FA-LRL（其 R7 为 800 完成）。

### 验证与交付边界

目标构建与全部维护 C++ 测试通过：V4 2373 项、frequency runtime 5252 项（新增真实
共享 remote 的非零竞争预测检查），8 组空间 fixture / 10 个提案的独立审计通过。
Python 143 项、142 通过/1 可选跳过；完整 smoke/regression 在执行提交前通过，包括
16 placement 组、100 任务联合故障与 483 条概率一致性记录；收尾再次通过 16 组 placement smoke。

原始结果、七组 `comparison.json/.csv`、等价门槛在 `output/n5c-v4/formal/`；构建/测试日志
在 `output/n5c-v4/`。七组统一离线审计为 AUDIT_PASS，包含原 Frequency 独立校验、
固定配置与确定性胜者、actual/quota、完整释放、assignment/storage 积分和实际资源守恒。
执行提交之后只修改 CLI 帮助、测试、离线统计与文档，不改仿真模型或场景。
N5C 分支提交供人工审阅；不自动合入 n5/main、不运行额外 GitHub CI、不打 tag，
保留 PR #99 及其依赖分支和历史实验。
