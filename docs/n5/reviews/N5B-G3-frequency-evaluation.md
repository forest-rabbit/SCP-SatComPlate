# N5B-G3：频率正式对照与节点集中度诊断

> **PRE-REVISION G3 EVIDENCE**：本报告及旧输出保留为修订前证据；R1–R4 修订后的合同与
> 正式结果见 [G3R 修订报告](N5B-G3R-semantic-corrections.md)，不以本报告代替最终验收。

状态：**G3 实现、三组正式运行与统一对账完成，STOPPED AT N5B-G3，待用户审阅**。
这是单个 paired seed/run 的权衡证据，不是“CompFRR 全面优于 fixed”的结论。

## 1. 授权与版本

- 分支 `feature/n5b-compfrr-frequency`，同一 Draft PR #96，base=`n5`。
- G2 审阅通过：`547520d809bab89523b09557ba38dd0f6b7a750b`。
- `n5` 基线：`b626a154d423219bc1503f060252962e1965b4cf`。
- 三组执行代码：`ea3268bebae52bef2fba477e77bfdcf6ff84bcb1`，启动时工作区均干净。
  报告提交在执行代码之后，不改变已经运行的二进制；最终交付 HEAD 以 PR head 为准。
- 授权为用户提供的 `N5B_G2_Audit_and_G3_Start_for_Codex.md`：完成 G3 后停止，
  不进入 N5C，不合并/Ready/清理分支，不运行 CI、多 seed 或参数扫描。

修改仅涉及 `contrib/satcompute` 内的 placement 接线与计数、诊断、参数/构建、测试和模块 README，
以及本报告。完整文件清单可用 `git diff --stat 547520d80..ea3268beb` 查看。
没有修改 workload/topology/fault 模型参数、RNG 流、frequency solver、storage estimator、
N5A 恢复选择算法或实际成本公式；机制中仅增加真实准入/释放的观察回调。

## 2. 三组配置与公平性

| 组 | Placement | Frequency | 用途 |
|---|---|---|---|
| A | FFP | fixed delta=5%、n=4 | N5A 实际执行基线 |
| B | FFP | CompFRR dynamic | A→B：N5B 主比较 |
| C | LRL，lambda=1 | 与 B 相同 | B→C：集中度诊断，不是 N5C 最终算法 |

均为 LEO-66（6×11，66 个计算节点）、800 任务、1300 s、在线 generate、seed=1/run=11、
10 Gbps、1 ms/link、deadline factor=1.3、每节点额外备份存储 10 GB。
F1/F2/F3 均启用，沿用冻结参数，F3 为节点 62、1027.055770726 s。
工作量分布、拓扑、算力、路由/chunk、故障模型及 RNG 流分配完全相同。
每组恰运行一次，独立进程/输出目录并行执行；并行仅影响墙钟耗时，不引入共享仿真状态。
未因结果调参或重跑。故障概率 CSV 与 shadow 关闭；策略自己的诊断 CSV 显式启用。

不要求不同策略的 fault trace 相同：策略改变真实恢复负载后，F1 温度及故障抽样结果属于
在线反馈效果。相同 seed/run 不是锁定同一故障清单，更不是多 seed 显著性证据。

## 3. Placement 与动态计数

FFP 按稳定 node ID 选首个可行 local，再选 remote。LRL 仅将排序换为
`(activeRemoteAssignments + lambda * activeRecoveries, nodeId)`；lambda=1 在正式结果出现前冻结。
二者共同要求：非 primary、健康、空闲（含队列为空）、可达、local 一跳、local≠remote，
之后使用同一个 storage/deadline/path 准入、同一个 checkpoint/recovery 执行器。
不增加风险/距离/容量/任务大小的加权目标，ON 后不迁移既定节点对。

- `PlacementLoadLedger` 的 backup 只计当前 remote assignment，不计 local，也不是历史累计排名。
  INIT_BASE 实际预留成功后建立；实际状态释放后撤销。保留用于恢复的 remote checkpoint
  在使用结束、物理释放时撤销，而非在故障刚发生时提前归零。
