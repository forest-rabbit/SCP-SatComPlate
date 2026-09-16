# N5B-G3R：语义修订与正式复验

状态：**R1–R4、维护测试和三组正式复验通过；STOPPED AT N5B-G3R，等待用户最终审阅。**
PR #96 保持 Draft，不自动冻结 N5B、不合并或进入 N5C。

## 范围与版本

沿用 `feature/n5b-compfrr-frequency`、Draft PR #96、base=`n5`。
`n5` 基线为 `b626a154d423219bc1503f060252962e1965b4cf`，修订起点为
`a3800c4b02a08ce3d5d4da73b420be31bd1c2f27`。
三组正式执行代码均为 `5ab805024b4995e1924eb015461d303f458a4453`，启动时工作区均干净。
本报告在执行提交之后补充，不改变已经启动的二进制。
[原 G3 报告](N5B-G3-frequency-evaluation.md)及 `output/n5b-g3/` 完整保留为 pre-revision evidence。
本次只修订故障预测查询、frequency 接线、网络准入预览、checkpoint 迁移和对应指标/测试；
不改变冻结场景、故障参数/RNG、评分公式、成本档位、delta/n 网格、deadline 或链路配置。
具体文件以该起点至执行提交的 `git diff --stat` 为准。

## R1–R4 合同

- **R1**：TASK_RUNNING 立即评估 OFF→START，未启动者后续仍在故障检查点评估。
  复用模型副本，从下一真实全局抽样点预测，不新增 Bernoulli draw；同刻检查未执行时包含当前点，
  已执行时排除，任务预计完成时刻排除。START 仅进入 INITIALIZING，真实收齐及 cR 后才 ON。
  常规抽样前预测与 ON UPDATE/PAUSE 的提案→抽样→存活提交顺序保留。
- **R2**：eventual F3 不关闭 F1/F2；实际 F3 时刻另记因果快照，`F1F2_sampled=0`。
  未读未来 F3 时间。低温且位于 F2 域外的零概率保留为零。
- **R3**：保护与恢复统一使用 NetworkTransferEngine 的只读路径查询，capacity-aware
  复用真实完整 ECMP FindPath/reservation；非容量模式复用相应 next-hop policy 的隔离预览。
  NO_ROUTE 与 NO_ADMISSIBLE_PATH 分开；不预留资源、不重路由已有流，实际传输仍重新准入。
  START 的硬路径是 primary→remote、primary→local、local→remote；source→remote INPUT
  重放是 OFF 成本的软条件。不可用时不伪造带宽，P_finish>0 且 START 可行可以启动；零风险不强制启动。
- **R4**：有效 checkpoint 的原 remote 忙/计算不可用但存储可读时，先尝试稳定 ID 顺序的空闲目标。
  MIGRATE_REDO 真实传输 `CommittedStateBytes(rf)`；MIGRATE_TAIL 同时注册 state 与实际 tail
  记录之和（含 H），收齐后一次 cR。目标真实预留存储，旧状态保留到新目标可接管或任务终态。
  可行 checkpoint 优先于零起点重算；全部不可行才 RECOMPUTE。

细节见 [protection README](../../../contrib/satcompute/protection/README.md)、
[fault README](../../../contrib/satcompute/fault/README.md)、
[traffic README](../../../contrib/satcompute/traffic/README.md)。

## 验证与边界

本地 targeted build、21 个 C++ 测试程序、71 个 Python unit（无 skip）、10 组 smoke、
全部维护 regression 通过。聚焦 frequency runtime 3262 项、policy 211047 项、path 2025 项、
recovery runtime 1391 项检查；
新增迁移对账测试独立核对完整状态字节与双接收/cR 屏障，不只依赖运行时 summary。
原 N4B 维护验收仍为 88 完成/12 失败、11 START、483 条模型/预测概率一致。
测试日志保留于本机 `/tmp/scp-g3r-{cpp-final,python-final,smoke,regression}.log`。

聚焦覆盖：开始即评估/开始即 START/稍后 START、真实网格同刻相位、无额外 RNG、
eventual F3 的正风险与模型零风险、首选路径忙而备选可准入、四种任务精确 committed state 字节、
直接 TAIL/REDO 不变、迁移 TAIL/REDO、目标忙/存储不足、迁移传输及 F3 失败、
旧状态保留、单次终态、实际 WU 和 reserved-idle、存储/网络锁清零。

