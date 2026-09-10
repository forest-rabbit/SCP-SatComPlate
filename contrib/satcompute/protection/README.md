# 保护与恢复模块（N5A）

N5A 回答“怎样执行保护”，N5B 才决定启动/频率，N5C 才优化节点选择。
当前停在 **G1 架构与存储合同**：以下组件可以独立编译、测试，但尚未绑定
TaskCoordinator、ComputeService 或 NetworkTransferEngine，不声称已经救回真实任务。
G2 接真实无故障备份流，G3 接故障恢复，G4 集成验收；每个门禁后等待用户确认。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `common/protection-types.h/.cc` | 动作、因果上下文、attempt 身份/免疫/终态守卫、恢复路径估计选择 |
| `common/task-state-adapter.h/.cc` | 独立的生产 G1 布局、整数 WU/状态/H/合法边界映射及唯一生产成本档位 |
| `storage/backup-storage-pool.h/.cc` | 每节点额外备份容量、used/reserved、原地融合、按任务清理及峰值 |
| `runtime/protection-runtime.h/.cc` | Policy/Mechanism 窄接口、动作分发、故障接管机会和清理通知 |
| `policy/fixed/fixed-protection-policy.h/.cc` | 首次主计算启动时的一次固定保护、稳定 ID 放置、失败时重算后备动作 |
| `mechanism/checkpoint/checkpoint-progress.h/.cc` | 不发包的纯进度合同：生成延迟、连续接收、融合提交及同纳秒历史查询 |

不建空目录或完整插件框架。G2/G3 才实现具体 checkpoint/recompute executor。
未来 1+1/Multi-tree 增加 mechanism/action，复用 runtime、attempt、真实服务与资源账本。
生产文件不引用 `tools/validation/compfrr-shadow`；测试可单向使用它核对旧布局，
不能拿旧 shadow 的理想网络耗时要求真实备份时序完全一致。

## 参数与当前可运行范围

参数在外层 `para.h/.cc`，CLI 注册/校验在 `satcompute.cc`，不增加完整配置 JSON。

| 参数 | 默认 | 说明 |
|---|---:|---|
| `protectionMode` | `off` | 保持 N4；G1 显式拒绝 `fixed`，避免未接数据流却报告已保护 |
| `backupStorageBytesPerNode` | `10000000000` B | 十进制 10 GB；仅为实验容量，可覆盖，0 可用于存储不足测试 |
| `fixedProtectionDelta` | `0.05` | 5% 增量；千分之一精度，转换后传入纯策略 |
| `fixedProtectionBatchN` | `4` | 4 个连续有效 L1 一批，要求 n>0 且 n×delta≤1 |

G1 不实际创建每星池、备份流或额外 CSV。FixedPolicy 的 selected 标记来自测试上下文；
不做动态概率决策。local 为最小稳定 ID 的健康、空闲（含队列为空）、可达一跳节点；
remote 为排除主星/local 后的最小可行 ID。候选可行不等于存储/带宽已经预留。
多个任务竞争同一候选时，G2/G3 的真实准入仍必须重新检查，不能靠 Pre-N5 的独立存在性替代。

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
正常成本账本保留 `Cinit + N_L*cL + N_R*cR`，生成、接收、提交计数分别记录，G2 接账本时
不得把失败/取消操作冒充已提交保护。真实网络传播、序列化与排队不再额外加一份解析时延。

不实现网络 ACK、重传或第二套网络。RemoteCommit 是内部零字节事件。
G2 将在既有 `NetworkTransferEngine` 中增加运行期注册入口，沿用普通 INPUT/RESULT 的 ID；
额外 transfer 从 `max_normal_transfer_id+1` 按确定性事件创建顺序分配，检查 uint64 耗尽，
并关联 `(task_id,attempt_generation,kind,sequence)`。种类区分 INIT_BASE、INIT_STATE、L1、
REMOTE_BATCH、RECOVERY_TAIL、RECOVERY_INPUT；不能把备份流混进业务吞吐/完成数。
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
G1 测试分别验证池与进度合同，真实回调中的跨组件原子编排属于 G2/G3。

## Attempt 与恢复接口

logical task 保持原 task ID、INPUT/RESULT 和首次建立的 deadline；execution attempt 用
`(task_id,generation)` 区分 PRIMARY=0 / RECOVERY=1。G1 独立守卫不是旧 TaskRuntime 状态机的替代。
G3 在真正 TaskCoordinator 中接入：主失败先提供恢复机会，再决定 logical FAILED；
旧 attempt 的完成/包/事件不能复活任务。唯一合法计算完成进入 RESULT，唯一 RESULT 交付完成任务。
deadline 不因恢复重置，同 ns 算完按既有合同视为按时；过期失败，恢复仍需实际服务时间与队列统计。

故障当刻先让已有 mechanism 尝试接受 checkpoint recovery；无人接受才交给 policy 的
RECOMPUTE fallback。没有远端 base（含 INITIALIZING 未完成）时，local 增量不能独自恢复。
有远端 base 时，只比较当前可知的 tail/redo 估计；tail 严格更小才选它，相等选择 redo。
只执行一条；估计和实际耗时分列，不能事后取两个实际结果的最小值冒充执行结果。
G1 只接收已算好的估计值；真实路径/带宽估计与输入重放在 G3 接入，不读取未来网络或 F3 日程。

接管前必须在聚合同 ns 故障后重新检查节点健康和空闲并锁定真实恢复服务；只“选中”不免疫。
Accepted recovery 的 RECOVERING/RUNNING_BACKUP 仅忽略后续 F1/F2 对该 attempt 的中断。
故障模型、温度、轨道、RNG 和真实故障事件仍推进，普通排队任务不获得免疫。
F3 始终终止恢复，不做二次恢复。计算完成立即结束免疫，RESULT 遵循原通信与整星故障规则。
G3 必须把 attempt 的执行资格与节点对普通队列的可用性分开，不能通过清除节点故障实现免疫。

## 同纳秒与取消

`BeforeFault(t)` 只返回严格早于 t 的有效状态，L1/初始化/RemoteCommit 在 t 时生效的记录
不计入 t 时故障恢复，即使回调恰好先执行。Stop 后取消未完成逻辑操作，迟到回调不推进状态。
G2/G3 接线时还必须确保实际对象不会在本轮故障取快照前被不可逆清理：统一 phase 编排，
或在同刻裁决结束前保留旧对象。G1 历史查询只证明进度选择，不代替真实数据生命周期测试。
同 ns 的主计算完成/故障、模型推进/抽样、接管锁定沿用任务书顺序，用专门测试固定，不能依赖偶然 UID。

测试与指令见 [tests](../tests/README.md)，本门禁证据见
[N5A-G1](../../../docs/n5/reviews/N5A-G1-architecture-storage.md)。
