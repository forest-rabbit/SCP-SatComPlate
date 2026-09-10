# N5B-G3R2：共同可行节点对、容量释放重试与真实前置负载

状态：R5/R6 实现、维护测试和 B/C 正式复验通过；**STOPPED AT N5B-G3R2**，等待用户审阅。
Draft PR #96 保持 Draft，不 Ready/merge、删分支、运行阶段 CI 或进入 N5C。

## 范围、版本与场景

- 分支 `feature/n5b-compfrr-frequency`，base=`n5@b626a154d423219bc1503f060252962e1965b4cf`。
- 修订起点 `a20515a9c`；正式执行提交 `304abad8b148af5e97fa9aaa385795fb20e65255`。
  B/C 启动时工作区均干净，报告提交在执行提交之后，不改变二进制。
- R5/R6 审计中“800 任务不变”由用户后续明确批准的前置任务增量覆盖：原 800 个任务的
  每个字段完整保留，另加 400000000 B compression 任务 801，共 801 个。
  两组使用同一已提交输入；没有为不同策略分别调整任务。
- 66 星、每星 100000 WU/s、1300 s、10 Gbps、fixed 1 ms、deadline1.3、seed1/run11、
  10 GB 备份池；online generate，probability audit/shadow 均关闭。
  F1/F2/F3 参数、独立故障抽样、评分公式、成本档位、delta/n 网格及 F3 node62/1027.055770726 s 不变。
- 本轮仅正式运行 B（FFP+CompFRR）和 C（LRL+CompFRR，lambda=1）各一次。
  旧 A 的 fixed 放置语义未改、输入也少一个任务，只能作为历史工程参考，不能给出严格 A→B 因果增益。
  不运行新的 A、none 基线、all-recompute、多 seed 或参数扫描。

当前输入详情见[场景 README](../../../contrib/satcompute/input/experiments/leo-66/README.md)。
旧 [G3R 报告](N5B-G3R-semantic-corrections.md)、`output/n5b-g3r/` 和更早 G3 输出均保留。
`workload-summary.json` 更新为当前生成器可复现的摘要，删除过时的中间候选统计；
F3 manifest 的旧 none 测量明确标为历史证据，未改写成新场景结果。

## 实现与语义

R5 的 `feasible-placement-pairs.cc` 枚举相同因果快照中的全部有序节点对：两者不同且均非 primary、
健康且空闲，local 为一跳，primary→local、primary→remote、local→remote 三条硬路径可准入。
每条路径通过真实 `NetworkTransferEngine::EstimateAdmissiblePath`，复用完整 ECMP/reservation；
单次评估内缓存查询，不预留容量、不改变路径注册或随机状态。source→remote replay 仍是 OFF 成本的软条件。

- FFP：按 `(local ID, remote ID)` 排序。
- LRL：保持既有 local-first，按 `(local load, local ID, remote load, remote ID)` 排序；
  load=`activeBackup + activeRecovery`，未引入 J 或风险作为放置评分。
- 对排序后的节点对依次检查频率硬约束；存储/deadline/初始化过晚则尝试下一组，
  第一组硬约束可行者执行原 J_start/J_off 比较，即使 NONE 也不继续按 J 挑其他节点对。
- `NO_FEASIBLE_NODE_PAIR`、`NO_ROUTE`、`NO_CAPACITY_NOW` 分开，端口耗尽另报
  `SOURCE_PORT_EXHAUSTED`，不冒充可以靠释放带宽恢复的阻塞。
- node/path 计数为全部候选；`pair_hard_checked/feasible`、storage/deadline skip 仅统计
  真正执行过的排序前缀；不是对未检查节点对的可行性推断。

