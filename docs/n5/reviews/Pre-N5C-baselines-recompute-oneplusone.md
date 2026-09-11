# Pre-N5C：Recompute 与 1+1 baseline

状态：**统一浪费离线修订通过，STOPPED AT PRE-N5C ACCOUNTING REVISION AUDIT**。
[Draft PR #97](https://github.com/forest-rabbit/SCP-SatComPlate/pull/97)，
分支 `feature/n5-baselines`，base=`n5`；不自动合并，不进入 N5C。

## 身份、合同与实现

N5B [PR #96](https://github.com/forest-rabbit/SCP-SatComPlate/pull/96) 已于 2026-09-11
合入 `n5@2f8e6a96abe6973d70835a91b69ea19c594cc2cc`；
[阶段 CI](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/34557596746)
构建、Python/C++ unit、smoke、maintained regression 全通过。旧 feature 分支已确认可达后清理。
`main@009788ca9c5042160e50a014c6d657e785f225f3` 不变。

六组执行 HEAD 均为 **`cfb7a0498d0f1d2182809372c12267cdf942b8c4`**，
每组开始时工作区干净；后续修订仅涉及离线分析器、其单元测试和本报告，审阅 HEAD 以 PR 为准。
本轮没有重新运行仿真，原始 R0–R5 CSV/JSON 保持只读，执行身份不变。
Recompute 增量为 `77c9d23a7`，1+1 与统一比较门禁为 `cfb7a0498`。

冻结语义：

- off：无常态保护、无故障恢复；完整 Recompute：无常态保护，故障后真实重传原 INPUT，从零计算。
- 1+1：首次主 TASK_RUNNING 一次性申请一个资源受限副本，主任务不等待；未准入不重试，不创建第三副本。
- 正常副本承受 F1/F2/F3；同纳秒全部故障和通信拓扑处理完毕后才允许幸存副本 takeover。
  此后沿用已接受恢复的 F1/F2 免疫，F3 始终有效。
- 维持原绝对 compute deadline，不改为结果交付 deadline。按时计算后的 RESULT 可继续传输；
  首个有效交付获胜，取消另一 attempt，但保留其实际 WU 和已发字节。
- planned 取决策时估计，actual 仅取真实执行/预留时长。失败任务的全部实际执行计入 waste；仅成功任务扣除一份有效 W，不生成负开销。
- 同星 INPUT/RESULT 使用 LocalDelivery；跨星统一走 NetworkTransferEngine。没有 scheme-specific Routing。
- 两种新 baseline 仅支持 FFP；fixed/compfrr 继续支持 FFP/LRL；n5c 明确 NOT_IMPLEMENTED。
  remoteBusyRecoveryPolicy 只切换 checkpoint 的 REMOTE_BUSY，对 off/完整 Recompute/1+1 不适用。

架构仍为 `Protection Scheme → Placement Policy → Recovery Policy → Shared Runtime`。
两种 baseline 均调用公共 `SelectBackupNode`：健康、空闲、非 primary、结果可达，
再检查本操作的 INPUT 准入及 deadline 可行性；没有另写 Replica/Recompute PlacementPolicy，
也不要求与双节点 checkpoint placement 拥有相同最终候选集。

| 主要改动文件（相对 contrib/satcompute） | 职责 |
|---|---|
| `protection/policy/baseline/recompute/`、`protection/runtime/recompute-controller.*` | 独立无常态保护策略；复用 RecoveryController、FFP、真实 INPUT/计算/RESULT |
| `protection/policy/baseline/one-plus-one/`、`protection/runtime/one-plus-one-controller.*`、`protection/mechanism/replication/replica-manager.*` | 一次性副本申请、独立物理 attempt、完整 fault batch 后接管与唯一 winner |
| `task/compute-service.*`、`task/task-coordinator.*`、`fault/runtime/fault-controller.cc` | 真实并行服务、逻辑终态、原 compute-deadline 和 batch 完成钩子 |
| `traffic/network-transfer-engine.*`、`metrics/core/{recovery,replica,protection,task}-metrics.cc` | 沿用路由/准入；业务与额外流分类、实际执行及物理流账本 |
| `para.h/.cc`、`satcompute.cc`、`CMakeLists.txt` | 类型化入口、CLI 合法性和构建；不新增 JSON 配置真值源 |
| `tests/unit/*baseline*`、`tests/integration/smoke/run-baseline-smoke.py`、`tests/integration/regression/{run-final-scenario,analyze-baseline-evaluation}.py` | 聚焦/维护验证、冻结 runner、统一离线比较 |

完整 Recompute 和 1+1 仅复用 CheckpointManager 的零容量 canonical 流登记账本，
不创建 local/remote checkpoint object，不分配备份池字节。
完整 Recompute 的 planned INPUT 等待另列；旧 checkpoint CSV 保持不变以支持严格 R5 比较。
模块合同见 [protection README](../../../contrib/satcompute/protection/README.md)。

## 验证与冻结场景

采用增量实现与逐级验证：Recompute 专项 **278** 项、1+1 专项 **681** 项检查；
最终执行提交上的 **85 项 Python、23 个 C++ 程序、11 组 smoke、4 组 regression 全通过**。
覆盖双向 winner、正常副本故障、同纳秒双方故障及 ID 逆序、INPUT 期间故障等待、
takeover 后免疫/F3、真实 LocalDelivery/UDP、loser 字节、deadline/仿真结束截断、
不重试准入、无第三副本、确定性和资源清理。无故障 Recompute 的业务 CSV 与 off 一致。
N4B 联合 fixture 仍为 88 完成/12 失败、483 条模型/预测概率匹配。
本地证据：`output/n5-baselines-validation/*-final.log`；维护范围和入口见
[tests README](../../../contrib/satcompute/tests/README.md)。没有启用 ns-3 上游 examples/tests。

正式场景保持 LEO-66（6×11）、800 tasks、1300 s、每星 100,000 WU/s、
10 Gbps、fixed 1 ms/link、deadline factor=1.3、20 s 网络更新；
global-capacity-aware-hrw、size-aware、online generate、seed=1/run=11、routing seed=1。
四类任务数为 240/240/240/80；INPUT=194,119,753,287 B，RESULT=100,168,131,855 B，
W=352,513,119 WU。task 120 仍为 compression、800,000,000 B、1,200,000 WU、
RESULT=433,985,046 B、arrival=1024.682825747 s；F3 为 node 62 @1027.055770726 s。
没有修改 workload、故障、cL/cR、delta/n 搜索空间、容量、路由或随机种子。

| 组 | Protection Scheme | Placement | REMOTE_BUSY |
|---|---|---|---|
| R0 | 完整 Recompute | FFP | 不适用 |
| R1 | 真实 1+1 | FFP | 不适用 |
| R2 | Fixed，delta=5%，n=4 | FFP | recompute |
| R3 | Fixed，delta=5%，n=4 | FFP | relocate |
| R4 | CompFRR 动态频率 | FFP | recompute |
| R5 | CompFRR 动态频率 | FFP | relocate |

先完成 R5 门禁，再依次 R0–R4，全部退出码 0、完整运行至 1300 s。
R5 对冻结 `output/n5b-architecture/B-ffp-compfrr-frequency`：
**25 份 CSV 逐字节一致，6 份 JSON 一致**，只排除 run-summary 的 wall_clock_ns/s。
800/800 按时、83/83 恢复、2,344,498.5932 eq-WU、226,079,890,635 B 额外流量完全复现。
六组壁钟时间（R0–R5）为 522.37 / 904.28 / 2,258.3 / 2,251.7 / 1,087.35 / 1,111.69 s。

## 成功、故障与计算开销

| 组 | 按时完成 / 失败 | 执行 deadline 超时 | 恢复申请 / 准入 / 成功 | F1 / F2 / F3 | 直接受影响任务 |
|---|---|---|---|---|---|
| R0 | 742 / 58 | 0 | 83 / 25 / 25 | 84 / 2 / 1 | 83 |
| R1 | 800 / 0 | 0 | 83 / 83 / 83 | 88 / 2 / 1 | 86 |
| R2 | 798 / 2 | 2 | 83 / 83 / 81 | 84 / 2 / 1 | 83 |
| R3 | 800 / 0 | 0 | 83 / 83 / 83 | 84 / 2 / 1 | 83 |
| R4 | 797 / 3 | 3 | 83 / 83 / 80 | 84 / 2 / 1 | 83 |
| R5 | 800 / 0 | 0 | 83 / 83 / 83 | 84 / 2 / 1 | 83 |

完成均满足原 compute deadline。恢复失败数（申请减成功）依次为 58/0/2/0/3/0。
R0 的 58 次为 `NO_FEASIBLE_RECOMPUTE_NODE_INPUT_OR_DEADLINE`：
逐条核对后，**全部即使忽略 INPUT 也没有足够时间在任一 100,000 WU/s 节点从零算完**。
它们在准入时失败，不是运行至 deadline 才超时，因此表中“执行 deadline 超时”为 0。
R0 的低网络流量伴随 58 个任务失败，不能解释为同等完成率下的成本优势。

R1 的恢复统计指主 attempt 故障后的接管；86 个直接受影响任务包含 83 个主故障和
3 个正常副本故障，后者由主任务完成。故障数按来源报告，不强求相同 seed 产生相同最终 trace。

本轮修订统一计算容量浪费，而不是改变机制或总执行量：

```text
task_execution_waste_wu
  = 所有物理 attempt 的 actual WU 总和 - 成功逻辑任务的有效 WU
w_waste_actual
  = task_execution_waste_wu + normal_protection_eq_wu + reserved_idle_eq_wu
```

成功严格取 `final_state == COMPLETED && compute_deadline_met == 1`；
按时计算后的 RESULT 可以晚于 compute deadline 交付。失败任务不扣任何有效 W。
normal 沿用实际完成的 cL/cR 事件等价计费；idle 沿用真实 reservation 等待，
fault-time cR 已在 idle 中，不重复加费。这是 eq-WU 容量浪费，不是纯 CPU 执行量或总资源消耗；
网络 Byte、备份池峰值 Byte 仍独立报告。

旧汇总漏掉了最终失败任务已执行却未形成有效结果的计算。本次从既有因果执行记录离线重建：
`W_waste_actual has been recomputed offline from existing causal execution ledgers; no simulation was rerun.`

| 组 | 成功任务额外执行 WU | 失败任务全部执行 WU | task execution waste WU | normal eq-WU | actual idle eq-WU | W_waste_actual eq-WU |
|---|---:|---:|---:|---:|---:|---:|
| R0 | 2,197,227 | 21,673,522 | 23,870,749 | 0 | 522,006.8628 | 24,392,755.8628 |
| R1 | 321,576,575 | 0 | 321,576,575 | 0 | 15,721,457.8606 | 337,298,032.8606 |
| R2 | 1,466,166 | 1,361,062 | 2,827,228 | 1,523,120 | 405,774.7797 | 4,756,122.7797 |
| R3 | 1,394,484 | 0 | 1,394,484 | 1,523,120 | 368,068.7436 | 3,285,672.7436 |
| R4 | 1,354,057 | 2,426,686 | 3,780,743 | 444,770 | 573,665.9036 | 4,799,178.9036 |
| R5 | 1,355,313 | 0 | 1,355,313 | 441,510 | 547,675.5932 | 2,344,498.5932 |

每组 task execution waste 等于前两列之和；normal/idle 只加一次。
原始 `recovery-summary.csv`、`replica-summary.csv` 的旧 waste 字段不改写，
只作为历史机制账本校验，跨方案结论统一取新 `evaluation.json`。

实际执行重建与门禁：

- 恢复任务：primary 取故障快照 `actual_work_units`，与 fault-task-impact 的实际进度和
  连续计算时间交叉核对；recovery 取 `actual_total_recovery_wu` 和实际 service。
  不能把被恢复 attempt 覆写的逻辑 compute_service_time 当作原 primary 服务。
- 1+1：逐项累加 `replica-attempts.csv` 的真实 WU/service，与副本汇总一致；
  普通未故障任务用实际 compute 起止时间。任何缺失或冲突的 actual 证据立即停止并报 task_id。
- 六组所有 attempt 的 actual service ns 总和均与 `compute-node-summary.csv` 的 busy ns 精确一致。
  没有从 planned/estimated 字段补 actual；逐 profile 聚合与全局守恒。
- R0：83 个故障任务实际前缀 **23,870,749 WU**；其中 58 个未恢复任务损失
  **21,673,522 WU**，25 个成功任务多执行 **2,197,227 WU**。
  加 actual idle **522,006.8628 eq-WU**，强制锚点 **24,392,755.8628 eq-WU** 通过。
- R1：primary **328,177,528** + replica **345,912,166** - 有效任务 **352,513,119**
  = **321,576,575 WU**；加 idle 得 **337,298,032.8606 eq-WU**。
  R3/R5 均 800/800 成功，新 task waste 分别与 actual catchup **1,394,484 / 1,355,313 WU** 相等。

R2/R4 最终失败任务全部实际执行如下；它们均未完成追赶，
故本场景 post-catchup=0，但分析器和离线测试也覆盖“追赶后继续执行再失败”的情况：

| 任务 | 组 | primary actual WU | recovery actual WU | 失败任务全部执行 WU | terminal reason |
|---|---|---:|---:|---:|---|
| 114 | R2 | 701,967 | 212,198 | 914,165 | COMPUTE_DEADLINE_EXCEEDED |
| 252 | R2 | 348,743 | 98,154 | 446,897 | COMPUTE_DEADLINE_EXCEEDED |
| 114 | R4 | 701,967 | 212,198 | 914,165 | COMPUTE_DEADLINE_EXCEEDED |
| 252 | R4 | 348,743 | 98,154 | 446,897 | COMPUTE_DEADLINE_EXCEEDED |
| 475 | R4 | 806,704 | 258,920 | 1,065,624 | COMPUTE_DEADLINE_EXCEEDED |

这些记录的 replica actual 均为 0。R0 的 58 个失败记录 recovery/replica actual 均为 0。
六组 `failed_task_execution` 保留全部 63 条逐任务审计记录
（task/group/scheme、primary/recovery/replica actual、failed execution、terminal reason），
不是只保存汇总。

恢复诊断项另列，**不能再次加入上述统一 waste**：

| 组 | planned 追赶 WU | actual 追赶 WU | actual post-catchup WU | actual 总恢复 WU |
|---|---:|---:|---:|---:|
| R0 | 23,870,749 | 2,197,227 | 11,274,928 | 13,472,155 |
| R1 | — | — | — | — |
| R2 | 2,516,876 | 1,776,518 | 24,294,103 | 26,070,621 |
| R3 | 1,394,484 | 1,394,484 | 24,335,591 | 25,730,075 |
| R4 | 3,211,471 | 1,923,329 | 24,245,762 | 26,169,091 |
| R5 | 1,355,313 | 1,355,313 | 24,335,591 | 25,690,904 |

planned 追赶仅解释恢复难度；actual 追赶仅解释已重复的历史计算。
post-catchup 在成功任务中是有效剩余工作，在最终失败任务中随全部实际执行计入 waste。
R1 的 checkpoint catchup/post-catchup 不适用，不用零值声称“没有恢复工作”。

| 组 | 有等待估计的记录数 | planned 等待 eq-WU | actual 等待 eq-WU | 观察到 T_catch 的记录数 | T_catch P50 / P90 / max（s） |
|---|---|---|---|---|---|
| R0 | 25 | 521,331.9975 | 522,006.8628 | 25 | 1.095459 / 1.959699 / 2.690803 |
| R1 | 800 | 15,698,550.4313 | 15,721,457.8606 | — | — |
| R2 | 83 | 404,998.0675 | 405,774.7797 | 81 | 0.196885 / 0.42292 / 1.502627 |
| R3 | 83 | 363,682.7253 | 368,068.7436 | 83 | 0.196885 / 0.421607 / 0.6063 |
| R4 | 83 | 511,708.8508 | 573,665.9036 | 80 | 0.091916 / 0.439208 / 2.17366 |
| R5 | 83 | 481,778.4213 | 547,675.5932 | 83 | 0.096401 / 0.457682 / 2.17366 |

planned 等待来自当次路径估计；actual 从真实 reservation 到 compute start 或终止计时，
含真实排队/接收/融合等待。完整 Recompute 只有 25 次准入存在完整等待估计；
其余 58 条不伪造 planned 等待，但其已执行 primary WU 仍完整计入 actual waste。
R2 的任务 114/252 计划追赶 701,967/348,743 WU，deadline 截断后只实际执行
212,198/98,154 WU；R4 同样只记已执行前缀。
T_catch 只统计真正达到故障前进度的记录，未追赶完成不是 0 秒。
1+1 沿现有副本进度继续，不伪造 checkpoint T_catch；使用 attempt 的实际时间轴。

## 1+1 专项与网络

800 次首次 TASK_RUNNING 申请，800 次准入（本场景 100%，不是绕过资源门禁）；
primary winner=709，replica winner=91，其中 83 次主故障接管、8 次无主故障但副本 RESULT 先交付。
没有第三副本或失败准入重试。各 attempt 的节点、INPUT/compute/RESULT 时刻、故障及 takeover
保存在 `replica-summary.csv`、`replica-attempts.csv`、`replica-events.csv`。

实际 REPLICA_INPUT=191,061,880,003 B；
实际 REPLICA_RESULT=11,734,550,995 B。
唯一 winner 的网络 RESULT=99,997,203,222 B，
另有本地 RESULT=170,928,633 B、无 UDP。
loser 已发 RESULT=1,147,529,326 B，
其中 primary=406,136,084、
replica=741,393,242 B，全部保留为额外物理开销。

以下 GB 均为十进制。业务/额外按真实 transfer ID 去重；实际发送载荷不是声明大小，
也不是跨 hop 累加的线速字节。核对全体 FlowMonitor 源端 tx_bytes−28×tx_packets
等于此载荷合计；跨链路序列化与时间加权利用率另取 link-summary。

| 组 | 业务 GB | 额外容错 GB | 实际发送载荷合计 GB | 平均链路利用率 % | 可用时段利用率 % |
|---|---|---|---|---|---|
| R0 | 286.087136 | 6.37165 | 292.458786 | 0.355941 | 0.357803 |
| R1 | 294.116957 | 192.209409 | 486.326366 | 0.61851 | 0.621746 |
| R2 | 293.763037 | 484.554403 | 778.31744 | 0.731736 | 0.735565 |
| R3 | 293.893656 | 484.092216 | 777.985872 | 0.730899 | 0.734723 |
| R4 | 293.453806 | 226.493278 | 519.947084 | 0.532432 | 0.535218 |
| R5 | 293.893656 | 226.079891 | 519.973547 | 0.532558 | 0.535345 |

| 实际发送 GB | R0 | R1 | R2 | R3 | R4 | R5 |
|---|---|---|---|---|---|---|
| INPUT | 194.119753 | 194.119753 | 194.119753 | 194.119753 | 194.119753 | 194.119753 |
| RESULT | 91.967383 | 89.410182 | 99.643283 | 99.773903 | 99.334053 | 99.773903 |
| INIT_BASE | 0 | 0 | 193.592491 | 193.592491 | 114.567812 | 114.578247 |
| INIT_STATE | 0 | 0 | 0 | 0 | 24.405058 | 24.405058 |
| L1 | 0 | 0 | 152.936797 | 152.936797 | 54.852086 | 54.751768 |
| REMOTE_BATCH | 0 | 0 | 133.493264 | 133.493264 | 26.840086 | 26.8307 |
| RECOVERY_INPUT | 6.37165 | 0 | 2.244832 | 1.166878 | 2.814861 | 1.166878 |
| RECOVERY_STATE | 0 | 0 | 0 | 0.563387 | 0 | 1.317919 |
| RECOVERY_TAIL | 0 | 0 | 2.287019 | 2.339398 | 3.013375 | 3.02932 |
| REPLICA_INPUT | 0 | 191.06188 | 0 | 0 | 0 | 0 |
| REPLICA_RESULT | 0 | 11.734551 | 0 | 0 | 0 | 0 |

R1 的 RESULT 列含 primary 的 winner/loser，REPLICA_RESULT 列含 replica 的 winner/loser；
业务/额外总表按最终唯一 winner 重分类，不能再次叠加分类表。
`replica-transfers.csv` 补齐 losing primary RESULT；不能只加 protection-transfers，
否则会漏记这部分真实流量。所有跨星流共用原 routing/admission，无新增策略专属路径算法。

## REMOTE_BUSY：重点 R4 对 R5

| 组 | 有效 checkpoint 的 REMOTE_BUSY | 路径 | 成功 / 失败 | actual 追赶 WU | 等待 eq-WU | INPUT / STATE / TAIL 字节 |
|---|---|---|---|---|---|---|
| R2 | 3 | RECOMPUTE×3 | 1 / 2 | 431,885 | 87,522.8149 | 1,077,953,592 / 0 / 0 |
| R3 | 3 | MIGRATE_TAIL×3 | 3 / 0 | 49,851 | 49,916.8631 | 0 / 563,386,814 / 52,379,095 |
| R4 | 4 | RECOMPUTE×4 | 1 / 3 | 690,805 | 133,456.7886 | 1,647,982,992 / 0 / 0 |
| R5 | 4 | MIGRATE_TAIL×3、MIGRATE_REDO×1 | 4 / 0 | 18,194 | 110,750.3788 | 0 / 1,317,919,429 / 44,387,252 |

四次 R4/R5 按 task_id、fault_time_ns、fault_type 一一配对，无未配对事件：

| 任务 | R4 / R5 终态 | R4 T_catch s | R5 T_catch s | R4 / R5 actual 追赶 WU | R4 / R5 等待 eq-WU |
|---|---|---|---|---|---|
| 252 | FAILED / COMPLETED | 未追赶完成（deadline） | 0.159178592 | 98,154 / 5,071 | 19,341.8297 / 10,846.8591 |
| 475 | FAILED / COMPLETED | 未追赶完成（deadline） | 0.332241177 | 258,920 / 5,328 | 45,933.9737 / 27,896.1176 |
| 551 | COMPLETED / COMPLETED | 1.502626751 | 0.335038472 | 121,533 / 1,208 | 28,729.675 / 32,295.8471 |
| 114 | FAILED / COMPLETED | 未追赶完成（deadline） | 0.462985551 | 212,198 / 6,587 | 39,451.3102 / 39,711.555 |

对这四次事件，R4（重算）相比 R5（迁移）：

- 多实际追赶 **672,611 WU**；R5 从 690,805 降至 18,194 WU，降低 97.37%。
- 多预留等待 **22,706.4098 eq-WU**；R5 降低 17.01%。
  并非每个任务都更低，任务 551/114 的迁移等待略长。
- 恢复发送从 1,647,982,992 B 降为 1,362,306,681 B，少 **285,676,311 B**。
- 任务 551 两组都完成追赶，T_catch 从 1.502626751 s 降至 0.335038472 s，快 **1.167588279 s**。
  其余三个在 R4 中尚未完成追赶便 deadline 失败，不能替它们编造有限的实际 T_catch。
- R4 仅 1/4 恢复成功，R5 为 4/4。忙事件 T_catch 的 P50/P90/max：
  R4 仅 1 个可观测样本、均为 1.502626751 s；
  R5 为 4 个样本、0.3336398245/0.4246014273/0.462985551 s。

**整组** R4→R5 的 waste 少 **2,454,680.3104 eq-WU（51.15%）**，
额外网络少 413,387,152 B，并多完成 3 个任务；不能与四个 busy 事件的差额直接混用。
同一频率算法在不同恢复占用下有后续闭环差异：例如同为 583 s 故障的任务 605，
R4 为 TAIL、追赶 1,731 WU，R5 为 REMOTE_REDO、追赶 106,326 WU；
R5 的频率记录出现 PLACEMENT_UNAVAILABLE/PAUSE，而 R4 的 checkpoint 更新不同。
因此“只切换恢复参数”不等于后续频率决策和实际资源轨迹也必须相同。
全局传输载荷 R5 反而多 26,462,700 B，因为还实际交付了三个额外结果；已计入上表。

## 其他对照、存储与 profile

- **R2→R4，Frequency 消融**：常态等价成本降低 70.8%，
  额外网络降低 53.26%，
  但统一总 waste **增加 43,056.1239 eq-WU（0.91%）**，完成数 798→797；
  正常维护成本下降不等于计入失败任务后总浪费也下降。
- **R2→R3，Fixed 下的 relocation**：同三个 busy 任务 114/252/551，从 1/3 成功变为 3/3，
  少 382,034 actual catchup WU、37,605.9518 eq-WU busy 等待、462,187,683 B 恢复字节。
  R3 busy T_catch P50/P90/max=0.394200256/0.4385059648/0.449582392 s（n=3）；
  R2 只有任务 551 达到追赶点（1.502626751 s），另外两个右删失。
  整组 waste 降低 **1,470,450.0361 eq-WU（30.92%）**，包含失败任务损失和 busy 外的等待差异。
- **R3→R5**：均 800/800 完成，统一 waste 降低 **941,174.1504 eq-WU（28.64%）**。
- **R0/R1/R5，整体对照**：R0 常态成本为零，但 58 个晚故障已不具备从零恢复的 deadline 预算。
  R0→R5 的统一 waste 降低 **22,048,257.2696 eq-WU（90.39%）**，完成数 742→800；
  这是包含最终失败损失的实际工程对照，不是两组都成功时的重计算反事实成本。
  R1/R5 均 800/800；R5 的 waste 比 R1 少 99.3%，
  但额外应用层网络载荷高于 R1，不能宣称所有资源维度均更低。
  平均链路利用率与应用字节不是同一量，已分别报告。

| 组 | 单任务 local 最大峰值 B | 单任务 remote 最大峰值 B | 单节点备份池最大峰值 B | 分配失败 |
|---|---|---|---|---|
| R0 | 0 | 0 | 0 | 0 |
| R1 | 0 | 0 | 0 | 0 |
| R2 | 264,929,280 | 1,255,330,802 | 2,704,701,610 | 0 |
| R3 | 264,929,280 | 1,255,330,802 | 2,704,701,610 | 0 |
| R4 | 755,679,232 | 1,754,456,798 | 2,003,345,404 | 0 |
| R5 | 755,679,232 | 1,754,456,798 | 2,039,466,341 | 0 |

六组所有节点 used/reserved、网络路径/传输/链路/速率预留最终归零，
protection-finalization 均 quiescent。每节点完整峰值见原始 66 行 storage CSV。
Recompute/1+1 不占 checkpoint 池；**active working-set storage 未独立量化，
不能据此宣称 1+1 的存储优劣**。

以下各单元格为“完成数 / actual waste eq-WU”；总任务数依次 240/240/240/80：

| 组 | dense-image：完成 / waste eq-WU | sparse-inference：完成 / waste eq-WU | compression：完成 / waste eq-WU | llm：完成 / waste eq-WU |
|---|---|---|---|---|
| R0 | 229 / 3,801,396.1039 | 226 / 5,597,189.8349 | 219 / 9,105,202.3334 | 68 / 5,888,967.5906 |
| R1 | 240 / 96,788,607.8611 | 240 / 88,528,824.7882 | 240 / 96,080,246.0935 | 80 / 55,900,354.1178 |
| R2 | 240 / 673,576.2723 | 239 / 1,466,424.8746 | 239 / 1,538,492.3508 | 80 / 1,077,629.2820 |
| R3 | 240 / 673,576.2723 | 240 / 552,128.5056 | 240 / 982,338.6837 | 80 / 1,077,629.2820 |
| R4 | 240 / 274,453.3199 | 239 / 1,286,308.6106 | 238 / 2,426,151.8050 | 80 / 812,265.1681 |
| R5 | 240 / 273,553.3199 | 240 / 378,980.7711 | 240 / 879,499.3341 | 80 / 812,465.1681 |

## 复现与边界

原始输出在 `output/n5-baselines/R0-recompute-ffp`、
`R1-one-plus-one-ffp`、`R2-fixed-ffp-recompute-busy`、
`R3-fixed-ffp-relocate-busy`、`R4-compfrr-ffp-recompute-busy`、
`R5-compfrr-ffp-relocate-busy`。
`execution.json` 保留完整命令和执行身份；`evaluation.json` 含六组逐任务、
恢复、副本与 busy 明细；新增 actual 执行分解、失败任务明细、审计门禁和六项 waste 百分比。
`R5-frozen-equivalence.json` 含 31 项比较结果，原始物理等价结论不变。
本次只运行离线 analyzer 及其 **15 项单元测试**；未运行 ns-3、C++/smoke/regression 仿真或 CI。
执行前后检查六组原始目录的全部 **192 个文件**，内容未写入，文件集合、大小及修改/变更时间不变。
这些大体积原始文件按 gitignore 留在本地；仓库中的本报告保存汇总，完整指标可按记录的命令复现。

```bash
# 本轮禁止重跑仿真；只读取已经存在的六组原始输出。
.venv/bin/python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_baseline_evaluation.py' -v
.venv/bin/python contrib/satcompute/tests/integration/regression/analyze-baseline-evaluation.py \
  --root output/n5-baselines --output output/n5-baselines/evaluation.json
```

这是一次受控 engineering baseline / mechanism comparison：场景曾为 B 的 task 120 F3 保护行为选定，
不是无偏论文样本，也不是多 seed 显著性验证。online generate 保持相同故障参数与 seed/run，
允许额外负载改变 F1 realization；R1 的 88 次 F1 已体现这一点。
不通过调大任务、改 deadline 或压低故障来改善 baseline 结果。

N5C 的前置接口已就绪：可替换公共 placement 层而不重写两种 baseline 或 Routing。
本轮仅揭示资源竞争、可行性和后续状态的影响，**没有设计或实现 N5C score、risk-aware placement、
Multi-tree 或多 seed 实验**。保留 Draft PR 和 baseline 分支等待用户审阅；
**STOPPED AT PRE-N5C ACCOUNTING REVISION AUDIT，不合并 PR #97，不进入 N5C**。
