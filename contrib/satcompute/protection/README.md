# 保护与恢复模块

N5A 回答“怎样执行保护”，N5B 才决定启动/频率，N5C 才优化节点选择。
当前接入单次故障恢复闭环和 **G4 planned/actual 资源账本**：真实备份路径、故障快照、
恢复服务预留、直接/迁移 checkpoint recovery、RECOMPUTE 和 winning RESULT。cL/cR 不占用主 ComputeService。
FIXED 正式场景仅为执行验收，不代表 CompFRR 算法效果。N5A 已合入 n5；
N5B 已将独立频率策略接入在线故障与真实 checkpoint；G3 对比固定/动态频率与 LRL 诊断，N5C 未实现。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `common/protection-types.h/.cc` | 动作、因果上下文、attempt 身份/免疫/终态守卫、恢复路径估计选择 |
| `common/task-state-adapter.h/.cc` | 独立的生产 G1 布局、整数 WU/状态/H/合法边界映射及唯一生产成本档位 |
| `storage/backup-storage-pool.h/.cc` | 每节点额外备份容量、used/reserved、原地融合、按任务清理及峰值 |
| `runtime/protection-runtime.h/.cc` | Policy/Mechanism 窄接口、动作分发、故障接管机会和清理通知 |
| `policy/fixed/fixed-protection-policy.h/.cc` | 首次主计算启动时的一次固定保护、注入 PlacementPolicy、失败时重算后备动作 |
| `policy/placement-policy.h`、`policy/feasible-placement-pairs.cc` | 单节点/双节点共用的候选筛选与排名接口 |
| `policy/recovery-policy.h` | 仅切换 REMOTE_BUSY 的 recompute / relocate 选择 |
| `runtime/decision-path-snapshot.h` | 同次同步决策内缓存完整路径估计，跨事件不保留 |
| `policy/baseline/first-feasible-placement/`、`least-recovery-load/` | 最小筛选 FFP/LRL；分别按稳定 ID、当前活动负载排序 |
| `policy/baseline/fa-first-feasible-placement/`、`fa-least-recovery-load/` | FA-FFP/FA-LRL：保留最新历史实现的可行性筛选，复用相同排序 |
| `policy/compfrr/frequency/compfrr-frequency-policy.h/.cc` | 独立 J_OFF/J_START/J_ON 求解、可行域、当前 q/完成前预测接口 |
| `policy/compfrr/frequency/frequency-decision-gate.h/.cc` | 每任务纯 proposal/commit 合同、前向 target/batch 规则；无仿真接线 |
| `mechanism/checkpoint/checkpoint-progress.h/.cc` | 不发包的纯进度合同：生成延迟、连续接收、融合提交及同纳秒历史查询 |
| `mechanism/checkpoint/checkpoint-manager.h/.cc` | 初始化、L1、batch 真实传输、存储预留/提交及停止清理 |
| `runtime/fixed-protection-controller.h/.cc` | 只读任务事件接线、候选快照、固定策略/机制分发 |
| `runtime/frequency-protection-controller.h/.cc` | generate 检查点前提案、故障执行后提交，FFP/路径/算力适配及实际机制调用 |
| `runtime/frequency-storage-estimator.h/.cc` | 按合法状态与真实库存计算保守的额外存储峰值 |
| `runtime/placement-load-ledger.h/.cc` | 有效 remote 分配/恢复所有权、累计次数、峰值及真实释放 |
| `../metrics/core/placement-load-metrics.cc` | 节点集中度和每次负载变更 CSV |
| `../metrics/core/frequency-metrics.cc` | 独立逐 epoch 决策 CSV；不混入 N5A actual 成本账本 |
| `runtime/recovery-controller.h/.cc` | 故障裁决、一次恢复、真实输入/尾部/计算/结果及终态清理 |
| `policy/baseline/recompute/`、`runtime/recompute-controller.*` | 独立无常态保护策略、公共 FFP 与完整从零恢复接线 |
| `policy/baseline/one-plus-one/`、`runtime/one-plus-one-controller.*` | 一次性热副本申请策略与主计算启动接线 |
| `mechanism/replication/replica-manager.*`、`../metrics/core/replica-metrics.cc` | 真实并行 attempt、完整故障 batch 后接管、首个 RESULT 裁决和独立实际账本 |
| `runtime/protection-transfer-key.h` | 同纳秒请求的稳定排序键与不回绕的保护流编号 |
| `../traffic/local-delivery.h/.cc` | 同星逻辑交付；不创建 UDP，不计网络字节 |

不建空目录或完整插件框架。
1+1 的独立 attempt 复用真实 ComputeService、网络和资源账本；Multi-tree 未实现。
生产文件不引用 `tools/validation/compfrr-shadow`；测试可单向使用它核对旧布局，
不能拿旧 shadow 的理想网络耗时要求真实备份时序完全一致。

## 参数与当前可运行范围

参数在外层 `para.h/.cc`，CLI 注册/校验在 `satcompute.cc`，不增加完整配置 JSON。