R6 在实际网络 reservation 释放时通知 controller，并 `ScheduleNow`，使取消/完成/拓扑批次先结束。
OFF、P_finish>0、尚有拓扑可达但容量阻塞的节点对时登记等待兴趣；不创建新 ProtectionPhase，
不预留节点、存储或网络，也不触发 START。等待集合按稳定 task ID 遍历，每任务每纳秒最多一次资源重评。
释放后重新读取进度、剩余时间、下一真实抽样网格上的 P_finish、J、路径、存储、负载及 deadline。
不复用旧提案、不新增 Bernoulli draw；初始化/ON/恢复/终态不做 OFF 重试。
START 只进入 INITIALIZING，实际收包和 cR 完成才 ON；ON 节点对与故障检查节奏保持不变。

等待单列 `frequency-capacity-waits.csv`；它不属于已接受恢复的 reserved-idle，也不计入 W_waste。
因容量未释放而继续等待时仍可在原故障检查点评估；释放但依旧阻塞则保留兴趣。
START 原因区分 TASK_RUNNING、FAULT_EPOCH、CAPACITY_RELEASE；后者当前抽样字段留空、sampled/hit=0。
详细字段与边界见 [protection README](../../../contrib/satcompute/protection/README.md)。

主要修改：共同 pair builder、FFP/LRL 排序、frequency controller/gate、网络释放通知、frequency metrics/
离线分析、生成器和输入，以及现有 policy/runtime/path/scene 测试。未修改上游 `src/`。

## 前置任务的独立物理检查

200 MB 对应 3 s 计算，从 17°C 升至约 19.84°C，仍低于当前 20°C F1 起点；因此采用用户允许的
400 MB（600000 WU、6 s）。任务 801 沿用 120 的 source54/compute62/result33，固定提前 6.2 s 到达；
是离线到达参数，不是在线读取未来 F3 或人为设置温度。RESULT 按现有压缩模型为 216992523 B。

两任务真实 network/compute + generate 检查（保护关闭，仅此小检查打开 audit）：

- 801 正常完成、真实计算 6 s；到 120 开始的间隙 45641307 ns（45.641307 ms）。
- 120 开始仍为 1024852770726 ns，未改变历史 1 ms 场景下的该开始时刻。
- 1025/1026/1027 s 的温度为 22.010537/22.646332/23.236563°C，
  pF1 为 0.0002936294/0.0005948665/0.0011099026；pF2=0（SAA 域外）。
- 120 仍在固定 F3 时刻中断。正概率只说明真实余温，不保证原频率策略判断 START 有利，
  更不意味着能预测 F3；不能为让该任务恢复成功而继续放大预热或修改故障模型。

## 本地门禁与复现

targeted build、21 个维护 C++ 程序、74 个 Python unit（含原生切片双次复现，无 skip）、
10 组 smoke、全部维护 regression 通过。新增/最终聚焦检查：policy 211065 项、frequency runtime 3321 项、
path 2027 项；原 recovery runtime 与 R1–R4 保持通过。
原 N4B 联合验收仍为 88 完成/12 失败、11 START、483 条概率一致。

新增覆盖包括首选 pair 不可用后继续、FFP/LRL 同集/稳定排序、三条硬路径、健康/空闲/一跳过滤、
真实备用 ECMP 可准入、正反枚举不改 reservation、第一组存储不够后选择下一组。
真实容量用例在 primary 开始后 3 ms 先释放无关链路（仍等待），5 ms 释放所有阻塞链路，
在 69105778 ns START，早于 100000000 ns 下一故障检查点；FFP 选 (2,0)，受控存储不足时 LRL 跳到 (2,4)。
多次同纳秒通知只重评一次，任务终态后不重试，INITIALIZING 后不重试，最终有真实初始化 commit；
F1/F2 抽样计数与温度不变、重复输出逐字节相同，等待时无 checkpoint 分配，终态资源归零。
smoke 还比较 audit on/off 原始业务与保护输出相同，验证概率 CSV 不是运行依赖且 off 会清理新 CSV。

