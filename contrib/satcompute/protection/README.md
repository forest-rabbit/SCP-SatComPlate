# 保护与恢复模块（N5A）

N5A 回答“怎样执行保护”，N5B 才决定启动/频率，N5C 才优化节点选择。
当前接入单次故障恢复闭环和 **G4 planned/actual 资源账本**：真实备份路径、故障快照、
恢复服务预留、TAIL / REMOTE_REDO / RECOMPUTE 和 winning RESULT。cL/cR 不占用主 ComputeService。
FIXED 正式场景仅为执行验收，不代表 CompFRR 算法效果；N5B/N5C 尚未接入。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `common/protection-types.h/.cc` | 动作、因果上下文、attempt 身份/免疫/终态守卫、恢复路径估计选择 |
| `common/task-state-adapter.h/.cc` | 独立的生产 G1 布局、整数 WU/状态/H/合法边界映射及唯一生产成本档位 |
| `storage/backup-storage-pool.h/.cc` | 每节点额外备份容量、used/reserved、原地融合、按任务清理及峰值 |
| `runtime/protection-runtime.h/.cc` | Policy/Mechanism 窄接口、动作分发、故障接管机会和清理通知 |
| `policy/fixed/fixed-protection-policy.h/.cc` | 首次主计算启动时的一次固定保护、稳定 ID 放置、失败时重算后备动作 |
| `mechanism/checkpoint/checkpoint-progress.h/.cc` | 不发包的纯进度合同：生成延迟、连续接收、融合提交及同纳秒历史查询 |
| `mechanism/checkpoint/checkpoint-manager.h/.cc` | 初始化、L1、batch 真实传输、存储预留/提交及停止清理 |
| `runtime/fixed-protection-controller.h/.cc` | 只读任务事件接线、候选快照、固定策略/机制分发 |
| `runtime/recovery-controller.h/.cc` | 故障裁决、一次恢复、真实输入/尾部/计算/结果及终态清理 |
| `runtime/protection-transfer-key.h` | 同纳秒请求的稳定排序键与不回绕的保护流编号 |
| `../traffic/local-delivery.h/.cc` | 同星逻辑交付；不创建 UDP，不计网络字节 |

不建空目录或完整插件框架。
未来 1+1/Multi-tree 增加 mechanism/action，复用 runtime、attempt、真实服务与资源账本。
生产文件不引用 `tools/validation/compfrr-shadow`；测试可单向使用它核对旧布局，
不能拿旧 shadow 的理想网络耗时要求真实备份时序完全一致。

## 参数与当前可运行范围

参数在外层 `para.h/.cc`，CLI 注册/校验在 `satcompute.cc`，不增加完整配置 JSON。

| 参数 | 默认 | 说明 |
|---|---:|---|
| `protectionMode` | `off` | 保持 N4；`fixed` 支持无故障与在线 generate，要求网络任务、shadow 关闭 |
| `backupStorageBytesPerNode` | `10000000000` B | 十进制 10 GB；仅为实验容量，可覆盖，0 可用于存储不足测试 |
| `fixedProtectionDelta` | `0.05` | 5% 增量；千分之一精度，转换后传入纯策略 |
| `fixedProtectionBatchN` | `4` | 4 个连续有效 L1 一批，要求 n>0 且 n×delta≤1 |

off 不创建保护池、流或 CSV；fixed 对每个首次主计算启动执行一次固定策略，
不做动态概率决策。local 为最小稳定 ID 的健康、空闲（含队列为空）、可达一跳节点；
remote 为排除主星/local 后的最小可行 ID。候选可行不等于存储/带宽已经预留。
多个任务竞争同一候选时，由共享备份池与网络容量准入处理；恢复接管时额外原子锁定空闲服务。

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

初始化同时启动 base 传输和状态生成路径；两条路径都完成后再等 cR。初始变量状态为 0
也必须显式完成初始化；没有变量 payload 时不创建零字节 UDP flow，不靠 `bytes>0` 判断 ON。
正常成本账本使用下文的唯一事件计数口径，生成、接收、提交计数分别记录，
不得把失败/取消操作冒充已提交保护。真实网络传播、序列化与排队不再额外加一份解析时延。

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

- 非 LLM：`K(w)=floor(Kvar*w/W)`，`Mstate(w)=S-floor(S*w/W)+K(w)`。
  剩余原始输入向上保留到整数 B，不采用 `S+K(w)` 或 `min(S,K)`。
- LLM：`K(w)=floor(w/100)*114688 B`，`Mstate(w)=K(w)`；正式 checkpoint 只取完整 token。
- L1：`D_L=K(w_new)-K(w_old)+H`，H 使用既有固定头加十进制 task ID 字节数，LLM H=0。
  batch 是所含实际 records 的字节和，不丢 H，也不额外发明一份 batch 头。