路径快照不是容量预约，不估计未来队列释放；并行注册不保证两条流同时获得带宽。
已接受迁移发生真实传输失败时，仍遵守单次恢复合同，不偷偷增加第二次恢复。
迁移等待计 reserved-idle，RECOVERY_STATE 计实际网络账本；零字节 LLM 初始 state 不制造 UDP。
下文给出原 17 个 OFF 和 5 个 REMOTE_BUSY 锚点对照及完整资源结果。

阶段边界：不进入 N5C、不 Ready/merge、不清理分支、不触发阶段 CI；完成后推送原 Draft PR 并停止于 G3R。

## 正式 A/B/C 结果

三组都完整运行到 1300 s、returncode=0；公平性检查通过。墙钟分别为 40.583 / 19.106 / 20.404 min。
本轮 trace 恰好逐字节相同：F1=84、F2=2、F3=1，共 87 条发生记录、83 个直接受害任务；
不把 trace 相同作为在线策略比较的必备条件。FlowMonitor 显式 Drop 与 lost 均为 0，
各组仅因 F3 增加一次路由重算（总计 2 次）。唯一逻辑终态、实际 WU、传输终态、
capacity reservation、backup pool、placement load 及异步操作清理全部通过。

| 指标 | A：FFP+fixed | B：FFP+CompFRR | C：LRL+CompFRR |
|---|---:|---:|---:|
| 完成 / deadline 失败 | 800 / 0 | 797 / 3 | 796 / 4 |
| 成功恢复 / 接受恢复 | 83 / 83 | 80 / 83 | 79 / 83 |
| 正常保护等效 WU | 1522840 | 422140 | 445640 |
| 恢复 reserved-idle 等效 WU | 412293.0357 | 635944.0212 | 607906.5723 |
| 实际 catchup 重做 WU | 1352720 | 1735052 | 1492199 |
| **W_waste_actual（以上三项之和）** | **3287853.0357** | **2793136.0212** | **2545745.5723** |
| protection 实际发送 GB | 484.641230 | 223.333832 | 226.326690 |
| TAIL / REMOTE_REDO | 56 / 17 | 63 / 4 | 67 / 2 |
| MIGRATE_TAIL / MIGRATE_REDO | 5 / 0 | 5 / 0 | 1 / 1 |
| RECOMPUTE | 5 | 11 | 12 |
| 已观测 catchup / 未到达 | 83 / 0 | 81 / 2 | 81 / 2 |
| T_catch mean / P50 / P90(s) | 0.212652 / 0.196885 / 0.416665 | 0.261933 / 0.110006 / 0.849744 | 0.228490 / 0.105607 / 0.627617 |

GB 为十进制。W_waste 不包含 catchup 后的正常剩余工作；失败任务仅计真实执行前缀。
T_catch 分布只包含实际到达 xf 的任务，不把未到达者当作 0，也不能只用较小的中位数宣称更优。

- **A→B**：正常保护成本 -72.28%、网络 -53.92%、实际 waste -15.05%，但完成数少 3。
  A 的全任务固定保护能覆盖 F1/F2 零风险的 F3 受害者；B 不是无条件优于 A。
- **B→C**：waste 再降 8.86%，catchup 重做 -14.00%，但正常成本 +5.57%、网络 +1.34%、完成数少 1。
  额外失败为 596，因 C 的所选节点对当时无可准入保护路径；不将 LRL 宣称为最终最优算法。
- **相对修订前**：A 完成 796→800，B 790→797，C 792→796；B/C waste 分别降低 40.52%/34.58%，
  同时正常保护成本约增加 8.20%/8.08%，网络增加 2.84%/2.95%。这是整套修订的权衡，不是单项消融。

### 实际执行与资源账本

| WU 分区 | A | B | C |
|---|---:|---:|---:|
| planned catchup | 1352720 | 1847616 | 1604763 |
| planned post-catchup | 23398854 | 23398854 | 23398854 |
| planned total recovery | 24751574 | 25246470 | 25003617 |
| actual catchup | 1352720 | 1735052 | 1492199 |
| actual post-catchup | 23398854 | 23266883 | 23245857 |
| actual total recovery | 24751574 | 25001935 | 24738056 |

| 实际发送 GB | A | B | C |
|---|---:|---:|---:|
| INIT_BASE | 192.999633 | 112.532699 | 112.316355 |
| INIT_STATE | 0 | 24.927575 | 24.958363 |
| L1 | 153.107390 | 53.171031 | 55.126386 |
| REMOTE_BATCH | 133.839763 | 25.314949 | 27.348675 |
| RECOVERY_INPUT | 1.166878 | 2.619568 | 2.782115 |
| RECOVERY_TAIL | 2.310390 | 3.132216 | 3.171489 |
| RECOVERY_STATE | 1.217176 | 1.635794 | 0.623307 |
| recovery RESULT（另计业务结果） | 10.265415 | 9.953379 | 9.785578 |