- recovery 在实际 accepted（含 reserved-idle）后建立；计算完成、失败或清理时释放。
  RESULT 传输不占恢复计算服务，故不再计 active recovery。重复清理幂等、所有权不可重复。
- 累计分配/恢复次数、活动峰值另存，所有计算星（包括零负载星）进入分位数分母。
  `placement-load-events.csv` 可逐事件重建计数，最终须全部归零。
- 空闲硬约束通常排除恢复中的节点，当前 LRL 主要由 active assignment 驱动；local-first
  和一跳候选集合会限制均匀性。每个 fault epoch 提案读取当时的因果快照，不为负载排序
  改变 G2 的“先提案、再故障、存活才提交”时序。

C 不重新实现 RECOMPUTE 的后备节点排序；那仍是 N5A 的稳定可行节点选择。
因此备份分配集中度和全部恢复集中度应分开解释，不能从前者直接推断后者。

小规模证明：65,536 个硬约束组合中 LRL 与 FFP 的可行存在性相同，返回 pair 均符合约束；
FFP 与原实现保持一致。两任务真实运行中首任务 remote=0，第二个任务 FFP 仍为 0、LRL 为 1；
第二个 START 的 remote active assignment 分别为 1/0。FFP 暂停 0.1 s（REMOTE_BUSY），LRL 无暂停。
两组最终各 2 次分配、2 次接受恢复，8 条增减事件可重建，活动计数均归零。
重复 LRL 的 decision/node/event CSV 逐字节一致。这些只证明接线，不替代正式效果证据。

## 4. 正式结果

### 4.1 主比较

三组 returncode 均为 0，全部跑到 1300 s；每个任务恰有一个逻辑终态，所有保护流终止，
池 used/reserved、活动分配/恢复计数均归零，quiescent=true。配对参数与代码检查通过。

| 指标 | A | B | C |
|---|---:|---:|---:|
| 完成 / 失败（均为 compute deadline） | 796 / 4 | 790 / 10 | 792 / 8 |
| 完成率 | 99.50% | 98.75% | 99.00% |
| 恢复接受 / 成功 / 失败 | 83 / 79 / 4 | 83 / 73 / 10 | 83 / 75 / 8 |
| TAIL / REMOTE_REDO / RECOMPUTE | 55 / 18 / 10 | 57 / 4 / 22 | 59 / 5 / 19 |
| START / never START | 800 / 0 | 402 / 398 | 402 / 398 |
| 故障时 OFF / INITIALIZING / ON | 0 / 5 / 78 | 17 / 0 / 66 | 17 / 0 / 66 |
| 正常保护等价 WU | 1522840 | 390140 | 412340 |
| reserved-idle 等价 WU | 462341.9765 | 817219.3819 | 674585.5906 |
| actual catchup WU | 2143633 | 3488290 | 2804271 |
| **W_waste_actual** | **4128814.9765** | **4695649.3819** | **3891196.5906** |
| waste / 原任务总 WU | 1.1742% | 1.3354% | 1.1066% |
| protection 发送字节（GB，十进制） | 485.244130 | 217.171134 | 219.837163 |
| 节点存储峰值 max / P50 / P90（GB） | 2.704702 / 0.096995 / 0.231727 | 2.001100 / 0 / 0.591331 | 1.755506 / 0 / 0.714850 |
| allocation failure 次数 / 任务 | 0 / 0 | 0 / 0 | 0 / 0 |
| 墙钟秒（并行运行，仅执行记录） | 2410.65 | 1175.45 | 1197.02 |

**A→B**：正常保护成本下降 74.38%，保护网络字节下降 55.24%；但 reserved-idle 增加 76.76%、
实际 catchup 增加 62.73%，总 waste **增加 13.73%**，完成任务减少 6。
故障时未启动保护的任务由 0 变为 17，RECOMPUTE 从 10 增至 22，恢复尾部代价不能忽略。
这是频率开销与恢复损失之间的真实权衡；本轮没有证明 B 在主 waste 或完成率上优于 A。