| 参数 | 默认 | 说明 |
|---|---:|---|
| `protectionMode` | `off` | `recompute` 完整从零重算；`one-plus-one` 真实热副本；`fixed` 固定检查点；`compfrr` 动态频率，仅后者要求 generate 且启用 F1/F2 至少一个来源；保护模式均要求网络任务、shadow 关闭 |
| `backupStorageBytesPerNode` | `10000000000` B | 十进制 10 GB；仅为实验容量，可覆盖，0 可用于存储不足测试 |
| `fixedProtectionDelta` | `0.05` | 5% 增量；千分之一精度，转换后传入纯策略 |
| `fixedProtectionBatchN` | `4` | 4 个连续有效 L1 一批，要求 n>0 且 n×delta≤1 |
| `placementMode` | `fa-ffp` | `ffp/lrl` 最小筛选；`fa-ffp/fa-lrl` 可行性感知筛选，四种保护模式均可注入 |
| `remoteBusyRecoveryPolicy` | `relocate` | 仅 fixed/compfrr/checkbullet 的 REMOTE_BUSY 分支：迁移 checkpoint 或从零重算；off/recompute/one-plus-one 不使用此开关 |
| `inputStagingPolicy` | `eager` | `eager` 保持旧预置行为；显式 `deferred` 仅支持 compfrr，常态只保护状态、故障后获取一次完整原始 INPUT |
| `lrlRecoveryWeight` | `1` | G3 正式运行前冻结，不扫描或事后选择；不影响 FFP |

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

`protectionMode=checkbullet` 接入独立的 [CB-Sat](policy/baseline/checkbullet/README.md)：
单备份星保留完整 INPUT、状态根及连续日志，复用公共服务与四种 placement，但不继承双层 tail。
内部 H/X 和独立标定 MTBF 均位于该子目录；平台 `para.cc` 不增加 CB 专有数值。

`protectionMode=recompute` 是完整 baseline：没有常态保护；首次主计算故障后调用
`PlacementPolicy::SelectBackupNode`，默认 FA-FFP 选非主、健康、空闲且可达结果端的节点，
还需原始 INPUT 当前可准入且 INPUT 估计加完整计算可能满足原 compute deadline。
复用 RecoveryController 和零容量共享账本，不创建 checkpoint 对象，不产生 cL/cR、L1 或 REMOTE_BATCH。
`recompute-controller.*` 接线、`policy/baseline/recompute/recompute-policy.h` 只决定故障后重算。
`recovery-summary.csv` 对此模式增加 planned INPUT 等待及 planned 浪费列，实际值仍取真实执行。
该严格筛选仅适用于完整 baseline，不改变 checkpoint 方案既有的 RECOMPUTE 后备行为。
完整 recompute 与 one-plus-one 均支持四种 placement；`placementMode=n5c` 仍明确报 `NOT_IMPLEMENTED`。

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

`runtime/one-plus-one-controller.*` 在首次 TASK_RUNNING 接线，
`policy/baseline/one-plus-one/one-plus-one-policy.h` 向公共 PlacementPolicy 申请一次副本，
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
这两种 baseline 不分配 checkpoint 对象；零容量共享账本仅复用规范化流编号。
普通任务与副本的 active working-set 存储未独立量化，不能据此宣称 1+1 存储更优。

## N5B：频率策略与在线接线

`compfrr` 默认组合 `CompFrrFrequencyPolicy + FA-FFP`；历史 A/B/C 名称按当时报告解释。
频率求解器独立实现数学公式，不调用验证目录；只有测试将同输入送入旧 shadow 比较。
`FrequencyInput` 是当前状态的只读数值快照，FFP 先给出节点，G2 适配层再提供主/恢复算力、
输入/备份路径估计和 storage headroom。cL/cR 从唯一 `GetProtectionCosts(Kvar)` 取得。
每个候选的额外 local/remote 峰值由必填的纯 `storageDemand` 提供，和真实池 free bytes 比较；
不得重复扣除当前已用/已预留状态。未提供估计器会拒绝输入，不默认当成容量无限。
G1 验证纯接口，G2 将实际库存和池快照接入同一求解器。

- OFF→START：先按候选节点的初始化估计计算 `t_ready`，复用 canonical predictor 的未来检查点
  `(t_k,q_k)`，以 `w_k=q_k*prod(j<k,1-q_j)` 计算首次故障质量。
  `P_ready=sum(t_k>=t_ready,w_k)`；初始化之前的风险仍参与 survival，不能重新归一化。
  `x_k=min(1,x+muP*(t_k-now)/W)`，`x_hat_ready=sum(w_k*x_k)/P_ready`（只加 ready 后的点）。
  `P_ready=0` 时代表进度留空，不启动保护；`sum(w_k)=P_finish`、`P_ready<=P_finish`。
- Eager：`Joff=P_ready*(S/B_I+x_hat_ready*W/muB)`；
  deferred：`Joff=P_ready*x_hat_ready*W/muB`。
  两者均 `Jstart=cL+cR+min[(1-x)*(cL/delta+cR/(n*delta))+P_ready*Rbar]`。
  正常保护项仍按完整剩余窗口计，不按预计存活时间折扣。
- ON：`Jon=Delta_t*(muP/W)*(cL/delta+cR/(n*delta))+q_current_sample*Rbar`。
- `Rbar=Kvar*(n-1)*delta/(2B)+cR*(n-1)/n+W*delta/(2muB)`。
- `Rmax=deadline-now-W*(1-x)/muB`；eager 候选要求 `Rbar<=Rmax` 并通过存储约束。
- Eager 初始化估计 `max(Tbase,cL+Tstate)+cR<Tremaining`；OFF 可行时严格 `Jstart<Joff` 才提出 START。
- 枚举 delta=1%..10%、步长0.1个百分点，n=1..100，n×delta≤1；精确同分按
  `(objective,delta_permille,n)` 升序，不增加 epsilon 或新的同分目标。

