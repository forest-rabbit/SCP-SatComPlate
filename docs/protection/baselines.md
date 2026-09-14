# Baselines 与公共 Placement

off 不创建保护池、流或 CSV，不做故障恢复；fixed 对每个首次主计算启动执行一次固定策略，
不做动态概率决策。FFP/LRL 只用健康、空闲（含队列为空）、非主/互异和 local 一跳结构条件；
FA-FFP/FA-LRL 另用原可达性和三条路径准入筛选。FFP 按 local/remote 稳定 ID 排名，
LRL 按 `(local load, local ID, remote load, remote ID)` 排名，load 为活动 assignment + 活动 recovery（正式 lambda=1）。
候选可行不等于存储/带宽已经预留；fixed 的频率仍来自 delta/n，不调用 CompFRR 求解器。
多个任务竞争同一候选时，由共享备份池与网络容量准入处理；恢复接管时额外原子锁定空闲服务。

架构为 `Protection Scheme → Placement Policy → Recovery Policy → Shared Runtime`；
Routing 是所有方案共用的基础设施，不属于其中任何算法开关。
`SelectBackupNode` 为单节点角色，`SelectCheckpointPair` 为 local/remote 角色；
两者按显式 `PlacementEligibility` 选择 minimal 或 FA builder；操作再提供自己的准入条件，不要求最终候选集相同。
单节点不要求一跳或虚构 local，允许 source/recovery/result 同星并沿用 LocalDelivery。
四种策略均实现两种角色；当前 checkpoint 恢复目标仍统一按既有稳定 ID 与操作可行性选择，
不会因切换 prefault LRL 而顺带改变恢复排序。

`protectionMode=checkbullet` 接入独立的 [CB-Sat](../../contrib/satcompute/protection/baseline/checkbullet/README.md)：
单备份星保留完整 INPUT、状态根及连续日志，复用公共服务与四种 placement，但不继承双层 tail。
内部 H/X 位于 canonical 子目录，独立标定 MTBF 保留原兼容路径；平台 `para.cc` 不增加 CB 专有数值。

`protectionMode=recompute` 是完整 baseline：没有常态保护；首次主计算故障后调用
`PlacementPolicy::SelectBackupNode`，默认 FA-FFP 选非主、健康、空闲且可达结果端的节点，
还需原始 INPUT 当前可准入且 INPUT 估计加完整计算可能满足原 compute deadline。
复用 RecoveryController 和零容量共享账本，不创建 checkpoint 对象，不产生 cL/cR、L1 或 REMOTE_BATCH。
`baseline/recompute/recompute-runtime.*` 接线、`baseline/recompute/recompute-policy.h` 只决定故障后重算。
`recovery-summary.csv` 对此模式增加 planned INPUT 等待及 planned 浪费列，实际值仍取真实执行。
该严格筛选仅适用于完整 baseline，不改变 checkpoint 方案既有的 RECOMPUTE 后备行为。
完整 recompute 与 one-plus-one 均支持四种 baseline placement；`placementMode=n5c` 仅允许 CompFRR。

### Pre-N5C 可行性筛选消融

默认 `fa-ffp` 保持最新 capacity-resume 版本行为；旧报告 `ffp/lrl` 必须按当时提交解释，
不能将全部早期实现都宣称为当前 FA。新简化版选定一个候选后仍做真实路径/资源/操作检查，
失败则结束本次决策，不寻找第二个候选。CompFRR 后续正常 epoch 或真实容量释放仍可重新决策，
不是永久禁止该任务保护；fixed/1+1 仍是首次 TASK_RUNNING 一次申请。
FA 的 Frequency 存储/deadline 搜索只保留在原 CompFRR 流程，Fixed 不新增 Frequency 筛选。
ON 固定节点对及路径释放即时恢复不变。不会因选中空闲节点而长期预占其计算服务。

四种 placement 只影响故障前 checkpoint pair，以及 R0 原生重算/R1 首次副本的单节点选择；
checkpoint 故障后的 recovery/relocation 排序和候选搜索不切换。R0 记录真实接受恢复到计算完成/清理的活动负载；
R1 assignment 从副本准入到完成/取消，recovery 从完整故障 batch 后 takeover 到计算完成/清理。
无任务可选时不伪造负载；idle 限制下 LRL 与 FFP 相同也是有效结果。

`placement-selections.csv` 记录 task/time、模式、`selected_by_minimal_policy`、pair 或单节点、
`actual_admission` 和原因；ACCEPTED 仅表示同步资源准入，不代表异步 INPUT/初始化已完成。
task/time 不是 CompFRR 决策的唯一键：普通决策后，同纳秒真实容量释放可触发一次非抽样重试。
离线审计按 OFF frequency 记录的触发类型及事件顺序逐条关联，禁止同类重复或容量重试额外抽样。
异步完成/失败仍核对保护/副本/恢复原始账本。Frequency 的全候选路径/硬约束计数字段只适用于 FA，
minimal 留空，不能把未检查的候选声称为 path-feasible。分布统计包括全部计算节点的零计数，
R0/R1 的 local/remote、checkpoint 存储指标为不适用；不能据零池占用宣称普通运行内存更优。