**B→C**：完成任务增加 2、remote-busy 回退由 5 减为 2，waste 下降 17.13%，
但正常保护成本增加 5.69%、保护网络字节增加 1.23%。C 的 waste 比 A 低约 5.76%，
完成数仍少 4；只是 placement 诊断结果，不是 N5C 最终算法收益或显著性结论。

三组 fault trace **逐字节相同**：均 87 条，F1=84、F2=2、F3=1，83 个直接受害任务/incident
（82 次可恢复计算中断、1 次永久整星中断）。另 4 次故障没有正在计算的直接受害任务；
不能将 87 条故障当成 87 个失败任务。允许策略改变 fault realization 的合同不变，本轮恰未发生差异。

T_catch 只统计真实到达故障前进度的样本：

| 指标 | A | B | C |
|---|---:|---:|---:|
| 已观测 catchup / 未到达 | 80 / 3 | 77 / 6 | 78 / 5 |
| mean / P50 / P90（s） | 0.241497 / 0.191945 / 0.424275 | 0.427557 / 0.122718 / 1.096397 | 0.355178 / 0.120480 / 1.036909 |
| planned catchup WU | 3431775 | 4899039 | 3667236 |
| planned post WU | 23398854 | 23398854 | 23398854 |
| planned total recovery WU | 26830629 | 28297893 | 27066090 |
| actual catchup WU | 2143633 | 3488290 | 2804271 |
| actual post WU | 23283000 | 22959184 | 23033550 |
| actual total recovery WU | 25426633 | 26447474 | 25837821 |

不能只看 B/C 较低的 T_catch 中位数：其 P90、均值和未到达数都高于 A；且样本集合不完全相同。

实际网络发送量（GB；完整 declared/sent/received 和流数保存在各组 JSON/原始 CSV）：

| 类别 | A | B | C |
|---|---:|---:|---:|
| INIT_BASE | 192.999633 | 109.092439 | 109.092439 |
| INIT_STATE | 0 | 25.879063 | 25.915024 |
| L1 | 153.107914 | 50.672737 | 52.438664 |
| REMOTE_BATCH | 133.840287 | 22.549801 | 24.614963 |
| RECOVERY_INPUT | 3.123621 | 6.300564 | 5.071952 |
| RECOVERY_TAIL | 2.172675 | 2.676531 | 2.704121 |
| RECOVERY RESULT（另计业务结果） | 9.658069 | 9.228088 | 9.537319 |

A 在计算起点初始化，初始变量状态为零，故 INIT_STATE 网络字节为零；B/C 延迟 START，
必须传输已经产生的状态。三组 LocalDelivery INPUT 均 0 B；成功本地 RESULT 的 logical bytes
分别为 106845642 / 106845642 / 274342049 B，均不创建 RESULT 网络流。

集中度以全部 66 个计算星为分母，包括从未被选中的星：

| 指标 | A | B | C |
|---|---:|---:|---:|
| 每节点累计 backup max / P50 / P90 | 565 / 0 / 0 | 278 / 0 / 0 | 184 / 0 / 0 |
| 每节点接受 recovery max / P50 / P90 | 49 / 0 / 0 | 53 / 0 / 0 | 50 / 0 / 0 |
| top-3 backup share | 99.625% | 99.751% | 99.005% |
| top-3 recovery share | 100% | 100% | 98.795% |
| 最大 active backup / active recovery 峰值 | 6 / 1 | 4 / 1 | 3 / 1 |
| 重建通过的 load event 数 | 1766 | 970 | 970 |

| node | A 累计 backup/recovery | B 累计 backup/recovery | C 累计 backup/recovery |
|---|---:|---:|---:|
| 0 | 565/49 | 278/53 | 184/50 |
| 1 | 183/26 | 99/23 | 147/26 |
| 2 | 49/8 | 24/7 | 67/6 |
| 3 | 3/0 | 1/0 | 4/1 |

其他节点均为零。LRL 降低了当前活动重叠和单节点最大分配，但没有消除长期低 ID 集中：
它不是按历史累计负载轮转，活动计数清零后仍按稳定 ID 打破平局。

按 profile 的实际结果与每任务 local/remote 峰值（不是节点共享池峰值）：