Deferred 的故障 INPUT 是 START/OFF 共同成本，已从两个相对评分同时消去，
不是免除真实恢复传输或从 deadline 中删除 INPUT。
ON 相对评分及 `(delta,n)` 搜索不变，但硬约束改为 `S/B_I+Rbar<=Rmax`，
初始化估计只含 `cL+Tstate+cR`。原 source→remote INPUT 路径成为硬条件。
频率解析评分不额外计传播时延；它是保守估计，不强迫实际 INPUT/state 串行执行。

`MakeFrequencyRisk` 调用现有 `PredictComputeFailureBeforeFinish`，保留它的当前检查点、整数
horizon 和 endpoint 语义；与故障侧提供的本轮联合 q 逐值核对。不用 next-1s 查询替代当前 q，
不重写预测器、成本表或故障抽样。F1/F2 仍分别抽样，F3 不进入策略风险输入。
代表性未来进度只用于 OFF 的预期收益，绝不写入实际 checkpoint、故障进度或恢复 WU。
`frequency-decisions.csv` 新增 `p_fail_after_init_ready`、`representative_progress_after_ready`、
`init_ready_time_ns`、`j_off_start_window`；`old_current_progress_loss` 仅保留旧当前进度损失的诊断值。
预测 ready 点不替代真实 RemoteCommit，同纳秒故障仍不能使用刚提交的 checkpoint。

周期决策网格对齐 fault-check，而非 task-start 的独立1秒定时器。在线按
`更新因果状态 -> q/P_finish -> 提出决策 -> 执行本轮故障 -> 存活且仍计算才提交`
执行。任务启动另做一次即时 OFF 评估（见下节），短于下一检查点的任务预测窗口为空。
检查点提议 START 遇到同轮故障仍视为 OFF；新 delta/n 不能改变当前故障前状态。
`FrequencyDecisionGate` 只维护单任务策略状态，真实初始化、记录、批次和故障仍由 N5A 执行。
新 delta 从实际完成/上次触发边界向前取合法 target；新 n 只消费尚未组批的记录，已建批次不可变。
ON 无可行候选时保留状态、暂停新 target 和新 batch，已有操作继续；不允许 ON→OFF。

N5B-G2/G3 正式接入和算法比较采用在线 **generate**。固定输入和配对 seed/run 不保证不同策略
得到同一故障序列：恢复计算改变负载与温度是 F1 闭环的一部分。N5A 的 validation-replay
仅保留执行验收用途，不增加回放预测器，不覆盖已有 G4 输出。默认仍为 off；
仅显式 `protectionMode=compfrr` 输出 `frequency-decisions.csv`，不依赖 `faultProbabilityAudit`。

### G2 运行时边界与存储估计

故障引擎提供成对的同步回调：当前 q 产生后、独立 F1/F2 抽样前提出决策；
整个同纳秒故障批次、节点与路由状态应用后才 Resolve。F3 时间表不传给策略；
同轮 F3 仍遵守原有“不抽 F1/F2”的规则，在决策记录中标记 `actual_fault_sampled=0`。
START 存活才调用实际初始化，只有物理初始化对象完成融合才进入 ON；预测 T_init 不调度 ON。

G3R 在 primary `TASK_RUNNING` 时立即评估 OFF→START，不等待下一个故障检查点。
该只读预测从下一真实全局抽样点开始，不新增抽样；同刻检查尚未开始则包含当前点，
已开始则排除，预计完成时刻不再抽样。未启动的任务仍在后续检查点重新评估。
即时 START 只进入 INITIALIZING；同纳秒重复决策被去重。故障检查时 ON 的 UPDATE/PAUSE 沿用上述
提案→抽样→存活提交合同；容量释放仅做非抽样的资源重评。`decision_trigger` 区分 TASK_RUNNING / FAULT_EPOCH / CAPACITY_RELEASE；
两个非抽样触发的 `q_current_sample` 留空，风险写入 `p_f1_snapshot/p_f2_snapshot/q_comp_snapshot`。
F3 实际时刻的 F1/F2 因果快照另写 `f3-compute-risk-snapshots.csv`，不额外抽样。

各 placement 的 OFF 候选不预留资源，START 存活后固定节点对；ON 不换节点。
节点当下不健康、不空闲或所需路径不可用时暂停新操作。
路径复用 NetworkTransferEngine 的只读准入查询：capacity-aware 搜索完整 ECMP 路径并使用
当前真实 reservation；不由 Frequency 独自选第一条路径。恢复速率读 remote 的 ComputeService。
primary→remote、primary→local、local→remote 是 START 的硬路径条件。
Eager 的 source→remote INPUT 重放仅用于 OFF 成本比较：不可用时显式记录 `replay_available=0` 和
原因，不虚构带宽/等待时间；P_ready>0 且 START 本身可行时允许启动，P_ready=0 不强制保护。
source=remote 的 INPUT 重算沿用 LocalDelivery，分析带宽用最大有限值表示零序列化极限，实际不发 UDP。

路径快照只存在于一次同步决策的栈内，以 `(source,destination)` 保存完整 reachable/admissible、
原因、选中路径、准入速率、传播时延和 local 标志，候选筛选与 BuildResources 共用一次查询。
下一任务/epoch/容量释放重试重新查询；真实 INIT/L1/BATCH/恢复传输仍向 NetworkTransferEngine
正式准入，快照不预留容量、不消耗 flow key，也不改变路由、故障抽样或事件顺序。

