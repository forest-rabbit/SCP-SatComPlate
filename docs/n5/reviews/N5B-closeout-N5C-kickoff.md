# N5B 收口与 N5C 启动边界

2026-09-13。依据项目外 `N5B_Closeout_and_N5C_Kickoff_Codex_Taskbook.md`、
`V4_N5C_Complete_Model_and_Backup_Node_Selection_Algorithm (1).md` 及用户确认的八点修订。
本页记录当前收口，不覆盖历史报告；本轮不改变冻结场景或执行 N5C 大实验。

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

## 已确认的 N5C 接口（尚未实现）

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
- 原 32 组 `output/pre-n5c-placement-final`（执行 `b51cc9d63`）保持只读；未来只有
  FA-FFP 回归等价门槛成立才复用相应对照，不在本次收口宣称 N5C 已通过该门槛。

## G4 验证与当前停止点

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

**当前 Gate：READY_FOR_USER_MERGE，不写 N5B CLOSED。** 本地修正、影响审计与必要回归
完成，未发现必须重跑的正式组。PR #98 更新后仍需明确合并授权；未合并前不创建正式
N5C 分支，不运行 GitHub CI、不合 main、不打 tag、不删除任何分支。