三组共 12 次实际迁移均完成，RECOVERY_STATE 声明=发送=接收字节：
A 1217176464 B、B 1635794157 B、C 623306824 B。没有迁移失败或漏计 state 网络开销。
其他保护流在故障取消时可出现 sent≠received，这不等价于网络 Drop；原始终态和实际字节均保留。

| 存储/集中度（全部 66 星，包含零计数星） | A | B | C |
|---|---:|---:|---:|
| 单节点共享池最大峰值 B | 2704701610 | 2039466341 | 1754456798 |
| 分配失败 / 终态存储占用 | 0 / 0 | 0 / 0 | 0 / 0 |
| 单节点累计 backup 最大值 | 565 | 289 | 176 |
| top-3 backup 份额 | 99.625% | 99.523% | 97.368% |
| 单节点累计 recovery 最大值 | 49 | 50 | 46 |
| top-3 recovery 份额 | 98.795% | 98.795% | 96.386% |

LRL 缓解了集中度，但低 ID 集中仍明显；这不是已解决放置优化的证据。

| profile 完成数 | A | B | C |
|---|---:|---:|---:|
| dense-image（240） | 240 | 239 | 239 |
| sparse-inference（240） | 240 | 240 | 239 |
| compression（240） | 240 | 238 | 238 |
| llm（80） | 80 | 80 | 80 |

## 原 17 个 OFF 任务回归

这些任务在旧 B 组故障时均为 OFF。本轮 B/C 的输入、故障时刻和故障进度相同，
但实时路径占用可能随保护策略而不同。`P0` 是任务开始时预测的完成前 F1/F2 联合概率，
不是故障当秒 q。ON/I/O 分别表示故障时 ON/INITIALIZING/OFF；I 仍没有可用完整 checkpoint。

| task | P0 | B 首次评估 | B 故障时状态 / 恢复路径 / 结果 | C 与 B 的差别 |
|---|---:|---|---|---|
| 33 | 90.985% | START | ON / TAIL / 完成 | 无 |
| 120 | 0 | NONE：模型零风险 | O / RECOMPUTE / deadline | 无 |
| 192 | 5.654% | START | ON / TAIL / 完成 | 无 |
| 247 | 37.983% | START | I / RECOMPUTE / 完成 | 无 |
| 325 | 99.394% | NONE：NO_ADMISSIBLE_PATH | O / RECOMPUTE / 完成 | 无 |
| 360 | 85.804% | START | ON / TAIL / 完成 | 无 |
| 399 | 82.212% | NONE：NO_ADMISSIBLE_PATH | O / RECOMPUTE / deadline | 无 |
| 470 | 94.433% | NONE：NO_ADMISSIBLE_PATH | O / RECOMPUTE / 完成 | 无 |
| 504 | 22.828% | START | ON / REMOTE_REDO / 完成 | 无 |
| 548 | 77.802% | START | I / RECOMPUTE / 完成 | 无 |
| 564 | 99.384% | START | I / RECOMPUTE / 完成 | 无 |
| 568 | 86.396% | START | ON / TAIL / 完成 | 无 |
| 591 | 46.875% | START | I / RECOMPUTE / 完成 | 无 |
| 596 | 42.944% | START | ON / TAIL / 完成 | C 路径无可准入容量，O / RECOMPUTE / deadline |
| 645 | 89.722% | NONE：NO_ADMISSIBLE_PATH | O / RECOMPUTE / 完成 | 无 |
| 727 | 5.783% | NONE：NO_ADMISSIBLE_PATH | O / RECOMPUTE / deadline | 无 |
| 734 | 63.506% | START | I / RECOMPUTE / 完成 | 无 |

B 中 11 个在 TASK_RUNNING 立即 START，其中 6 个故障前完成初始化、5 个仍处于初始化。
后者不能用预测 T_init 冒充真实 ON，故仍重算。325/470/645/727 后来在真实故障检查点
提出有利 START，但该轮实际故障命中，继续遵守不提交的新状态约束。
因此修订消除了“只因等待检查点”的启动延误，并不保证真实容量约束或初始化延迟消失。

### 360 / 399 路径锚点