G3R2 演进后的强筛选（当前 FA-FFP/FA-LRL）共享同一时刻的全部可行节点对：健康、空闲、local 一跳且三条硬路径
均获上述只读准入。FFP 按 (local ID, remote ID)；LRL 按 (local load, ID, remote load, ID)。
依次跳过存储/deadline/初始化硬约束失败的节点对；遇到第一组频率硬约束可行的节点对就
比较 J_start/J_off，不按 J 搜索其他节点对。`NO_FEASIBLE_NODE_PAIR`、`NO_ROUTE`、
`NO_CAPACITY_NOW` 分开记录。候选数/路径数为全量；`pair_hard_checked/feasible` 与
storage/deadline skip 仅统计实际检查过的排序前缀，不声称检查了后续所有频率组合。

OFF 且 P_finish>0、FA 的全部候选或 minimal 已选候选暂被容量阻塞时登记等待兴趣，不预留资源。
实际传输释放容量后 ScheduleNow 按任务 ID 重评，同一任务每纳秒最多一次；重新读取进度、
下一真实抽样网格的预测、路径和负载，不复用旧提案、不额外抽故障。成功仍须真实初始化，
INIT/恢复/终态不重试；ON 不做 OFF 重试。`frequency-capacity-waits.csv` 单独记录 OFF 等待区间，
不混入 ON pause、reserved-idle 或 W_waste；START 原因区分任务开始、故障检查及容量释放。

ON 因 `NO_ADMISSIBLE_PATH` 暂停时也登记容量释放通知：保持原节点对，在释放事件完成后
读取当前进度、风险、库存和真实剩余容量，重新求解原 ON 公式；不忽略自身流的预约，不新增故障抽样。
仍被容量阻塞则继续等待，其他原因（节点忙、存储、deadline 等）不因容量释放而持续重试。
可行时提交 UPDATE，记录 `RESUME_AFTER_CAPACITY_RELEASE`，只恢复未来 target/batch，不补造历史检查点。
同纳秒故障批次处理完毕后已进入恢复或终态的任务不重试；重复通知合并，每任务每纳秒最多重试一次。
`capacity_retry_success` 表示 OFF 的 START 或 ON 的 UPDATE 成功，按 `phase_before` 区分；ON 暂停时长
仍只记在 `frequency-pause-intervals.csv`，不作为额外计算占用。非抽样重试的评分使用 `q_comp_snapshot`。

库存快照包含 r/l、已捕获记录及 H、是否分配/接收、当前 remote state 和不可变 batch。
估计器只输出与 **free bytes** 比较的新增峰值：

- OFF 至少覆盖初始化临时峰值（eager 为 INIT_BASE + INIT_STATE，deferred 仅 INIT_STATE）及对应策略的 CommittedStateBytes；
- local 保守计入现有尚未分配记录与剩余合法捕获，不提前抵扣将来网络何时释放空间；
- remote 考虑未来 batch 的实际记录字节和 state 融合峰值，扣除已经计入 used/reserved 的 state/batch；
- OFF 的初始化完成时刻可能受队列影响，因此额外使用剩余变量状态的上界。

这是安全偏保守的容量筛选，不是容量最优估计，也不读取未来队列完成时刻。
真实预留、拒绝、发送和清理由 N5A 账本执行；预测通过不等于资源已预留。
新 delta 只替换未触发目标；delta 不变时不推迟已有目标。新 n 只作用于尚未组批记录。
PAUSE 只停新目标/新 batch，不取消已有生成、传输或融合；UPDATE 可恢复。

`frequency-decisions.csv` 分开记录当前/提议/实际提交的 delta/n、q/P_finish、FFP/空闲字节/速率、
J、Rbar、Rmax、初始化估计、额外存储峰值、实际故障和提交结果。不适用字段留空；
OFF 的 NONE 仍可包含最优候选用于解释为何不启动。真实网络字节、cL/cR、恢复及 W_waste
仍只在既有 protection/recovery 账本中统计。

阶段证据：[G1](../../../docs/n5/reviews/N5B-G1-frequency-policy.md)、
[G2](../../../docs/n5/reviews/N5B-G2-dynamic-frequency-runtime.md)、
[G3 正式对照](../../../docs/n5/reviews/N5B-G3-frequency-evaluation.md)。

### G3 placement 与诊断口径

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

## 最终时序合同

用户最终确认覆盖补充任务书原来的 `cL = cost only` 提案：

```text
合法边界 -> 捕获不可变状态 -> 等 cL -> 开始真实 L1 传输
         -> local receiver 收齐连续记录 -> 推进 l

remote receiver 收齐 batch -> 等 cR -> RemoteCommit
         -> 推进 r -> 释放临时 batch 和 local 中已覆盖的记录
```

cL/cR 同时是等效资源成本和异步逻辑时间，不进入普通 ComputeService、不暂停主计算；
等待期间的新进度不补入已捕获状态。成本按完整 Kvar（不是 INPUT 或单批大小）分档：

| Kvar（十进制） | cL | cR |
|---|---:|---:|
| ≤100 MB | 0.1 ms | 0.5 ms |
| (100,500] MB | 0.5 ms | 2 ms |
| >500 MB | 2 ms | 8 ms |

Eager 初始化同时启动 base 传输和状态生成路径；两条路径都完成后再等 cR。初始变量状态为 0
也必须显式完成初始化；没有变量 payload 时不创建零字节 UDP flow，不靠 `bytes>0` 判断 ON。
正常成本账本使用下文的唯一事件计数口径，生成、接收、提交计数分别记录，
不得把失败/取消操作冒充已提交保护。真实网络传播、序列化与排队不再额外加一份解析时延。