| 组/profile | 完成/任务 | 恢复成功/尝试 | W_waste_actual（WU） | protection GB | local / remote peak B |
|---|---:|---:|---:|---:|---:|
| A dense-image | 240/240 | 13/13 | 696117.810 | 176.219531 | 255330802 / 1255330802 |
| A sparse-inference | 239/240 | 23/24 | 759967.874 | 64.082662 | 264048 / 555473015 |
| A compression | 237/240 | 28/31 | 1586546.292 | 130.890842 | 138795477 / 1138795477 |
| A llm | 80/80 | 15/15 | 1086183.000 | 114.051094 | 251166720 / 891469824 |
| B dense-image | 238/240 | 11/13 | 600995.243 | 65.786256 | 777525688 / 1755505741 |
| B sparse-inference | 238/240 | 22/24 | 1077533.671 | 45.101447 | 967058 / 555341922 |
| B compression | 235/240 | 26/31 | 1813681.990 | 62.126986 | 504270840 / 1108537781 |
| B llm | 79/80 | 14/15 | 1203438.478 | 44.156445 | 750747648 / 957415424 |
| C dense-image | 238/240 | 11/13 | 606782.796 | 67.298605 | 777525688 / 1755505741 |
| C sparse-inference | 238/240 | 22/24 | 1078233.671 | 45.102400 | 967058 / 555341922 |
| C compression | 237/240 | 28/31 | 1214325.099 | 61.838793 | 504270840 / 1431892015 |
| C llm | 79/80 | 14/15 | 991855.025 | 45.597364 | 755679232 / 957415424 |

### 4.2 频率行为与准入

| 指标 | B | C |
|---|---:|---:|
| 提案记录 | 3338 | 3338 |
| START / never START | 402 / 398 | 402 / 398 |
| 故障时 OFF / INITIALIZING / ON | 17 / 0 / 66 | 17 / 0 / 66 |
| UPDATE / 其中配置真正变化 | 871 / 599 | 922 / 646 |
| delta P10 / P50 / P90 | 1.4% / 5.5% / 10% | 1.4% / 5.5% / 10% |
| n P10 / P50 / P90 | 8 / 10 / 45 | 8 / 10 / 45 |
| 时间加权 delta / n | 5.8256% / 18.3089 | 5.7751% / 18.1316 |
| PAUSE 提交 / 合并后 episode | 177 / 128 | 124 / 107 |
| PAUSE 总时长 ns | 149314468331 | 101615064160 |
| START P_finish P10 / P50 / P90 | 0.001131 / 0.049681 / 0.649375 | 0.001131 / 0.049681 / 0.649375 |
| START 的 Joff−Jstart P10 / P50 / P90（s） | 0.000406 / 0.025246 / 0.434456 | 0.000406 / 0.024471 / 0.445189 |
| OFF_NOT_MORE_EXPENSIVE | 1635 | 1640 |
| INITIALIZATION_TOO_LATE | 80 | 80 |
| DEADLINE_INFEASIBLE / STORAGE_INFEASIBLE | 0 / 0 | 0 / 0 |
| PLACEMENT_UNAVAILABLE（solver 汇总原因） | 280 | 221 |

两组各 416 条有利 START 提案，其中 14 条遇到当轮故障未提交；402 个真实 START 均建立有效分配。
不是全部任务都保护，398 个任务从未 START。两组均无存储拒绝任务、无实际分配失败，
因此本轮没有“频繁 storage reject 而实际低峰值”的观察，不调整 estimator。

下表的暂停次数是**按原因分段数**，同一连续暂停改变原因时会跨行；不能相加后当作 episode。

| PAUSE 原因 | B 分段数 / ns | C 分段数 / ns |
|---|---:|---:|
| LOCAL_BUSY | 17 / 32728940709 | 7 / 8918514535 |
| REMOTE_BUSY | 27 / 34001388326 | 13 / 12873891788 |
| PATH_UNAVAILABLE | 91 / 82584139296 | 89 / 79822657837 |
| PLACEMENT_UNAVAILABLE / STORAGE_INFEASIBLE / DEADLINE_INFEASIBLE | 均 0 | 均 0 |