日志/受控证据：`output/n5b-g3r2-validation/{python,cpp,smoke,regression,frequency-runtime,protection-path}.log`，
`frequency-runtime/` 及 `warm-predecessor/warmup-check.json`。所有输出按原 gitignore 忽略。

正式命令（每组本轮已完整运行一次，不能当作日常测试重复执行）：

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --output-dir=output/n5b-g3r2/B-ffp-compfrr-frequency --protection-mode=compfrr
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --output-dir=output/n5b-g3r2/C-lrl-compfrr-frequency --protection-mode=compfrr --placement-mode=lrl
.venv/bin/python contrib/satcompute/tests/integration/regression/analyze-frequency-evaluation.py \
  --runs output/n5b-g3r2/B-ffp-compfrr-frequency output/n5b-g3r2/C-lrl-compfrr-frequency
```

## 正式结果

两组 returncode=0、最后链路窗口均到 1300 s，运行及公平性检查通过。墙钟分别为
1198.841/1209.738 s（约 19.981/20.162 min）。`run_status=PARTIAL` 表示存在一个失败任务，
不是仿真未运行完。两组恰好有相同故障 trace：F1=84、F2=2、F3=1，87 条故障记录、
83 个直接受害任务；在线策略不要求 trace 必须相同。

| 指标 | B：FFP+CompFRR | C：LRL+CompFRR |
|---|---:|---:|
| 完成 / deadline 失败（共 801） | 800 / 1 | 800 / 1 |
| 接受恢复 / 成功恢复 | 83 / 82 | 83 / 82 |
| 故障时 OFF / INITIALIZING / ON | 1 / 5 / 77 | 1 / 5 / 77 |
| TAIL / REMOTE_REDO | 68 / 5 | 71 / 4 |
| MIGRATE_TAIL / MIGRATE_REDO / RECOMPUTE | 3 / 1 / 6 | 1 / 1 / 6 |
| 正常保护等效 WU | 441260 | 461280 |
| 恢复 reserved-idle 等效 WU | 564775.2136 | 497533.9046 |
| actual catchup 重做 WU | 1415839 | 1147812 |
| **W_waste_actual（以上三项之和）** | **2421874.2136** | **2106625.9046** |
| planned / actual total recovery WU | 24868465 / 24724279 | 24600438 / 24456252 |
| actual post-catchup WU（不计 waste） | 23308440 | 23308440 |
| T_catch mean / P50 / P90(s) | 0.219145 / 0.096213 / 0.435364 | 0.178258 / 0.095086 / 0.418448 |
| 已观测 catchup / 未到达 | 82 / 1 | 82 / 1 |

两组 dense/sparse/LLM 分别为 240/240/80 个完成；compression 为 240/241。
RECOMPUTE 的 6 个任务均为 120、247、548、564、591、734；后五个在真实初始化完成前遭故障，
之后重算完成，只有 OFF 的 120 超时。失败任务只计真实执行前缀；不把未到达 catchup 计作 0。
全部 6 次迁移均成功，原 R4 有效状态优先、真实字节/cR/一次恢复与终态清理保持。

**B→C**：完成数相同，waste 降低 13.0167%，actual catchup 降低 18.9306%，
reserved-idle 降低 11.9059%；正常保护成本增加 4.5370%，保护发送量增加 1.3158%。
这是单 seed/run 的放置诊断，不是多 seed 显著性或全局最优声明。
相对旧 G3R，原有 800 个任务中 B 完成 797→799、C 完成 796→799，另有新增 801 正常完成；
不能把新增任务算成 R5/R6 挽回的任务。

### START、候选节点对与等待

| 指标 | B | C |
|---|---:|---:|
| 总决策 / TASK_RUNNING 评估 | 4103 / 801 | 4103 / 801 |
| START：TASK_RUNNING / FAULT_EPOCH / CAPACITY_RELEASE | 149 / 276 / 0 | 147 / 277 / 0 |
| 总 START / never START | 425 / 376 | 424 / 377 |
| OFF 评估次数 | 2848 | 2858 |
| 每次总有序节点对 | 4160 | 4160 |
| node-feasible mean | 215.558 | 215.626 |
| path-feasible mean / P50 / P90 | 202.972 / 186 / 248 | 203.053 / 186 / 248 |
| 实际 frequency-hard 检查次数（排序前缀） | 20176 | 20307 |
| 找到 frequency-hard 可行节点对的评估次数 | 2768 | 2777 |
| 候选对 skip：node / capacity | 11233771 / 35846 | 11273022 / 35932 |
| 候选对 skip：no-route / other / storage | 0 / 0 / 0 | 0 / 0 / 0 |
| 候选对 skip：deadline/初始化过晚 | 17408 | 17530 |
| 全部节点对暂被阻塞（NO_CAPACITY_NOW）决策 | 0 | 0 |
| capacity wait 任务 / retry / retry START | 0 / 0 / 0 | 0 / 0 / 0 |
| capacity wait 总时长 / mean / P90 | 0 / — / — | 0 / — / — |

skip 为跨评估的候选对累计拒绝次数，不是失败任务数；同一节点对可被不同时间的评估重复统计。
本场景虽然有不可准入的个别节点对，但完整可行集始终还有其他选择，故没有触发 R6 等待。
**不能把本轮正式收益归因于 capacity-release retry**；它由 5 ms/部分释放/重复/终态受控用例验证。
空等待样本的 mean/P90 不定义，不能写成“平均等待 0”。ON 的全部决策仍为 FAULT_EPOCH，未改频率节奏。

### 重点任务回归

时间单位 s；括号为选中的 (local, remote)。这六个任务两组均在故障时 ON、通过 TAIL 恢复并完成。

| task | B 的 START | C 的 START | 解释 |
|---|---|---|---|
| 325 | 668.599770996，TASK_RUNNING (21,1) | 同 B | 不再因首选 pair 不可用而结束搜索 |
| 399 | 59.277757046，TASK_RUNNING (8,4) | 同 B | 旧 B/C 的 deadline 失败消除 |
| 470 | 65.232792390，TASK_RUNNING (41,0) | 同 B | 选可行 pair，替代旧 OFF 重算 |
| 645 | 463.267846702，TASK_RUNNING (10,0) | 同 B | 选可行 pair，替代旧 OFF 重算 |
| 727 | 203.000000000，FAULT_EPOCH (41,0) | 同 B | 202.830901544 s 首次因 J 不合算而 NONE，不是容量等待；203 s 比较有利后启动 |
| 596 | 860.194635269，TASK_RUNNING (0,1) | 860.194635269，TASK_RUNNING (21,1) | C 改用其排序下的可行 pair，旧 C 的额外失败消除 |

### 任务 120：不再冷启动，但不是高概率强制保护

正式 B/C 中 801 的真实计算时长和 45.641307 ms 间隙与独立检查完全一致，801 未启动保护且正常完成。
120 的 INPUT=207142024 B，故障前已完成 220300/310714 WU（70.901215%）。两组决策相同：

| 时间 s | P_finish（概率值） | J_off(s) | J_start(s) | 决策 |
|---|---:|---:|---:|---|
| 1024.852770726 | 0.00199723785 | 0.000330970 | 0.009894675 | NONE |
| 1025 | 0.00199723785 | 0.000625003 | 0.009563007 | NONE |
| 1026 | 0.00170410878 | 0.002237382 | 0.007252206 | NONE |
| 1027 | 0.00110990255 | 0.002567131 | 0.004881909 | NONE |

始终为 `OFF_NOT_MORE_EXPENSIVE`：开始时完成前风险约 **0.199724%**，不是 19.97% 或 50%。
实际 F3 时刻的因果 q_comp 快照约 **0.114707%**，pF2=0，明确未新增 F1/F2 抽样。
该时刻 P_finish=0（原始 CSV 的 `-0` 为浮点零）：下一真实检查点 1028 s 已晚于预计主计算完成
1027.959910726 s；不代表温度被清零或不再预测 F1/F2。

F3 后剩余 deadline 1.836282 s，空闲 node0 在下一纳秒接受重算；INPUT 实传 0.170996204 s，
完整计算仍需 3.10714 s，故必然来不及。到 1028.892052726 s 超时前实际重做 166528 WU，
对应 waste=183627.6204 WU；不是 node0 正在排队或等待别的计算任务。
保留此结果，不读未来 F3、不强制 START，也不为消除这一个失败再修改模型/预热任务。

### 网络、存储、集中度与守恒

| 实际发送十进制 GB | B | C |
|---|---:|---:|
| INIT_BASE | 113.778247235 | 114.038205241 |
| INIT_STATE | 24.380882893 | 24.302122737 |
| L1 | 54.751767671 | 56.449687001 |
| REMOTE_BATCH | 26.830699811 | 28.529850285 |
| RECOVERY_INPUT | 1.374020116 | 1.374020116 |
| RECOVERY_TAIL | 3.029319959 | 3.112222202 |
| RECOVERY_STATE | 1.317919429 | 0.623306824 |
| **保护/恢复总计（不含 RESULT）** | **225.462857114** | **228.429414406** |
| 恢复 RESULT（另计业务结果） | 9.865661479 | 9.865661479 |

RECOVERY_STATE 的声明/发送/接收字节相等；其他保护流因故障取消可能 sent≠received，
这不等价于 FlowMonitor Drop。各组唯一逻辑终态、planned/actual WU、真实传输终态、
迁移字节和 cR 屏障、正常成本事件分账、placement 所有权及最终清理均通过既有分析器对账。

| 指标（含全部 66 星的零计数星） | B | C |
|---|---:|---:|
| 单节点共享备份池最大峰值 B | 2039466341 | 1754456798 |
| 分配失败 / 终态 used / reserved | 0 / 0 / 0 | 0 / 0 / 0 |
| 单节点累计 backup 最大值 / top-3 份额 | 282 / 96.7059% | 175 / 93.3962% |
| 单节点累计 recovery 最大值 / top-3 份额 | 45 / 92.7711% | 41 / 89.1566% |
| FlowMonitor tx=rx 包数 | 13655836 | 14013204 |
| FlowMonitor 显式 Drop / lost / 未归因 lost | 0 / 0 / 0 | 0 / 0 / 0 |
| ISL 窗口 drop packets | 0 | 0 |
| 网络活动路径 / pending / reservation | 0 / 0 / 0 | 0 / 0 / 0 |

65 份已应用拓扑切片，路由计算总计 2 次（初始与 F3）；未增加随机拓扑或 routing 更新。
`protection-finalization.json` 均 quiescent=true，placement active counts 均为 0。
F3 的永久故障状态按合同保留，不要求故障本身在仿真结束恢复。

## 结论与阶段边界

实现与复验门禁通过，建议进入用户的 N5B 审阅/冻结决策，但不自动冻结或合并。
现有场景可以保留为“有真实前序负载、仍允许低风险 F3 超时”的新实验输入；不是全任务必保场景。
R6 正式触发数为 0、只做了单 seed/run、LRL 仍有较强集中度、没有新的严格 A 基线，均是证据边界。
瞬时只读可行性不是多路径同时预约，不预知未来队列；ON 不因释放事件换 pair，单次恢复合同不变。
旧 R1–R4 回归保留。本轮执行的全部运行仅为维护/受控检查、一次两任务余温检查和正式 B/C 各一次。
分析结果在各组 `frequency-evaluation.json` 与 `output/n5b-g3r2/paired-evaluation.json`；
原始 CSV/JSON 留在本地，不提交体量大的仿真输出。

**STOPPED AT N5B-G3R2**；更新原 Draft PR #96 后等待用户审阅，不进入 N5C。