Deferred 不发送 INIT_BASE，也不为 INPUT 预留备份池：分配零字节 REMOTE_STATE 身份，
在 cL 后发送当前 `K(w)+H` 的 INIT_STATE、接收后等一次 cR 才进入 ON。
`w=0` 时保留显式逻辑零状态，照常支付 cL/cR，没有假 UDP；对象存在与否不能用字节数判定。

不实现网络 ACK、重传或第二套网络。RemoteCommit 是内部零字节事件。
`NetworkTransferEngine::RegisterRuntimePlan` 沿用普通 INPUT/RESULT 的 ID/端口；
额外 transfer 从 `max_normal_transfer_id+1` 单调分配，检查 uint64 耗尽。
同一请求纳秒按 `(task_id,attempt_generation,kind,sequence)` 排序，在下一纳秒统一注册，
固定 1 ns 注册等待用于消除 UID 顺序依赖，不计作 cL/cR；注册前重新检查任务资格。
kind 顺序为 INIT_BASE、INIT_STATE、L1、REMOTE_BATCH；sequence 使用捕获/覆盖的整数 WU，
初始化 sequence=0。源端口沿用每源递增的 10000–65535 范围，不复用，耗尽显式停止保护。
G3 追加 RECOVERY_TAIL、RECOVERY_INPUT、RECOVERY_RESULT（后者仅共享编号，属于业务流）；
备份/输入重放流不混进业务吞吐/完成数。
sender finished、reservation、部分接收都不是有效状态；L1 乱序收齐也不能跨越前驱缺口。
G1 的 CheckpointProgress 由单元测试显式驱动时刻，不自己调度仿真事件或模拟 UDP。

## 状态大小与存储

进度用整数完成 WU 表示，W 为总 WU。K 包含变量索引，不含重复 H：

- 非 LLM：`K(w)=floor(Kvar*w/W)`；eager 的 `Mstate(w)=S-floor(S*w/W)+K(w)`。
  剩余原始输入向上保留到整数 B，不采用 `S+K(w)` 或 `min(S,K)`。
- LLM：`K(w)=floor(w/400)*114688 B`，`Mstate(w)=K(w)`；正式 checkpoint 只取完整 token。
- L1：`D_L=K(w_new)-K(w_old)+H`，H 使用既有固定头加十进制 task ID 字节数，LLM H=0。
  batch 是所含实际 records 的字节和，不丢 H，也不额外发明一份 batch 头。
- remote committed 的固定 metadata 暂为 0；历史 records 的 H 不累积进入长期状态。

Deferred 的四种 profile 统一 `Mstate(w)=K(w)`，包括迁移的 RECOVERY_STATE；不混入原始 INPUT。
`CommittedStateBytes(w)` 保留 eager 兼容接口，带显式 `InputStagingPolicy` 的重载供新路径使用。

图像保持 G1 的 tile/合成文件边界，LLM 保持完整 token；相同 WU 边界去重，不生成零进度 checkpoint。
这是已披露的线性任务/状态预算，不声称能够真实恢复任意压缩器或 LLM 程序。

代表性公式值（B；图像 INPUT=1 GB，LLM=1250 token/500000 WU、请求400 B）：

| 类别 | 10% | 50% | 100% |
|---|---:|---:|---:|
| dense-image | 1000000762 | 1000003814 | 1000007629 |
| sparse-inference | 900186906 | 500934532 | 1869064 |
| compression | 954248130 | 771240653 | 542481307 |
| LLM | 14336000 | 71680000 | 143360000 |

表为精确指定进度下的公式值；真实 checkpoint 先对齐合法应用边界，不强行在恰好10%处产生包。

每池始终 `used + reserved <= capacity`。对象 ID 在池内单调分配、不复用，必须与所属池一起持有。
Reserve 不使状态有效；CommitReservation 仅把相同字节从 reserved 移至 used。
容量不足返回空结果，不改变普通任务；错误释放类型、重复释放、跨任务融合都不会损坏账本。
零字节 committed 对象仍有独立 ID。只计额外保护数据，不计普通 INPUT、RESULT、模型权重或队列。

local 只保留 `(r,l]` 的连续有效增量；在途/乱序记录另以临时对象或 reservation 管理，不冒充有效尾部。
remote 融合峰值为 `old committed + batch`，原地 Merge 后仅剩一个新 committed 对象，
不分配第三份完整对象。LLM 的新状态可能等于该峰值，不能假定每次 commit 都会减少 used。
初始化用 base 和初始状态临时对象，commit 后同样收敛为单一状态。
接管时恢复对象转为 active task state，从备份池释放；任务终态清理全部 used/reserved。
G2 在注册/发包之前预留接收端对象；收到完整流才转 used。
初始化预留失败停止本次保护；L1 预留或传输失败留下不可跳过的缺口；batch 失败保持
已有 local 和 r，并停止后续 batch 尝试，无隐式重试。主计算继续，计算结束即取消在途流、
生成/融合定时器并清空该任务的所有 used/reserved。仿真结束也显式执行同样清理。

## G2 输出与小规模复现

四个输出由 `metrics/core/protection-metrics.h/.cc` 写入普通 outputDir，fixed/compfrr 开启：

