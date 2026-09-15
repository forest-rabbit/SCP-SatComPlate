# CompFRR-F：Frequency 与 INPUT

实现：`policy/compfrr/frequency/` 是唯一求解器，`compfrr-controller.*` 负责在线接线；
`input/input-cost-adapter.*` 只描述成本，中性 INPUT contract 位于 `common/input-contract.*`。
两者平行组成 CompFRR-F，公共 Checkpoint/Recovery/Fixed 不依赖 Frequency。目录见[架构](architecture.md)。

## INPUT policy

对外只有 `--inputPolicy=eager|deferred|selective`，默认 `eager`。
Eager 常态预置完整 INPUT；Deferred 常态只保护状态，故障后获取 INPUT；Selective 在
Deferred 布局上启用独立可选预置。后两者要求 `protectionMode=compfrr`。
内部保留中性 EAGER/DEFERRED layout、optional lifecycle、SER selector 三层，
公共 Checkpoint/Recovery/Fixed 不依赖私有选择器。

- SER break-even：比较 `sum(w_k * min(T_ser, max(0,t_k-t_I)))` 与 `(1-P_F)*T_ser`。

SER 是唯一 production selective admission；最终 CompFRR 组合显式指定
`--inputPolicy=selective`，不自动改变全平台默认值。
旧 NET-ready 已从正式入口退役；历史比较与 `T_net/G_net` 诊断保留，但不能触发 SEND。

`T_ser` 是实际路径估计器向上取整的序列化纳秒，`T_net=T_ser+传播纳秒`；
`w_k=q_k*prod(j<k,1-q_j)`，复用 canonical predictor 的完整采样窗口，保留 first sample/finishExclusive。
仅严格大于才 SEND，等于则 DEFER，无经验 epsilon、阈值、profile 特判或第二套 Frequency。
收益是该规则的 INPUT 就绪时间代理量，不保证真实恢复一定加速，也不引入 A/barrier 预测器。

快照在 post-batch revalidation 通过、actual pair 固定后、START_CHECKPOINT 前冻结；
只有初始化真实准入后才请求预取。一次 START 最多尝试一次，不做 epoch 重试或 JIT。
同星不套用网络比值，直接尝试零网络成本的 LocalDelivery，仍预留逻辑 INPUT 空间。

执行由中性 `mechanism/input-staging/input-staging-manager.*` 负责。`INPUT_STAGING` 是独立对象，
不进入 committed checkpoint、Kvar、merge 或 ON barrier。真实池容量必须满足；
资源账本按 `max(checkpoint actual, checkpoint quota) + INPUT actual/reserved` 计数，INPUT 不消耗
checkpoint 的维护 quota。额外流量/存储会真实影响共享资源，不人为消除其反馈。

未准入保持 ABSENT；已建立流失败保留已发字节且不自动重发。共享 Recovery 只依赖
`common/input-dependency.h`：候选检查无副作用，最终 target 接受后才 adopt/handoff。
同目标 READY 复用对象；IN_FLIGHT 用当前已建立流的路径/速率、实际已发量估计剩余时间，
仅供可行性与路径选择，绝不据此调度 compute start。计算必须等待真实 receiver completion
和 state/tail join。READY 复用保留原接收时间，可能早于 recovery acceptance。
同目标完整 INPUT 不再要求源星存活或新流可准入；holder F3 仍使其失效。
异目标或失败预取分别记录 `WRONG_TARGET_REFETCH` / `FAILED_PREFETCH_REFETCH`，合法重新获取不是 duplicate bug。
跨星 REQUESTED 即使已有注册 ID，只要尚未真实准入就仍按 FETCH 评估，不能用 1 ns 伪装在途等待；
诊断 `PREFETCH_NOT_ESTABLISHED` 不属于失败 refetch。最终接受 FETCH 才取消 pending 请求与对象，
未注册请求由 guard 阻止后续建立；同星 pending 仍等待已有的 LocalDelivery 事件，不创建网络流。