## 1+1：一次申请、真实双 attempt

`baseline/one-plus-one/one-plus-one-runtime.*` 在首次 TASK_RUNNING 接线，
`baseline/one-plus-one/one-plus-one-policy.h` 向公共 PlacementPolicy 申请一次副本，
`mechanism/replication/replica-manager.*` 执行真实 INPUT、完整 WU 和 RESULT。
默认 FA-FFP 预筛可达性、INPUT 准入及原 deadline；minimal 先选健康空闲节点，再对该节点核对相同准入。
未准入不重试，副本失败不创建第三副本，不追加隐藏的 Recompute。主任务不等待副本。

正常副本不免疫 F1/F2；INPUT 期间的计算故障沿用普通任务语义：输入继续、计算等待可用性恢复。
主 attempt 失效时，同纳秒所有故障及通信拓扑覆盖完成后，幸存且可接管的副本才进入恢复 attempt
的 F1/F2 免疫；F3 始终有效。同批主副本均被击中不能因处理顺序而提前免疫。
两个 attempt 共享最初的绝对 **compute deadline**，不以 RESULT 到达时间判超期；
deadline 取消尚未算完的 attempt，但保留已按时算完、正在交付的 RESULT。
首个有效逻辑 RESULT 获胜，取消另一 attempt 的计算和未完成传输，已发送字节保留。

`metrics/core/replica-metrics.cc` 输出 `replica-summary.csv`、`replica-attempts.csv`、
`replica-events.csv`、`replica-transfers.csv`。两个 attempt 的 WU、等待和传输独立记录；
成功任务的冗余 WU 为两者实际执行之和减 W，失败任务只报告 raw actual，不做负数减法。
planned INPUT 等待取准入时估计，actual reserved-idle 取锁定服务到实际启动/终止的时间。
REPLICA_INPUT 全属额外流量；仅获胜 RESULT 属业务，败方已发送 RESULT 属额外流量。
`replica-transfers.csv` 保留所有结果流，包括原主 RESULT；不能只加总 checkpoint transfer 表来算 1+1 开销。
这两种 baseline 不分配 checkpoint 对象；使用 `TransferOnlyRecoveryLedger`，
其零容量池仅保留既有输出行，实际流由统一 dispatcher 编号，不继承 checkpoint executor。
普通任务与副本的 active working-set 存储未独立量化，不能据此宣称 1+1 存储更优。

## Placement 与诊断口径

LRL 仅将 FFP 的稳定 ID 排序换成 `(activeRemoteAssignments + lambda*activeRecoveries, nodeId)`；
local 先选、remote 排除 primary/local，同样要求健康、空闲、可达和 local 一跳。
两者使用相同频率求解器、存储/路径/deadline 准入。lambda 在正式实验前固定为 1。
硬约束中的空闲通常已排除恢复中的节点，因此当前主要由有效 remote assignment 驱动分散；
local-first 和一跳集合仍会影响分布，不保证每节点均匀。

有效分配在真实 INIT_BASE 预留成功后加一，实际状态释放后减一；故障快照保留的 remote state
在恢复使用完并释放时才撤销分配。恢复负载在实际 accepted（含 reserved-idle）加一，计算完成、
失败或清理后减一，RESULT 传输期间不再占用恢复计算服务。重复清理不重复扣数。
`placement-node-summary.csv` 含所有计算星（包括零负载星）的累计次数/活动峰值/最终值；
`placement-load-events.csv` 保留实际变更顺序，最终所有活动计数必须归零。

`frequency-decisions.csv` 的节点字段统一为 `local_node/remote_node`，附 placement mode 和当前负载。
`frequency-pause-intervals.csv` 记录暂停区间及原因；原因改变会分段，总次数按相邻区间合并，
持续时间在真实 protection stop 截止。delta/n 分位数按已提交 START/UPDATE 样本计算；
时间加权只计物理 ON 且未 PAUSE 的时间，初始化期间不计。

`recovery-summary.csv` 附 checkpoint 是否存在、故障回调时 remote 是否可用/忙以及回退原因：
`REMOTE_BUSY / REMOTE_UNAVAILABLE / REMOTE_F3 / STATE_MISSING / PATH_UNAVAILABLE / OTHER`。
故障回调时节点健康已更新，但同批路由 overlay 尚未应用；实际 fallback 原因取下一纳秒的
恢复裁决状态。两者时间点不同，不将它们强行视作同一个可行性快照，也不改变原恢复裁决。
