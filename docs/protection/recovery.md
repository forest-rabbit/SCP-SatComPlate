# Recovery 与可选 Relocation

恢复合同由 `runtime/recovery-controller.*` 编排，依赖中性 `CheckpointRecoveryPort`，
不反向依赖 CompFRR Frequency/P。`mechanism/relocation/` 执行获准的目标预留与 state/tail 传输；
是否调用由 scheme capability 与 recovery policy 决定，CB 主 baseline 不自动继承。
以下为 Fixed/CompFRR 的 checkpoint recovery 合同；其他方案见 [Baselines](baselines.md)。

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
`tests/support/protection/input_staging_audit.py`（旧 CLI 入口转发） 另行输出 INPUT 逻辑/物理字节、
常态/故障期额外流量、依赖关键路径等待、存储和四 profile 对比；浪费复用 PR #97 修正后的
actual 执行口径，不把 raw recovery 表中的旧机制诊断列直接当跨方案总浪费。

### 远端忙与迁移

fixed/compfrr 保持 **remote-first**：原 remote 空闲不代表能按时完成。
direct 与 migration 共用无副作用的 `runtime/checkpoint-recovery-estimate.h`：
在原 compute deadline 前同时容纳“依赖就绪等待 + 追平补算 + 追平后的剩余计算”，
边界相等可行。INPUT/state/tail 仍按真实并行依赖估计，RESULT 不纳入 compute deadline。
只要原 remote 有任一可行 TAIL/REDO 就不迁移；二者都可行选较短 catch，平局选 REDO。
二者都不可行记 `DIRECT_DEADLINE_INFEASIBLE`：relocate 搜索迁移，recompute 保留重算兜底。
原 INPUT 路径不可准入记 `INPUT_PATH_UNAVAILABLE`，检查点仍可读时允许搜索其他目标。
不使用未来队列/故障信息，也不在已接受的恢复失败后重新创建 attempt。
`estimated_remote_redo_ns` / `estimated_tail_ns` 保留 direct 估计，新增
`direct_post_catchup_ns`、`direct_deadline_budget_ns`、`direct_redo_fits`、`direct_tail_fits`
与持久的 `direct_fallback_reason`；未评估为空，负 budget 不是缺失值。
成功迁移仍会清空当前 fallback 错误，但 `checkpoint_relocation_trigger` 和
`direct_fallback_reason` 保留起因，区分忙时迁移和空闲但无法按时完成。

remote 优先使用原固定备份节点。已有有效 committed state、原 remote 忙或计算不可用但
整星/存储仍可读时，先按稳定 ID 寻找非主星、健康空闲、结果可达且存储/路径/deadline 可行的迁移目标。
`MIGRATE_REDO` 实际传输对应 INPUT 策略的 `CommittedStateBytes(rf, policy)` 后从 rf 重做；`MIGRATE_TAIL` 同时注册
state 和真实 L1 记录之和（含 H）的 tail 传输，两者收齐后等一次 cR，再从 lf 开始计算。
目标先预留 state/tail 存储，旧 checkpoint 保留到目标状态有效并接管，或 logical task 终态清理。
默认 relocate 下可行 checkpoint 优先于零起点重算，即使后者估计略快；全部 checkpoint 选项不可行才 RECOMPUTE。
若配置 recompute，`REMOTE_BUSY` 或 `DIRECT_DEADLINE_INFEASIBLE` 跳过迁移，
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
`recovery-events.csv` 保存逐事件交付方式；恢复事件也进入 `protection-events.csv` 的 generation=1 行。
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