360 在 971.113562074 s 即通过真实准入预览并 START，971.321437248 s 完成初始化，
972 s 故障时已有可恢复状态。B 的 399 在 59.277757046 s 和 60 s 的共享完整路径查询均返回
NO_ADMISSIBLE_PATH，而非 NO_ROUTE；其 INPUT 重放可用，受阻的是保护硬路径。
没有通过选一条不存在/无容量的路径来强行保护。首选忙、备选可用的情况已由 2025 项 path
测试中的真实双路径准入锚点验证；正式记录不是对“所有时刻都必有备选容量”的承诺。
C 的 399 在任务开始时同样无可准入路径，60 s 时路径已可用并提出 START，但当轮故障命中，
不能倒推为故障前已经建好保护。

### 120 的 F3 / 风险锚点

任务 120 在 1024.852770726 s 开始计算；TASK_RUNNING、1025/1026/1027 s 检查点及
1027.055770726 s 实际 F3 快照的 pF1/pF2/q/P_finish 全为 0。
前三个整数秒是实际抽样点；非整数 F3 时刻另记 `F1F2_sampled=0`。
这不是按 eventual F3 关闭预测：当前温度尚未达到 F1 起始风险温度，位置在 F2 有界风险域外，
剩余计算窗口内也无模型风险。该 F3 本来就不属于 F1/F2 完成前概率所覆盖的来源。
受控测试另外验证了 eventual F3 任务的正 P_finish，防止用这个零风险样本掩盖禁用预测的错误。
A 则已固定保护该任务，故障时 rf=lf=206833 WU，REMOTE_REDO 在 0.134670001 s 后追平 xf 并完成。

### 剩余 deadline 失败并非恢复节点忙

B 失败为 120/399/727，C 多出 596。四个任务均在 OFF 状态遇到故障、STATE_MISSING，
故障后立即接受空闲节点 0 的重算，不排队等待其上已有计算任务。
等待 INPUT 属于 reserved-idle，不属于 compute queue/busy；即使不计输入传输，完整重算也超出剩余期限。

| task | 故障完成度 | 故障后剩余期限(s) | 完整重算(s) | 实际等待 INPUT(s) |
|---|---:|---:|---:|---:|
| 120 | 70.901% | 1.836282 | 3.107140 | 0.170996 |
| 399 | 32.345% | 2.180566 | 2.232930 | 0.127504 |
| 727 | 83.222% | 0.657129 | 1.404790 | 0.075957 |
| 596（C） | 33.031% | 2.364308 | 2.438210 | 0.136355 |

120 为模型零风险后实际 F3；其余未保护源于当时保护硬路径无可准入容量。
这与旧 REMOTE_BUSY 场景中“已有有效 checkpoint 却丢弃后重算”是不同原因。

## 原 5 个 REMOTE_BUSY 任务回归

旧 B 五个任务均从零重算，114/252/456/475 deadline 失败，551 完成；本轮五个均迁移成功。
字节列为真实 RECOVERY_STATE 声明/收齐字节，不是 K(rf) 的替代值。

| task | B old remote→new node | state bytes | B 路径 / T_catch(s) | C 路径 / T_catch(s) |
|---|---|---:|---|---|
| 114 | 0→6 | 489034056 | MIGRATE_TAIL / 0.462986 | MIGRATE_TAIL / 0.462986 |
| 252 | 0→1 | 155381411 | MIGRATE_TAIL / 0.178073 | MIGRATE_REDO / 0.159179 |
| 456 | 0→2 | 296766085 | MIGRATE_TAIL / 0.274348 | TAIL / 0.063236 |
| 475 | 0→1 | 344789829 | MIGRATE_TAIL / 0.332241 | TAIL / 0.074551 |
| 551 | 0→1 | 349822776 | MIGRATE_TAIL / 0.335038 | TAIL / 0.145679 |

C 的 252 在故障时 lf=rf=343672 WU，没有有效未同步 tail，故迁移 134272768 B 完整状态后
从 rf 重做；不是人为偏好 TAIL。C 的 456/475/551 原 remote 当时可直接恢复，无需迁移。
114 的新节点 6 由当前可行性和稳定 ID 顺序得到，不使用未来队列释放时间，也未改变 placement policy。

| task | B 估计 MIGRATE_TAIL(s) | B 估计 MIGRATE_REDO(s) | B 估计 RECOMPUTE(s) |
|---|---:|---:|---:|
| 114 | 0.462597 | 7.415897 | 7.412897 |
| 252 | 0.178015 | 0.868075 | 3.680707 |
| 456 | 0.269303 | 1.242483 | 1.649298 |
| 475 | 0.332112 | 0.959262 | 8.526064 |
| 551 | 0.294938 | 1.496188 | 1.502188 |