| 文件 | 内容 |
|---|---|
| `protection-events.csv` | START、初始化、捕获/生成、接收、commit、清理；每行含 l/r/x 与存储对象身份/池快照 |
| `protection-transfers.csv` | 真实保护流类型、稳定 ID/端口、请求/启动/发送完成/接收/终态时间及实际字节 |
| `protection-task-summary.csv` | 放置、S/W/K、delta/n、cL/cR、有效进度、生成/提交计数和停止原因 |
| `protection-node-storage-summary.csv` | 各计算星备份池容量、used/reserved、各类峰值及容量拒绝次数 |

`transfer-summary.csv` 与 run-summary 的业务应用字节/完成数仅含 INPUT/RESULT；
FlowMonitor、链路负载及路由容量账本仍包含所有真实保护包。真实 sender-finished 时间从
发送器的完成字节/最后发送时刻取得，接收完成与其分列；没有网络 ACK。

四类 fixture 位于 `tests/fixtures/protection/`：16 星、计算星均为 100000 WU/s，
4 个任务依次在主星 3 计算，固定 local=2、remote=0，15 s 仿真、10 Gbit/s、1 ms。
任务 1 为 dense-image：S=52428800 B、W=78644 WU、Kvar=52429200 B，
delta=5%、n=4、每星额外池 10 GB；其余为 sparse-inference、compression、5000-token LLM。
fixture 仅用于执行验收，不改变正式任务场景或 para 默认保护关闭。

## Attempt 与恢复接口

logical task 保持原 task ID、输入/结果字节与首次建立的 deadline；execution attempt 用
`(task_id,generation)` 区分 PRIMARY=0 / RECOVERY=1。G1 独立守卫不是旧 TaskRuntime 状态机的替代。
TaskCoordinator 中主失败先提供恢复机会，再决定 logical FAILED；
旧 attempt 的完成/包/事件不能复活任务。唯一合法计算完成进入 RESULT，唯一 RESULT 交付完成任务。
deadline 不因恢复重置，同 ns 算完按既有合同视为按时；过期失败，恢复仍需实际服务时间与队列统计。

故障当刻先让已有 mechanism 尝试接受 checkpoint recovery；无人接受才交给 policy 的
RECOMPUTE fallback。没有远端 base（含 INITIALIZING 未完成）时，local 增量不能独自恢复。
有远端 base 时，只比较当前可知的 tail/redo 估计；tail 严格更小才选它，相等选择 redo。
只执行一条；估计和实际耗时分列，不能事后取两个实际结果的最小值冒充执行结果。
估计复用真实路由/准入的只读查询，使用当前传播时延、准入速率和 payload 序列化时间；
不预测未来队列释放，不保证与实际 UDP 耗时相等。tail 加 cR 和 `(xf-lf)/恢复速率`，
redo 为 `(xf-rf)/恢复速率`；上述是 eager，deferred 的 INPUT 依赖如下。

### Deferred 故障依赖

每个 accepted recovery 向**实际恢复节点**请求一次完整原始 INPUT（原 source 发出），
不是剩余输入，也不是先发旧 remote 再转发。INPUT 属于临时业务依赖，不进额外备份池。

| 路径 | 可并行发起的故障期数据 | 启动恢复计算前的条件 |
|---|---|---|
| REMOTE_REDO | INPUT | INPUT 接收；已有状态无需再融合 |
| TAIL | INPUT、TAIL | INPUT 接收且 TAIL 接收后一次 cR 完成 |
| RECOMPUTE | INPUT | INPUT 接收；从 0 WU 重算，不搬运检查点 |
| MIGRATE_REDO | INPUT、STATE | 两者接收，无额外 cR |
| MIGRATE_TAIL | INPUT、STATE、TAIL | INPUT 接收且 STATE/TAIL 收齐后一次 cR 完成 |

流在同一决策时刻请求，由公共网络准入/竞争决定实际排队。恢复路径估计用当前各依赖的
`max(INPUT, state/tail/merge)+redo`，不预测未来队列；与频率层的保守加法估计分开。
INPUT 来源星在接收完成前发生 F3 会终止本次恢复，完成后不再是该 INPUT 的依赖。
保留现有 F1/F2 恢复 attempt 免疫、同纳秒 fault batch、原 deadline、唯一终态与清理合同。
reserved-idle 只计接受到实际计算/终止的一段等待，不把并行流耗时重复相加。

Deferred 新增 `input-staging-summary.json` 的全网同时 used+reserved 峰值，
以及恢复表中的 `state_ready_time_ns`、`planned_fault_input_wait_ns`；旧 eager CSV 不改列。
`tests/integration/regression/analyze-input-deferred.py` 另行输出 INPUT 逻辑/物理字节、
常态/故障期额外流量、依赖关键路径等待、存储和四 profile 对比；浪费复用 PR #97 修正后的
actual 执行口径，不把 raw recovery 表中的旧机制诊断列直接当跨方案总浪费。

### 远端忙与迁移