四类 profile 的频率统计（`O/I/N` 表示故障时 OFF/INITIALIZING/ON）：

| 组/profile | START/never | O/I/N | UPDATE/变化 | delta P10/P50/P90（%） | n P10/P50/P90 | 加权 delta（%）/n | PAUSE episode/秒 |
|---|---:|---:|---:|---|---|---|---|
| B dense-image | 101/139 | 2/0/11 | 206/169 | 1.8/5.2/10 | 8/8/10 | 5.7200/8.4189 | 37/43.1757 |
| B sparse-inference | 139/101 | 9/0/15 | 285/164 | 1/5/10 | 10/20/100 | 5.3663/36.9649 | 22/30.5126 |
| B compression | 115/125 | 6/0/25 | 251/165 | 1.4/5.8/10 | 10/11/12 | 5.9658/11.1184 | 32/36.2672 |
| B llm | 47/33 | 0/0/15 | 129/101 | 2.4/8.1/10 | 6/6/10 | 6.7302/7.1497 | 37/39.3590 |
| C dense-image | 101/139 | 2/0/11 | 227/186 | 1.9/5.1/10 | 8/8/10 | 5.7005/8.4191 | 29/24.6228 |
| C sparse-inference | 139/101 | 9/0/15 | 296/175 | 1/5/10 | 10/20/100 | 5.3162/36.9895 | 16/20.5051 |
| C compression | 115/125 | 6/0/25 | 261/174 | 1.4/5.7/10 | 10/11/12 | 5.8911/11.1192 | 29/26.3489 |
| C llm | 47/33 | 0/0/15 | 138/111 | 2.4/7.7/10 | 6/6/10 | 6.6522/7.0798 | 33/30.1383 |

### 4.3 B→C 的具体恢复差异

B/C 的 fault trace 逐字节相同，这是本次结果，不是强制回放。C 将两个 compression 任务由失败改为完成：

| task | input bytes | B | C |
|---|---:|---|---|
| 456 | 308759776 | remote=0 忙，回退 node=2 重算，T_catch=1.64967 s，deadline 失败 | remote=1，TAIL，T_catch=0.11229 s，完成 |
| 475 | 570029400 | remote=0 忙，回退 node=1 重算，尚未 catchup 即超时 | remote=0 当时空闲，TAIL，T_catch=0.14803 s，完成 |

任务 475 的 remote ID 并未改变，变化来自其他分配引起的实时占用；不能把改善全部归因于“换了节点”。
两组均 66 份 ON checkpoint，remote 在故障时可行分别 61/64，busy 分别 5/2。
RECOMPUTE 原因分别为 `STATE_MISSING=17, REMOTE_BUSY=5/2`；其余四种回退原因本轮均零。
作为参照，A 有 78 份 ON checkpoint，remote 可行 78、busy 5，回退 `STATE_MISSING=5, REMOTE_BUSY=5`。
“remote 可行”只问健康/空闲/路径，不要求 checkpoint 已就绪，A 的 78 中含 5 个 INITIALIZING；
必须与 phase/checkpoint_state_exists 联合解释，实际 checkpoint 路径数为 55+18=73。
这验证了诊断链，但尚不代表 LRL 是最终 placement 算法。

## 5. 指标口径与验收

主浪费量仍为 N5A 的 `normal_protection_eq_wu + reserved_idle_eq_wu + actual_catchup_wu`；
包括未故障任务的正常保护成本，post-catchup 不计 waste。失败恢复只计实际服务完成的 WU，
planned/actual 的 catchup/post/total 分别保留并验证分区、速率、时长和上界。
T_catch 只统计实际到达 catchup 的样本，同时列出未到达数，不能用幸存样本代表全部恢复。