Selective 追加 `input-admission-decisions.csv`、`input-prefetch-events.csv`、
`input-prefetch-summary.json`；Eager/Deferred 不产生这些预取专用记录。
最小因果值对象在 `input/selective-input-snapshot.*`，不再导出开发用大 START JSON。
CSV 中 `policy=ser-break-even` 是冻结算法标识，不是第二个配置开关；`T_net/G_net` 仅作兼容诊断。
`B_prefetch_total` 覆盖同一 proactive flow 的完整生命周期，包含故障后续传；used/unused 同口径。
`PREFETCH_USED` 只在真实恢复计算开始时确认，不在 READY/acceptance 时确认。
字节分类互斥优先级为 USED、WRONG_TARGET、FAILED_OR_CANCELLED、NO_FAULT、NOT_CONSUMED；
同时保留独立原因标志。正常/故障后发送按实际 fault snapshot 分界，网络总量每条流只算一次。
每节点/全局 storage peak 为同时占用峰值，结束必须 used/reserved=0。

实现与开发 run11 证据见[执行审计](../n5/reviews/CompFRR-input-binary-admission-runtime.md)。

`compfrr` 默认组合 `CompFrrFrequencyPolicy + FA-FFP`；历史 A/B/C 名称按当时报告解释。
频率求解器独立实现数学公式，不调用验证目录；只有测试将同输入送入旧 shadow 比较。
`FrequencyInput` 是当前状态的只读数值快照，FFP 先给出节点，F 适配层再提供主/恢复算力、
输入/备份路径估计和 storage headroom。cL/cR 从唯一 `GetProtectionCosts(Kvar)` 取得。
每个候选的额外 local/remote 峰值由必填的纯 `storageDemand` 提供，和真实池 free bytes 比较；
不得重复扣除当前已用/已预留状态。未提供估计器会拒绝输入，不默认当成容量无限。
实际库存和池快照由 F controller 接入同一求解器；不复制第二套求解逻辑。

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
ON 无可行新配置时保留状态和最后 committed 配置，不允许 ON→OFF。
真正的策略 PAUSE（例如 deadline 不可行）暂停新 target/batch；路径、存储等暂时资源拒绝是
`RESOURCE_HOLD`，不连带停止另一层可执行的维护，也不能解除此前真正的策略 PAUSE。
两种情况下，已创建的 generation、传输、merge 均按原生命周期继续。

正式算法比较采用在线 **generate**。固定输入和配对 seed/run 不保证不同策略
得到同一故障序列：恢复计算改变负载与温度是 F1 闭环的一部分。N5A 的 validation-replay
仅保留执行验收用途，不增加回放预测器，不覆盖已有 G4 输出。默认仍为 off；
仅显式 `protectionMode=compfrr` 输出 `frequency-decisions.csv`，不依赖 `faultProbabilityAudit`。

## 运行时边界与存储估计

故障引擎提供成对的同步回调：当前 q 产生后、独立 F1/F2 抽样前提出决策；
整个同纳秒故障批次、节点与路由状态应用后才 Resolve。F3 时间表不传给策略；
同轮 F3 仍遵守原有“不抽 F1/F2”的规则，在决策记录中标记 `actual_fault_sampled=0`。
START 存活才调用实际初始化，只有物理初始化对象完成融合才进入 ON；预测 T_init 不调度 ON。

当前在 primary `TASK_RUNNING` 时立即评估 OFF→START，不等待下一个故障检查点。
该只读预测从下一真实全局抽样点开始，不新增抽样；同刻检查尚未开始则包含当前点，
已开始则排除，预计完成时刻不再抽样。未启动的任务仍在后续检查点重新评估。
即时 START 只进入 INITIALIZING；同纳秒重复决策被去重。故障检查时 ON 的 UPDATE/PAUSE 沿用上述
提案→抽样→存活提交合同；容量释放仅做非抽样的资源重评。`decision_trigger` 区分 TASK_RUNNING / FAULT_EPOCH / CAPACITY_RELEASE；
两个非抽样触发的 `q_current_sample` 留空，风险写入 `p_f1_snapshot/p_f2_snapshot/q_comp_snapshot`。
F3 实际时刻的 F1/F2 因果快照另写 `f3-compute-risk-snapshots.csv`，不额外抽样。