remote 优先使用原固定备份节点。已有有效 committed state、原 remote 忙或计算不可用但
整星/存储仍可读时，先按稳定 ID 寻找非主星、健康空闲、结果可达且存储/路径/deadline 可行的迁移目标。
`MIGRATE_REDO` 实际传输对应 INPUT 策略的 `CommittedStateBytes(rf, policy)` 后从 rf 重做；`MIGRATE_TAIL` 同时注册
state 和真实 L1 记录之和（含 H）的 tail 传输，两者收齐后等一次 cR，再从 lf 开始计算。
目标先预留 state/tail 存储，旧 checkpoint 保留到目标状态有效并接管，或 logical task 终态清理。
默认 relocate 下可行 checkpoint 优先于零起点重算，即使后者估计略快；全部 checkpoint 选项不可行才 RECOMPUTE。
若配置 recompute，**只有** `checkpointFallbackReason=REMOTE_BUSY` 跳过迁移，
直接走原始 INPUT 的零起点重算；REMOTE_UNAVAILABLE/PATH_UNAVAILABLE 等分支维持原行为，
REMOTE_F3 不可读的状态仍不能迁移。两种操作共享基础候选生成，但迁移额外检查 committed state
的传输、存储和 deadline，重算使用原 source INPUT；当前重算准入后的 INPUT 可真实等待网络容量。
已接受的迁移若真实传输失败，按现有单次恢复合同终止，不偷偷重新选择第二个 attempt。
迁移等待计 reserved-idle，RECOVERY_STATE 计真实网络开销；LLM 在 rf=0 的零字节 state
仅使用 1 ns 因果边界，不创建虚假 UDP。诊断记录 old/new node、state bytes、迁移触发/失败原因、
三种候选估计和实际收齐时刻。它是恢复机制，不改变 N5B 的 FFP/LRL 放置排序。

无可用 checkpoint 时按稳定 ID 选非主星的健康、空闲、可达节点重算。
不会为了产生 UDP 排除 source 或 result。接受后等待 INPUT/tail/cR 时处于 reserved-idle，
普通任务可入队但不能抢占；该等待不计 compute busy。catchup 是真实服务达到故障时 xf 的事件，
并非“开始恢复”或“算完整个任务”。重做 WU 与 catchup 后的正常剩余 WU 分列。

### 同星 LocalDelivery 与 RESULT

- `source == recovery`：RECOVERY_INPUT 本地就绪，不创建 UDP、transfer ID 或网络开销。
- `recovery == result`：计算完成后本地交付实际 `output_bytes`，记录 LOCAL、交付时刻与 logical completion。
- 其余情况全部走原 NetworkTransferEngine；该引擎仍拒绝同星传输，未放宽其异星合同。
- 原 `2*T` RESULT 保留取消历史；跨星 winning RESULT 使用新确定性业务 ID，从实际 recovery node 发出。
  业务 transfer 表保留物理历史，因此旧 RESULT 的取消不代表已恢复的 logical task 失败。

同星交付采用 1 ns 因果阶段边界以重新检查 attempt/F3；它不是 UDP 时延，也不计 cL/cR 或网络成本。
`recovery-summary.csv` 记录真实结果字节、delivery mode、logical completion；本地交付的 transfer ID 留空。
`recovery-events.csv` 保存逐事件交付方式；G3 事件也进入 `protection-events.csv` 的 generation=1 行。
快照含有效对象 ID、pending/in-flight 身份、cR-pending 状态、原 deadline；不适用的时刻/估计留空。
`task-summary.csv` 中恢复任务的 compute service 是两次 attempt 的实际服务之和，不含恢复等待；
compute stage elapsed 仍是从首次开始到完成/失败的墙钟时间。

接管前必须在聚合同 ns 故障后重新检查节点健康和空闲并锁定真实恢复服务；只“选中”不免疫。
Accepted recovery 的 RECOVERING/RUNNING_BACKUP 仅忽略后续 F1/F2 对该 attempt 的中断。
故障模型、温度、轨道、RNG 和真实故障事件仍推进，普通排队任务不获得免疫。
F3 始终终止恢复，不做二次恢复。计算完成立即结束免疫，RESULT 遵循原通信与整星故障规则。
attempt 的执行资格与节点对普通队列的可用性分开，不能通过清除节点故障实现免疫。

## 同纳秒与取消

`BeforeFault(t)` 只返回严格早于 t 的有效状态，L1/初始化/RemoteCommit 在 t 时生效的记录
不计入 t 时故障恢复，即使回调恰好先执行。Stop 后取消未完成逻辑操作，迟到回调不推进状态。
G3 将 RemoteCommit 的物理融合/旧记录清理延后 1 ns，名义有效时间不变；同刻故障因此仍持有
旧 committed 与完整旧 tail，既不额外复制整份状态，也不只回滚数字。G3 的 commit 事件行可能仍显示
清理前的池占用；G2 无故障路径仍原时刻融合并记录清理后占用。
故障先冻结并保留所有可能使用的对象、quiesce 旧操作，再在下一纳秒（通信 overlay 已应用）
锁定恢复节点并释放最终不需要的对象。这 1 ns 裁决阶段不读取未来故障日程。
正常完成/保护放弃走 Stop 全清理；QuiesceForRecovery 不清空有效备份。
受控测试反转同纳秒 fault/commit UID，检查相同实体对象、有效进度和最终结果时间；
另检查同 ns deadline 完成与 stale primary 回调。

## 资源账本与 Pre-N5C 冻结口径

跨方案计算资源指标统一标注 **eq-WU（equivalent cost）**：

- `W_execution`：全部物理 attempt 的实际执行 WU，减去每个成功逻辑任务一份有效 W；
  成功任务的多执行与失败任务的全部已执行量分列，在资源汇总中按等值 eq-WU 记账。
- `W_normal`：已完成保护事件的等价成本，单位 eq-WU；cL/cR 不占主 ComputeService，
  不能将这部分声称为测得的额外 CPU 执行量。
- `W_reserved_idle`：真实恢复/副本预留期间未计算的容量机会成本，单位 eq-WU；
  不表示当时必然存在被阻塞的其他任务，更不是实际 CPU 重算。