- remote committed 的固定 metadata 暂为 0；历史 records 的 H 不累积进入长期状态。

图像保持 G1 的 tile/合成文件边界，LLM 保持完整 token；相同 WU 边界去重，不生成零进度 checkpoint。
这是已披露的线性任务/状态预算，不声称能够真实恢复任意压缩器或 LLM 程序。

代表性公式值（B；图像 INPUT=1 GB，LLM=5000 token/500000 WU、请求400 B）：

| 类别 | 10% | 50% | 100% |
|---|---:|---:|---:|
| dense-image | 1000000762 | 1000003814 | 1000007629 |
| sparse-inference | 900186906 | 500934532 | 1869064 |
| compression | 954248130 | 771240653 | 542481307 |
| LLM | 57344000 | 286720000 | 573440000 |

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

四个输出由 `metrics/core/protection-metrics.h/.cc` 写入普通 outputDir，仅 fixed 开启：

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
fixture 仅用于执行验收，不改变正式 800 任务场景或 para 默认保护关闭。

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
估计沿当前可达路径的稳定接口顺序，使用当前传播时延、剩余瓶颈容量和 payload 序列化时间；
不预测未来队列释放，不保证与实际 UDP 耗时相等。tail 加 cR 和 `(xf-lf)/恢复速率`，
redo 为 `(xf-rf)/恢复速率`；无可用远端对象时才回退到原 source 的 INPUT 重放。

remote 优先使用原固定备份节点；不可接受时按稳定 ID 选非主星的健康、空闲、可达节点重算。
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

## G4 资源账本

`recovery-summary.csv` 区分 planned 与 actual 三列：catchup_redo、post_catchup、total，单位 WU。
TAIL/REMOTE_REDO/RECOMPUTE 的起始进度分别为 lf/rf/0，计划 catch-up 为 `xf-start`，
计划 post 为 `W-xf`，计划 total 为 `W-start`。xf/lf/rf 均为整数 WU，不是百分比。
实际 WU 由 ComputeService 在真实服务完成/取消/停止时保留，使用
`min(planned, floor(actual_service_ns * rate / 1e9))`；正常完成 actual=planned，
失败只计执行前缀。post-catchup 须统计，但不属于重复计算 waste。

正常成本只计实际完成事件：初始化 `INIT_STATE_GENERATED*cL + INIT_COST_COMMITTED*cR`；
后续 `L1_GENERATED*cL + REMOTE_COST_COMMITTED*cR`，初始化不再计入后续次数。
尚未完成生成/物理提交的取消操作不按完整 cL/cR 收费；这是事件完成计费，不是假设占用了真实 CPU。
物理提交成本事件与名义 RemoteCommit 区分，同 ns fault 导致未物理提交的操作不计提交成本。
任务保护表保存四项次数、成本 ns、主星速率和 normal 等效 WU。

主 waste = `normal_protection_eq_wu + recovery_reserved_idle_eq_wu + recovery_catchup_actual_wu`。
normal 用主星速率换算；reserved-idle 用恢复星速率，区间是 accepted 到 compute start，
从未开始则到释放/失败。等待 tail 的 cR 已在该区间内，不重复加一次。
恢复后的正常剩余计算、业务 RESULT 网络流量均不计入上述 waste/备份网络主项。

存储表保留各节点 used/reserved/total 峰值及 final used/reserved；任务表的 local/remote peak
是本任务在该节点的同时 used+reserved 峰值，不拿整个共享池峰值冒充。
`protection-finalization.json` 检查存储、恢复锁、在途 runtime flows、待注册请求/提交/定时器清空，
并列出容量分配失败的任务 ID。fixed 仿真结束还将未终结任务标为 `FAILED/SIMULATION_ENDED`；
off 的原有截断合同不变。

离线脚本 `tests/integration/regression/analyze-protection-accounting.py` 从真实 CSV 校验并生成
全局、四类任务、恢复路径/终态分组的 planned/actual、waste、存储/网络与完成/deadline 统计。
网络分别保留 declared/sent/received，主备份开销使用实际 sent payload；同星交付 network=0，
跨星恢复 RESULT 单列。脚本默认不随平台或 CI 启动，不修改原始 CSV。

测试与指令见 [tests](../tests/README.md)，本门禁证据见
[N5A-G1](../../../docs/n5/reviews/N5A-G1-architecture-storage.md)、
[N5A-G2](../../../docs/n5/reviews/N5A-G2-fixed-backup-path.md)、
[N5A-G3](../../../docs/n5/reviews/N5A-G3-recovery-loop.md)。