各 placement 的 OFF 候选不预留资源，START 存活后固定节点对；ON 不换节点。
START 仍要求 local/remote 健康且 ComputeService 空闲；recovery compute 准入也不变。
ON maintenance 不要求 local/remote ComputeService idle/available：F1/F2 的计算不可用不等于存储失效。
local capture 和 remote batch 分别检查整星存活、对应路径、实际存储及既有配额；F3 对象失效不变。
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
仍被容量阻塞则继续等待；其他原因不因容量释放额外重跑 Frequency。
独立维护操作可在真实容量释放、存储 cleanup 或完整 fault epoch 后重试，使用已提交配置，不新增求解或故障抽样。
可行时提交 UPDATE，记录 `RESUME_AFTER_CAPACITY_RELEASE`，只恢复未来 target/batch，不补造历史检查点。
同纳秒故障批次处理完毕后已进入恢复或终态的任务不重试；重复通知合并，每任务每纳秒最多重试一次。
`capacity_retry_success` 表示 OFF 的 START 或 ON 的 UPDATE 成功，按 `phase_before` 区分；ON 暂停时长
记录在 `frequency-pause-intervals.csv`，`resource_hold=1` 区分配置资源阻塞与真正策略 PAUSE，
不作为额外计算占用。`frequency-decisions.csv` 的 `maintenance_resource_hold` 标明实际提交的资源保留。
非抽样重试的评分使用 `q_comp_snapshot`。

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

维护事件 `CAPTURE_BLOCKED_PATH/STORAGE`、`REMOTE_BATCH_BLOCKED_PATH/STORAGE` 及对应 `RESUMED`
独立记录两条流水线的资源阻塞；`FREQUENCY_RESOURCE_HOLD` 不等于 `FREQUENCY_PAUSED`。
local record 在捕获前预留完整增量（含 H），成功后才推进 immutable sequence 并计 cL。
拒绝时不前移 triggered；恢复时从真实当前进度选择未来合法边界，不补造过去快照。
已生成待注册的记录保留原对象及序列，路径恢复后只注册一次。已注册的真实终结失败保留实际已发送字节，
相应流水线标记 `TRANSFER_FAILED`，不在 UPDATE/epoch 自动重发；L1 缺失不能被后续接收跳过，
remote 流失败不禁止继续 local capture。真正的策略 PAUSE 不取消已创建请求，F3/任务终止则依原合同清理。
CompFRR-P quota 更新仍替换旧峰值，结束释放；每次维护同时遵守实际容量、其他任务承诺和本任务 remote 峰值，
不将 actual 和 quota 重复相加。现有 INPUT + remote state + local tail 并行恢复链路完全保持。

旧结果只有维护轨迹与资源账本均等价才可复用；没有后续故障不能证明未受影响。
历史修复审计与验证见 [维护语义审计](../n5/reviews/Checkpoint-maintenance-semantics-audit.md)。

`frequency-decisions.csv` 分开记录当前/提议/实际提交的 delta/n、q/P_finish、FFP/空闲字节/速率、
J、Rbar、Rmax、初始化估计、额外存储峰值、实际故障和提交结果。不适用字段留空；
OFF 的 NONE 仍可包含最优候选用于解释为何不启动。真实网络字节、cL/cR、恢复及 W_waste
仍只在既有 protection/recovery 账本中统计。

阶段证据：[G1](../n5/reviews/N5B-G1-frequency-policy.md)、
[G2](../n5/reviews/N5B-G2-dynamic-frequency-runtime.md)、
[G3 正式对照](../n5/reviews/N5B-G3-frequency-evaluation.md)。

## 最终时序合同

cL 是异步生成时间与 equivalent cost，不是 cost-only：

```text
合法边界 -> 路径/存储准入并预留 -> 捕获不可变状态 -> 等 cL -> 开始真实 L1 传输
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
正常成本账本使用[统一记账](reproducibility.md#计划与实际执行)的事件计数口径，生成、接收、提交计数分别记录，
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
初始化预留失败停止本次保护；尚未捕获/注册时的临时路径或存储阻塞不前移进度，按维护事件重试。
已经注册的真实 L1 传输终结失败留下不可跳过的缺口；真实 batch 传输失败保持
已有 local 和 r，并停止后续 batch 尝试，无隐式重传。主计算继续，计算结束即取消在途流、
生成/融合定时器并清空该任务的所有 used/reserved。仿真结束也显式执行同样清理。