START/UPDATE 只计真实提交，另列实际分配数；初始化预留失败不虚增有效 assignment。
delta/n 分位数来自已提交 START/UPDATE，时间加权只覆盖真实物理 ON 且未 PAUSE 的时长。
PAUSE 原因变化时分段，相邻片段合并计 episode，总时长在真实 protection stop 截止。
分母不是所有 1300 s 或所有任务总存活时间。INITIALIZING 不当成已可恢复的 ON。
`PATH_UNAVAILABLE` 指当前路径估计不可用（也包括剩余瓶颈速率为零），不等于发生拓扑断链。

回退原因在下一纳秒实际恢复裁决时记录；`remote_eligible_at_fault/remote_busy_at_fault`
取原故障回调快照（节点健康已更新，同批路由 overlay 尚未应用）。不强称两者同一时间点。
`checkpoint_state_exists` 是故障时已提交 ON remote 对象是否存在，不是“出现过 START”。
直接受害者为 `RUNNING_INTERRUPTED*` 的唯一 task，incident 另按 task/fault 对计数；
其他排队/输入/结果端点影响不混入该分母。

网络按 INIT_BASE、INIT_STATE、L1、REMOTE_BATCH、RECOVERY_INPUT、RECOVERY_TAIL 分列声明/发送/接收字节。
Recovery RESULT 单列为业务结果；LocalDelivery 只计 logical bytes，不计网络流。
存储报告节点峰值及分位数、失败次数/任务，不能将 solver 拒绝等同实际分配失败。
若有 storage reject 而实际峰值较低，仅列保守估计观察，不据此放宽 estimator。

本地验收：21 个 C++ 程序、70 个 Python 单元测试（无 skip）、10 个 smoke 套件、全部维护回归通过。
frequency-runtime 为 3,186 项检查/16 个小案例，policy 为 211,045 项，recovery-runtime 为 866 项。
N4B 维护验收仍为 88 完成/12 失败、11 START、483 条概率逐值一致。
fixed smoke 仍为 81 个保护流完成、17 次含初始化 commit、零泄漏。
新分析器有分位数/物理 ON 加权/暂停扣除/正式 runner 门禁的手算锚点，并在四任务真实 CSV 上验证。
编译使用项目 `.venv` CMake；全局 ns-3 examples/tests 关闭，无上游测试、CI 或完整场景重复运行。

## 6. 证据位置与复核

- 正式原始输出：`output/n5b-g3/A-ffp-fixed/`、`B-ffp-compfrr-frequency/`、`C-lrl-compfrr-frequency/`。
- 小规模：`output/n5b-g3-small-runtime/`，包括 FFP/LRL 两任务与 LRL 重复运行。
- 本地检查日志：`/tmp/scp-n5b-g3-{cpp,python,smoke,regression}.log`。
- 执行身份/完成状态：各目录 `execution.json`、`execution-result.json`、`run.log`、`time.txt`。
- 分析入口：`tests/integration/regression/analyze-frequency-evaluation.py`，复用 N5A accounting，
  不运行仿真、不覆盖原始 CSV；生成物均按现有 gitignore 留在本地。

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/analyze-frequency-evaluation.py \
  --runs output/n5b-g3/A-ffp-fixed output/n5b-g3/B-ffp-compfrr-frequency \
  output/n5b-g3/C-lrl-compfrr-frequency
```

## 7. 限制、建议与停止点

建议接受 **G3 实现与第一轮实验门禁通过**，但不将 N5B 宣称为全面优胜：B 的正常保护/网络成本
显著下降，同时完成率降低、总 waste 上升。应先审阅 OFF 覆盖、恢复尾部与目标函数权衡，
不能在本轮事后改参数、过滤失败或重跑直至胜出。

N5C 前的观察限于：低 ID 长期集中依然明显；remote-busy 可改变恢复路径；分配调整有正常保护/
网络额外代价；部分失败来自未保护而不是节点排序。以上不是新 policy 设计或实现授权。
单个 paired seed/run 不能替代多 seed 的统计证据；不把预测 Rbar 等同实际 T_catch，
不从幸存 catchup 样本推断全体恢复。本阶段没有开展 CI、置信区间、容量敏感性或 lambda 扫描。

**STOPPED AT N5B-G3**；PR #96 保持 Draft，base=n5，等待用户审阅；不 merge/Ready/删分支，不进入 N5C。