- **Active equivalent compute overhead**：`W_active = W_execution + W_normal`（eq-WU）。
- **Total compute-capacity equivalent waste**：
  `W_total = W_active + W_reserved_idle`（eq-WU），对应统一 analyzer 的 `w_waste_actual`。

planned 不补 actual；catchup/post-catchup 是执行剖面，不再次加到 `W_total`。
原始任务工作量、已完成/检查点进度仍是整数 WU，网络与存储仍用 Byte，不改变字段合同或量纲。
故障时已执行量写为 `W_f`，local/remote 有效检查点写为 `W_L/W_R`，
检查点落后量为 `W_f-W_L`、`W_L-W_R`（WU）；比例需要另除以任务总 W。

例如 FA-LRL 的 R5/R7 均完成800任务：`W_active` 为1.152619/1.068858百万eq-WU，
`W_total` 为1.452554/3.056674百万eq-WU。deferred 的主要增加项是预留等待机会成本，
不是更多 CPU 重算。完整冻结证据见
[32组正式报告](../../../docs/n5/reviews/Pre-N5C-placement-baselines-final.md)。

`T_catch` 是故障到恢复计算追平 `W_f` 的时间，不是 RESULT 交付时延；
未追平的任务没有样本，不记为零。比较同时给出样本数、mean/P50/P90/max；
严格逐任务时延比较使用双方成功且故障时刻一致的样本，并说明选择范围。

LRL/FA-LRL 是 N5C 的强 baseline，必须在同一受控合同下公平比较，不预设结果；
不为获得优势调整冻结输入。multi-seed 不是本阶段门槛，结论只适用于受控场景。

### 计划与实际执行

`recovery-summary.csv` 区分 planned 与 actual 三列：catchup_redo、post_catchup、total，单位 WU。
TAIL（含 MIGRATE_TAIL）/REMOTE_REDO（含 MIGRATE_REDO）/RECOMPUTE 的起始进度分别为 lf/rf/0，计划 catch-up 为 `xf-start`，
计划 post 为 `W-xf`，计划 total 为 `W-start`。xf/lf/rf 均为整数 WU，不是百分比。
实际 WU 由 ComputeService 在真实服务完成/取消/停止时保留，使用
`min(planned, floor(actual_service_ns * rate / 1e9))`；正常完成 actual=planned，
失败只计执行前缀。post-catchup 须统计，但不属于重复计算 waste。
若用归一化进度 x_f 表示，Recompute 的 `x_f * W` 仅是 planned full catch-up；
发生再次中断时不能把这个计划值记作 actual。

正常成本只计实际完成事件：初始化 `INIT_STATE_GENERATED*cL + INIT_COST_COMMITTED*cR`；
后续 `L1_GENERATED*cL + REMOTE_COST_COMMITTED*cR`，初始化不再计入后续次数。
尚未完成生成/物理提交的取消操作不按完整 cL/cR 收费；这是事件完成计费，不是假设占用了真实 CPU。
物理提交成本事件与名义 RemoteCommit 区分，同 ns fault 导致未物理提交的操作不计提交成本。
任务保护表保存四项次数、成本 ns、主星速率和 normal eq-WU（equivalent cost）。

N5A 历史机制诊断列为
`normal_protection_eq_wu + recovery_reserved_idle_eq_wu + recovery_catchup_actual_wu`（eq-WU）。
它不包含最终失败任务的全部执行浪费，不能直接当作当前跨方案的 `W_total`；
统一统计使用 `analyze-baseline-evaluation.py` 从实际账本重建上述 `W_execution/W_total`。
normal 用主星速率换算；reserved-idle 用恢复星速率，区间是 accepted 到 compute start，
从未开始则到释放/失败。等待 tail 的 cR 已在该区间内，不重复加一次。
该历史机制诊断不计恢复后的正常剩余计算；当前 `W_total` 仍计入失败任务的全部实际执行量。
唯一 winner 的业务 RESULT 不计入额外备份网络开销。

存储表保留各节点 used/reserved/total 峰值及 final used/reserved；任务表的 local/remote peak
是本任务在该节点的同时 used+reserved 峰值，不拿整个共享池峰值冒充。
`protection-finalization.json` 检查存储、恢复锁、在途 runtime flows、待注册请求/提交/定时器清空，
并列出容量分配失败的任务 ID。fixed 仿真结束还将未终结任务标为 `FAILED/SIMULATION_ENDED`；
off 的原有截断合同不变。

历史机制校验脚本 `tests/integration/regression/analyze-protection-accounting.py` 从真实 CSV 校验并生成
全局、四类任务、恢复路径/终态分组的 planned/actual WU、机制诊断 eq-WU、存储/网络与完成/deadline 统计。
网络分别保留 declared/sent/received，主备份开销使用实际 sent payload；同星交付 network=0，
跨星恢复 RESULT 单列。脚本默认不随平台或 CI 启动，不修改原始 CSV。

测试与指令见 [tests](../tests/README.md)，本门禁证据见
[N5A-G1](../../../docs/n5/reviews/N5A-G1-architecture-storage.md)、
[N5A-G2](../../../docs/n5/reviews/N5A-G2-fixed-backup-path.md)、
[N5A-G3](../../../docs/n5/reviews/N5A-G3-recovery-loop.md)、
[N5A-G4](../../../docs/n5/reviews/N5A-G4-integration-accounting.md)。
