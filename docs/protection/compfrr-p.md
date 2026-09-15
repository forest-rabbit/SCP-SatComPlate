# CompFRR-P：固定配置后的备份节点选择

启用 `--protectionScheme=compfrr --compfrrPlacementPolicy=compfrr`（默认 adaptive），
支持 Eager/Deferred/Selective；用户现已将 CompFRR-P + Selective + Relocate 指定为正式默认，
旧 FA-FFP 默认仅作为历史实验身份保留，不改变任何 placement 算法。
production compute-pressure 仅有两种，默认不变：

| 正式 policy | CLI 值 | 用于评分的 U |
| --- | --- | --- |
| CUMULATIVE：Cumulative Historical Compute Pressure | `--compfrrPressureModel=cumulative` | 全历史实际 compute busy / 存活 exposure |
| IDLE_AWARE：Idle-Aware Historical Compute Pressure | `--compfrrPressureModel=idle-aware` | `U_global * H/(H+I)` |

`compfrrPlacementAblation=noR|noU|noM` 保留为 cumulative 下的 ablation capability，
默认 none；只关闭对应评分项，仍记录原值并保留硬约束。
noU 不是第三种 U policy；`recent-U` 已从正式 CLI 移除，历史 API/证据见[归档口径](reproducibility.md)。

| 文件 | 职责 |
|---|---|
| `policy/compfrr/placement/compfrr-placement-policy.*` | 恢复冲突、存储压力、确定性 min-max 排名 |
| `policy/compfrr/placement/compute-pressure/*` | CUMULATIVE / IDLE_AWARE；不带可调衰减参数 |
| `storage/peak-quota-ledger.*` | 按 owner 替换/释放未来远端峰值承诺 |
| `policy/compfrr/compfrr-placement-adapter.cc` | 当前状态/原生预测器到候选的只读适配，不重新求解 Frequency |
| `runtime/placement-resource-tracker.*` | 中性实际 READY、assignment 积分、storage byte-time 与 quota 接线 |
| `policy/compfrr/placement/compfrr-placement-tracker.*` | P 提案与 pressure 诊断；不拥有求解器 |
| `runtime/compute-usage-history.h/.cc` | 通过原有计算状态通知记录实际普通/恢复服务区间；只读前缀查询，不改变调度 |
| `metrics/core/{n5c-placement,placement-resource}-metrics.cc`（satcompute 目录下） | P 决策与中性资源分别写出；保留原外部 schema |

START 保留 FA-FFP 公共节点/路径筛选后的首个 reference pair 的 local。先检查该 remote；
仅当现有 Frequency 返回 deadline、storage 或 initialization-too-late 硬拒绝时，按原确定性顺序
检查同一 local 的后续 remote，直到首个硬可行 anchor。硬可行但收益不足时不继续挑选其他 remote。
冻结 anchor 的启动及 `(δ,n)` 结果，然后用实际 remote 的算力、带宽、存储验证固定配置，按 V4 排名。
排名阶段仍不重跑 Frequency，不搜索其他 local，不改目标函数或搜索空间。未越过完整同纳秒故障批次
之前不占用资源；提交前再次检查同一 pair/config，不可行则拒绝 START 或暂停 ON。
ON 始终使用 committed 实际 pair 的资源更新配置，不重新 placement。

三个归一化量：`R = 加权冲突质量 / 本任务可恢复首次故障质量`，
`U = (普通实际忙时 + 恢复实际忙时) / 存活观测时间`，`M = 提交后的实际与配额不重复占用 / 容量`。
选择 `max(R,U,M)` 最小者；并列依次比较主星到 remote 的传播时延、稳定卫星 ID。
R 是当前活跃任务的竞争风险代理，不是已校准的真实 busy 概率；不读取未来到达任务或故障结果。

每个未来抽样点的首次故障质量为此前存活概率乘该点 `q_comp`。Ready 之前的概率仍扣减存活，
不重新归一化；恢复占用窗口左闭右开。Eager 的 Ready 包含预置 INPUT，Deferred 只要求状态就绪及
故障时完整 INPUT 路径/deadline 可行，同星 INPUT 的估计网络等待精确为零。
已初始化任务使用实际 READY 时间；初始化中的任务使用当前完整初始化的保守估计，不能冒称真实就绪。
V4 的串加时间只作候选估计，实际恢复仍按既有 INPUT/state 并发屏障执行。

quota 按节点逐任务累加 `max(实际 used+reserved, 当前远端 peak quota)`，其余物理对象按实际计入；
ON 更新替换自身旧 quota，不重复计占用，任务离开主计算后释放未来承诺，实际对象仍由原生命周期释放。
历史利用率的分母从仿真起点到当前观测时刻，整星故障后截止于实际 F3；F1/F2 不扣除恢复免疫执行时间。
无 exposure 或无预测需求分别记录 `history_unavailable` / `no_predicted_demand`，不靠截断掩盖错误。

IDLE_AWARE（历史证据名 `rational-U`）使用 `U_global * H/(H+I)` 作为评分中的 U，
H 为当前主任务精确剩余纯计算时间（整数 ns），不是 deadline 余量，I 为节点自最近一次实际普通/恢复计算结束后的连续空闲时间。
从未计算的节点从 t=0 累计空闲；当前实际忙则 I=0，但不会因此绕过现有空闲候选硬约束。
预留等待不算忙，F1/F2 暂时不可用且没有真实计算时继续累计空闲；恢复免疫期间的实际计算仍算忙。
H 必须正、I 非负；不新增衰减系数，不读取未来任务或未来故障。
`n5c-rational-u-history.csv` 单独记录 H、I、freshness、累计 U 与 rational pressure；
原候选表的 `historical_utilization` 和节点全程统计仍是实际累计利用率，不改含义。
R/M、min-max、传播时延/稳定 ID tie-break、START/ON、Frequency 和 Recovery 均不变。
两种正式 policy 并列保留，不据重构更换默认 FULL 或宣称 IDLE_AWARE 更优。
旧五轮审计与 corrected maintenance 结果见[复现与证据边界](reproducibility.md)。

`n5c-placement-decisions.csv` 只记录 START 后的空间提案，区分 reference/实际资源、候选及最终准入。
原 `frequency-decisions.csv` 的 OFF 标量和评分属于可行 anchor（无 fallback 时即原 reference），
local/remote 列为实际提案；ON 均为实际 pair，原列保持不变。
新增 `compfrr-candidate-coverage.csv` 仅记录 P 的 OFF 搜索，区分原 reference、首个可行 anchor、
最终 P remote 和真实提交。anchor index 从 1 起算；fallback depth 为多检查的 remote 数，即 checked−1。
汇总分开报告 hard-feasible、START、fault-hit 和全体不可行，不能把 anchor 存在等同于已建立保护。
`placement-resource-summary.csv` 对平台 CompFRR 各 placement 输出相同的只读计数/积分/峰值；
FA-FFP 等不会因此启用 N5C quota。原场景、故障随机流、Routing、恢复策略和工作量标度不变。
正式运行与审计入口见 [测试说明](../../contrib/satcompute/tests/README.md)，结果见
[N5C 验收记录](../n5/reviews/N5B-closeout-N5C-kickoff.md)。
离线汇总的 direct/relocate/recompute 比例以恢复尝试数为分母，failed 是可与这些动作重叠的结果；
未追平不记零延迟。`relocated_state_logical_bytes` 仅为迁移检查点逻辑大小；
`migration_sent_bytes_by_kind` / `migration_total_sent_bytes` 才是 MIGRATE 操作实际发送的
INPUT/state/tail（含部分发送、取消，按真实流去重），同星交付不凭空增加网络字节。