估计从当前节点准入后的状态传输/融合到 xf，不含 catchup 后的正常剩余计算；实际 T_catch
还包含 1 ns 裁决、真实分包/传播/排队。两者分列，未用实际结果倒选路径。

## 频率行为与路径准入

| 指标 | B | C |
|---|---:|---:|
| TASK_RUNNING 即时评估 | 800 | 800 |
| 即时 START / 总 START | 119 / 419 | 109 / 418 |
| never START | 381 | 382 |
| 故障时 OFF / INITIALIZING / ON | 6 / 5 / 72 | 7 / 5 / 71 |
| 所有决策 / UPDATE / 实际改变配置的 UPDATE | 4102 / 989 / 686 | 4105 / 1033 / 729 |
| OFF 重放不可用但 START 可行而启动 | 13 | 12 |
| 有利 START 提案遇当轮故障、未提交 | 4 | 6 |
| PAUSE episode / 总时长(s) | 115 / 132.159877 | 88 / 86.225535 |
| NO_ADMISSIBLE_PATH 暂停分段 / 时长(s) | 76 / 68.827182 | 78 / 69.815218 |
| REMOTE_BUSY 暂停分段 / 时长(s) | 26 / 30.603755 | 8 / 9.841523 |
| LOCAL_BUSY 暂停分段 / 时长(s) | 17 / 32.728941 | 5 / 6.568794 |
| 实际 ON 未暂停时间加权 delta / n | 5.7096% / 18.8114 | 5.6582% / 18.5733 |
| 存储拒绝任务 / 实际分配失败 | 0 / 0 | 0 / 0 |

按原因分段不能直接相加作为 PAUSE episode。OFF 重放不可用的 START 没有可比较的有限 Joff，
不混入 `Joff-Jstart` 分布。两组均保留当前真实容量门槛，未使所有高风险任务必定 START。
逐 profile、delta/n、P_finish、margin 分布保留于各组 `frequency-evaluation.json`。

## 正式运行身份与复现

三组均 LEO-66、800 任务、1300 s、在线 generate、seed=1/run=11、10 Gbps、1 ms/link、
deadline factor=1.3、每节点额外备份池 10 GB；原 F1/F2/F3 参数，F3 节点 62、1027.055770726 s。
A=FFP+fixed 5%/n4，B=FFP+CompFRR，C=LRL(lambda=1)+同一 CompFRR。
概率审计与 shadow 关闭，未使用 validation-replay。独立进程并行运行，只影响墙钟耗时。

本次三条正式运行命令各执行一次，没有为改善结果重跑：

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py --output-dir output/n5b-g3r/A-ffp-fixed --protection-mode fixed
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py --output-dir output/n5b-g3r/B-ffp-compfrr-frequency --protection-mode compfrr --placement-mode ffp
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py --output-dir output/n5b-g3r/C-lrl-compfrr-frequency --protection-mode compfrr --placement-mode lrl
```

输出包含 `execution.json`、`execution-result.json`、真实事件/任务/传输/存储/恢复/决策 CSV。
生成物按原 gitignore 留在本机，不提交大体积输出或引入 SHA-256 合同。
统一对账使用 `tests/integration/regression/analyze-frequency-evaluation.py --runs A目录 B目录 C目录`，
检查同代码、同场景及唯一允许差异（policy/output），输出 `paired-evaluation.json`。

本轮只是一个 paired seed/run 的描述性证据。R1–R4 合并复验不是逐项消融，不能把总体成本变化
全归因于某一项；LRL 仍是诊断基线，不是 N5C 最终算法；不要求不同在线策略的故障 trace 相同。
本轮新增的是 TASK_RUNNING 触发；OFF 后续重评仍在真实 fault-check，不订阅每一次
链路容量释放/节点空闲变化。未通过事后看见路径释放或实际故障时刻来提前启动保护。
R3 的备选是所选端点之间的真实 ECMP 路径，不新增跨 backup pair 的搜索或 ON 重放置；
某一 FFP/LRL 节点对的硬路径受阻，不等同于全网所有节点对都不可行。

建议：修订实现与本轮复验门禁通过，可以提交用户作 N5B 最终冻结审阅；性能结论仍是上述权衡，
不是普遍最优或多 seed 显著性结论。`main` 与 `n5` 均未变更，未运行阶段 CI；
交付停在 **N5B-G3R**，保留同一 feature 分支及 Draft PR #96。
