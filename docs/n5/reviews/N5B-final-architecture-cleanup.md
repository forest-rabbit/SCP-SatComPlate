# N5B 最终架构整理

分支 `feature/n5b-compfrr-frequency`，base `n5`，Draft PR #96。
本轮只整理接口并验证原 B 行为，不实现完整 baseline、不进入 N5C、不运行阶段 CI、不合并。

## 接口与边界

`Protection Scheme → Placement Policy → Recovery Policy → Shared Runtime`。
Routing/完整 ECMP 搜索与容量准入始终是公共基础设施，不作为方案开关。

- PlacementPolicy 同时提供 `SelectBackupNode` 和 `SelectCheckpointPair`。
  共用非主星、健康、空闲、可达等基础筛选；具体操作增加自己的路径/存储等要求，
  不是强求迁移与重算拥有完全相同的最终候选集。单节点不构造虚假的 local/remote。
- FFP 使用稳定 ID；LRL 使用 assignment + lambda×active recovery，仍保持 local-first。
  fixed 真正注入 PlacementPolicy，并提供实时负载；fixed/CompFRR 的 pair 均检查三条硬路径。
  fixed 在拥塞下也执行这层候选筛选；不据本轮小测试宣称旧正式 A 的逐事件结果不变。
- 现有恢复的公共候选生成复用单节点基础框架，保持原有 FFP 恢复排序；
  不把 prefault LRL 切换暗中扩大为恢复排序变化。
- `remoteBusyRecoveryPolicy=relocate` 保持当前 B 默认行为；`recompute` **只**在
  REMOTE_BUSY 跳过 checkpoint 迁移，重放原始 INPUT，从 0 开始。
  REMOTE_UNAVAILABLE/PATH_UNAVAILABLE 等不受此开关影响；F3 不可读状态不能迁移。
- 单次决策栈内保存完整路径快照：可达/可准入、原因、路径、速率、传播时延、local。
  pair 筛选与成本适配共用查询，下一事件/任务/epoch/释放重试全部重新读取。
  实际传输仍由 NetworkTransferEngine 正式准入，预览不预留资源、不消耗 flow key。
- 若 x_f 表示归一化故障进度，Recompute 的 x_f×W 是 planned full catch-up；
  actual 只按 ComputeService 实际执行 WU 记账，中断时不能计满计划量。

## 参数矩阵与后续扩展

参数仍在 `para.h/.cc`，CLI 校验在 `satcompute.cc`，不增加配置 JSON 或参数层。

| protectionMode | placementMode | remoteBusyRecoveryPolicy | 当前状态 |
|---|---|---|---|
| off | ffp（不使用）；lrl 拒绝 | 合法值忽略 | 无常态保护、无故障恢复 |
| fixed | ffp / lrl | relocate / recompute | 可运行，delta=0.05、n=4 默认不变 |
| compfrr | ffp / lrl | relocate / recompute | 可运行，在线 generate + F1/F2 |
| recompute / one-plus-one | 未来 ffp / n5c | 不适用 | 明确 NOT_IMPLEMENTED |
| fixed / compfrr + n5c | n5c | relocate / recompute | 明确 NOT_IMPLEMENTED |

未来 Recompute baseline 无预故障 checkpoint，故障后选择单节点重算；
1+1 要真实输入、双副本计算、winner RESULT 与 actual 计费，不能用极小 checkpoint 代替。
两者复用单节点 PlacementPolicy（因此可共用 FFP 和未来 N5C）、共享 runtime/账本/路由，
不另建 ReplicaPlacementPolicy。当前 RECOMPUTE 恢复后备不能作为完整 baseline 的实验结果。
N5C 的接口可分别实现单节点排名和双节点排名，本轮不实现其评分。

## 验证

本地构建、78 Python（无 skip）、21 C++ 程序、10 smoke、4 组维护回归通过。
日志：`output/n5b-architecture-validation/`。其中：

- 纯策略 211074 个检查，FFP 穷举规则与 CompFRR 数值锚点不变；单节点拒绝条件、
  操作特定筛选、FFP/LRL 排名和 fixed 的实际注入通过。
- 路径/机制 2884 个检查：真实 fixed 双任务负载影响 LRL、同次 pair/成本复用查询、
  下一决策读新容量、旧预览不绕过新传输准入、LocalDelivery 的 1 ns 因果边界及资源归零。
- 恢复 1539 个检查：默认迁移、忙远端从零重算、非忙计算不可用仍迁移、
  重算追赶中被 F3 打断只计实际 WU。原 N4B 回归仍为 88/12、11 START、483 条概率一致。

完整 800 任务 B 的本轮复现结果将在运行后补入；此前不宣布冻结验收完成。
历史基准见 [最终 800 任务场景](N5B-final-800-task-scene.md)，不覆盖历史输出。
