# Selective INPUT 初始化预置：离线审计

2026-09-14。以下首轮记录保留；后续获准拆分 A0/A1，Stage A 结果见文末。
首轮状态：`STOP_RECONSTRUCTION_INSUFFICIENT`，停在原任务书第 21 节覆盖率门禁。

结论：能够识别完整 START population、恢复总风险并建立实际 INPUT 等待标签，
但历史日志不足以完成所有候选的逐故障点收益重建。本轮不能判断 selector 的 Precision/Recall，
也不能据此判定“运行时模型没有足够信息”，更不能直接回到动态 JIT。

## 范围与身份

工作分支 `feature/compfrr-input-worthiness` 从 `n5@aa7a49a1c`（#103）创建，
离线工具提交 `b02ec3d6f`。本轮遵循 Selective INPUT taskbook 及已接受的四项审阅补充，
替代旧的滚动 ON Worthiness 计划：只审计成功 START、初始化之前的一次决策，
Deferred checkpoint layout + 独立完整 INPUT，端点是 source → designated remote。
既有 Eager layout、START/Frequency/placement/recovery、参数和 RNG 均未修改。

原始目录：`output/v7-cbsat-adjustment/20260913-jit-formal/`。
三组共同的执行提交为 `367f23f393c45205cf87fbee003bfb668f453d5e`，不是 corrected N5R 仿真。
均为 seed 1 / run 11、800 任务、1300 s、FA-LRL、relocate、online generate；
执行成功、工作区干净、概率审计关闭。核验了 CLI 非策略参数、任务静态定义/原始 deadline budget，
并逐行确认三组实际 `fault-events.csv` 相同。

| 守恒项 | V6START：主组 | Full-V7：独立辅助组 |
| --- | ---: | ---: |
| 全部任务 | 800 | 800 |
| committed + physical START + ACCEPTED 三方确认 | 409 | 413 |
| TASK_RUNNING / FAULT_EPOCH 触发 | 389 / 20 | 390 / 23 |
| START 后主任务故障 | 83 | 83 |
| NEEDED：Deferred INPUT 确在关键等待路径 | 71 | 71 |
| FAULT_NONCRITICAL：故障但 INPUT 无额外等待 | 12 | 12 |
| NO_FAULT | 326 | 330 |
| UNKNOWN 标签 | 0 | 0 |
| 历史确有 prefetch 生命周期 | 343 | 347 |
| 历史 INPUT 被恢复实际使用 | 16 | 16 |
| START source 与 remote 同星 | 8 | 8 |

没有把旧 JIT 已发送的子集当作 population，也没有用 Full-V7 后续记录补 V6START 特征。
83 个主任务故障为 82 个 F1、1 个 F3；F3 保留为 outcome tag，不注入 F1/F2 预测质量。
标签要求主任务故障、原 deadline、实际 recovery target/path 严格匹配，并有 accepted recovery
及真实依赖时间；关键等待为 `max(0, INPUT dependency ready - state ready)`。
71 个 NEEDED 的 Deferred 实际 INPUT 关键等待总和为 **18,186.406988 ms**，不是可节省时间。
历史配对 catch 为 15 改善、67 相同、1 变差，属于描述性历史对照，不是新方案效果。

身份核验不等于整条轨迹相等：V6START 的任务 232 local 为 8（anchor 为 10），
任务 322 remote 为 1（anchor 为 3），二者均 NO_FAULT。
任务 298/694 的派发时间及绝对 deadline 有差异，但不在两组 START population 中；
原 deadline budget 相同，83 个故障配对的绝对 deadline 均一致。
Full-V7 有 15 个 START 对照差异项，包括 anchor 中不存在的新增候选，单独记录，不池化。

## 因果重建覆盖率

| 字段 | V6START 已知 / 总数 | Full-V7 已知 / 总数 |
| --- | ---: | ---: |
| 唯一 START、固定 local/remote、初始 cadence、任务量与原 deadline | 409 / 409 | 413 / 413 |
| 精确剩余计算时间、下一合法抽样点、`P_F` | 409 / 409 | 413 / 413 |
| `q_next` 与完整后续联合首次故障质量序列 | 16 / 409 | 17 / 413 |
| START 时 INPUT serialization estimate | 390 / 409 | 391 / 413 |
| 固定 cadence 的初始化有效性及 state/tail 预测输入 | 0 / 409 | 0 / 413 |
| `P_Iimpact`、`G_I`、`P_Iddl` | 8 / 409 | 8 / 413 |

最后一行的 8 个已知值仅是 LocalDelivery 恒等情况：两种 INPUT 等待均为零，
INPUT-only gain / deadline rescue 为零；没有将任何跨星缺失值补零。
LocalDelivery 的计划网络字节为零，网络 byte density 不适用，在 CSV 中留空并写明原因。

### 可以证明的部分

- 非 epoch 查询沿用 `QueryTaskPrediction()` 的 pending/next canonical grid 和 finish-exclusive 窗口，
  不新增抽样。主组 389 个 TASK_RUNNING 均不在整数秒边界；合成测试另覆盖 pending 同刻检查。
- FAULT_EPOCH 的 20 个 START 已存活当前检查，必须剔除该点。
  本批原完成时间均不落在检查网格上，因此总质量可由
  `P_future = (P_logged - q_survived) / (1 - q_survived)` 恢复。
  这是条件生存后的绝对故障概率，不是归一化为“未来必故障”；完成点恰好落网格的合成例明确返回 UNKNOWN。
- 剩余窗口只有一个检查点时，`q_next = P_F`，该点首次故障质量也等于 `P_F`。
  主组有 16 个这样的候选；不能据此拆出 F1/F2 各自概率或推导多点序列。
- 固定历史 controller 的 TASK_RUNNING / CAPACITY_RELEASE 是同步 Evaluate → AfterEpoch，
  source → remote 的提案速率可用于该次提交前的 serialization 估计；并非 receiver-ready 或实际准入保证。
  FAULT_EPOCH 中间可以发生批量故障和其他任务准入，不能把提案带宽直接升格为 post-batch 快照。
  主组 20 个 epoch 候选中 1 个 LocalDelivery 可直接确定零等待，余下 19 个留 UNKNOWN。

### 不能替代缺失信息的部分

历史概率审计关闭，CSV 只有当前概率和累计概率等摘要，未保存完整 predictor steps 或 START 的完整
F1/F2 model state。多点累计值不能唯一确定每点的 `q_k` / `w_k`。历史 task/recovery events
可提供部分过去状态线索，但尚无经验证的 prefix-state 重放器，不能宣称已完整重建。

START 不是 checkpoint-ready。任务书要求在同一个固定 cadence 下比较未来各检查点的两种 INPUT 等待，
还需要合法的初始化完成估计、local/remote progress、state/tail 字节与对应时间。
初始 cadence、单个 `t_init_s`、`predicted_recovery_s` 摘要不足以给出这些轨迹；
后续真实 checkpoint/ON UPDATE/实际 recovery target 不能替代它们。

当前 [并行恢复 estimator](../../../contrib/satcompute/protection/runtime/checkpoint-recovery-estimate.h)
只对调用者给出的时间做并行 join、完整 compute-deadline feasibility 运算，不预测未来 checkpoint。
因此本轮没有另写串行近似或预测器 surrogate，也没有把初始化之前的质量丢掉后重新归一化。
后续若证据齐备仍须复用这个 estimator：有限 catch 差与 deadline-rescue mass 分账，
REMOTE_REDO/TAIL 严格沿用当前选择及同值优先 REDO 的规则。

源码依据：历史执行的 `protection/runtime/frequency-protection-controller.cc` 中
OnTask、BuildResources、AfterEpoch；当前对应 [CompFRR controller](../../../contrib/satcompute/protection/policy/compfrr/compfrr-controller.cc)。
抽样窗口见 [FaultModelEngine](../../../contrib/satcompute/fault/runtime/fault-model-engine.cc)，
风险轨迹见 [canonical predictor](../../../contrib/satcompute/fault/model/compute-failure-predictor.cc)。
历史至 N5R 的 fault diff 只有只读 exposure 查询扩展，预测模型与抽样合同未改变。

## 产物、验证与停止点

最终证据：`output/audits/compfrr-selective-input-init-run11-verified/`（ignored，不写回原始目录）。

- `source-identity.json`、`summary.json`。
- `candidate-start-snapshots.csv` / `candidate-labels.csv` / `candidate-features.csv`：各 822 行，cohort 显式分开。
- `feature-reconstruction-audit.csv`：19,728 行，逐候选逐字段的值、来源、状态与缺失原因。
- `score-sweeps.csv` / `reference-points.csv` **未生成**，summary 显式记为 `SKIPPED_RECONSTRUCTION_GATE`。
  不用空曲线冒充分析成功，也不借唯一已知的 `P_F` 绕过联合收益覆盖率门禁。

初次扫描目录 `output/audits/compfrr-selective-input-init-run11/` 保留为中间产物；
它尚未识别单点窗口的可重建性，以上 `-verified` 为最终证据。
119 个原始文件的大小及修改时间在最终审计前后完全一致；这只是只读检查，不是 SHA/安全性机制。

验证通过：目标模块 build；230 项维护 Python tests（1 项缺少外部位置切片的既有 skip，含 17 项新增合成测试）；
完整维护 C++ test runner。后者包含 canonical predictor、并行恢复/deadline/缺失路径等既有合同测试。
新增合成测试覆盖 source 分组、唯一/真实准入 START、窗口条件化、单点重建、禁止未来 outcome 入特征、
互斥标签、LocalDelivery、UNKNOWN、覆盖率停止与输出不可覆盖；未实现的 score/收益计算不宣称已验收。
未改变 production/fixture/外部 schema，未启动新正式仿真、GitHub CI、PR 合并或 runtime Phase 2。

若获下一轮授权，最小补证方向是**被动记录** START 提交前的完整 canonical predictor steps/状态、
post-batch 的 source→actual remote 只读路径快照，以及固定初始 cadence 的因果初始化/state/tail 估计输入。
先验证记录与估计合同不改变原运行语义，再决定如何采集新证据；本轮不实施记录器、不运行新矩阵，
不新增优化器、阈值或动态 reevaluation。

未来即使通过覆盖率：计划预置字节不等于实际流量；未覆盖 anchor 关键等待的计划字节不称为实际浪费；
captured critical wait 不称为实际节省时延；ALL_STAGE 不等于 Eager，ORACLE_NEEDED 只是标签参考，
不是可实现恢复性能上界。所有网络字节指标均排除 LocalDelivery。

## Reduced Worthiness Audit: P_F-only separability（Stage A）

用户批准 PF / instrumented run11 任务书及三项修正后，A0 与 A1 分开。
A0（source、成功 START、标签、P_F、任务静态量和 NEEDED 的实际等待）全部通过；
A1 的复杂 `G_I / P_Iimpact / P_Iddl` 仍不完整，仅跳过 advanced ranking。
历史 FAULT_EPOCH 使用 finish-inclusive PredictionInput，另两种 trigger 使用 finish-exclusive QueryTaskPrediction；
分析器显式区分来源并补齐 aligned-finish 两种合成测试，没有修改 production predictor。

产物：`output/audits/compfrr-selective-input-init-run11-reduced/`。
完整 sweep 覆盖 ALL_FAULT 405 个唯一 cut、F1_F2_PREDICTABLE 404 个唯一 cut，共 809 行；
并输出 6 个 NONE/ALL/ORACLE reference。分值相同的任务同时选入，不按 task ID 拆分平分。
下面只是预先固定 Recall landmarks 的描述性展示，所有 cut 都在 CSV，不推荐任何 production threshold。

| 视图 / 诊断点 | 选中 / NEEDED | Precision | Recall | 计划网络 GB | 计划 NO_NEED GB | BytePrecision | 捕获实际等待 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ALL / ALL_STAGE | 409 / 71 | 17.36% | 100% | 106.685 | 83.135 | 22.07% | 100% |
| ALL / 首个 ≥80% Recall | 120 / 57 | 47.50% | 80.28% | 33.176 | 14.211 | 57.16% | 81.27% |
| ALL / 首个 ≥90% Recall | 178 / 64 | 35.96% | 90.14% | 46.628 | 24.964 | 46.46% | 92.35% |
| ALL / 首个 100% Recall | 275 / 71 | 25.82% | 100% | 70.147 | 46.597 | 33.57% | 100% |
| F1/F2 / ALL_STAGE | 408 / 70 | 17.16% | 100% | 105.885 | 83.135 | 21.49% | 100% |
| F1/F2 / 首个 80% Recall | 119 / 56 | 47.06% | 80% | 32.763 | 14.211 | 56.63% | 82.37% |
| F1/F2 / 首个 90% Recall | 165 / 63 | 38.18% | 90% | 43.269 | 22.088 | 48.95% | 93.62% |
| F1/F2 / 首个 100% Recall | 208 / 70 | 33.65% | 100% | 54.442 | 31.693 | 41.79% | 100% |

ALL 视图的 90% Recall 点较 ALL_STAGE 少选择 231 个任务、计划网络字节减少约 56.29%，
且捕获约 92.35% 的历史 INPUT 等待。说明 **P_F 在这批 START population 中有描述性筛选价值**，
值得保留为下一阶段核心输入；不是经过独立 runs 验证的泛化结论，也不是实际节省流量/恢复时间。

唯一 F3 为 task120：NEEDED、P_F=0.018517819600187、实际关键等待 645.504897 ms。
ALL 分母为 409 candidates / 71 NEEDED / 18.186406988 s；
F1/F2 视图只排除该 out-of-model hazard，保留全部 326 个 NO_FAULT 和 12 个 FAULT_NONCRITICAL，
分母为 408 / 70 / 17.540902091 s。F3 不计为 F1/F2 predictor 错误；它尤其影响 ALL 的全 Recall 尾部，
但不改变本批 P_F 有筛选价值的描述性结论。ORACLE 的计划网络量分别为 23.550 / 22.750 GB；
NONE 的零选中 Precision/BytePrecision 未定义，输出 null，不伪设为零或一。

Stage A 验证：build no-op；234 项 Python tests（1 个既有外部切片 skip），
其中 21 项 selective/PF 合成测试通过。旧 409/71/12/326 统计未变，未启动仿真。
下一步仅实施默认关闭的被动记录并验证 no-op，再运行一次最新底座 development/calibration run11。

## Deferred passive START trace（Stage B）

记录器按 `committed → post-batch revalidation → actual pair fixed → snapshot → START_CHECKPOINT → admitted`
顺序执行，仅成功准入的任务输出一次。新 `input-start-snapshots.json` 默认关闭；不复用 reference pair
冒充 actual pair。实际 post-batch storage/rate 与 Frequency 提案 inputs/estimates 分开存储；
未来概率直接调用现有 QueryTaskPrediction/canonical predictor，显式输出完整 F1/F2/union 序列、
first-sample 及 finish-exclusive 合同，没有新增 checkpoint forecaster 或 recovery surrogate。

实施 gate：build、243 项 Python tests（1 个既有 skip）、维护 C++ contracts、16 组 placement smoke 通过。
与 corrected N5R 小基线比较 1,965 文件（1,707 CSV）一致；logging on/off 同样全量一致，
仅增加 16 个审计 JSON。fixture 的 18 个成功 START 已逐个检查轨迹和实际 pair。
原 fault/probability、Frequency、placement、checkpoint/recovery、bytes/WU/storage、routing 和时序均未变。

唯一一次完整采样已完成：clean execution `e8a90d46657d31e7c8cee8f51d06f421cc06f67c`，
目录 `output/compfrr-input-worthiness/20260914-run11-instrumented/`，1300s、退出 0、耗时 954.433s。
CompFRR / N5C FULL / Deferred / relocate / seed1-run11；复用 `checkpoint-maintenance-fixed/run-11/full`
的全部非记录参数，三个版本化场景输入逐字节一致。purpose 为 DEVELOPMENT_CALIBRATION，
**不是 final performance**。运行中只补充离线审计和合成测试，未改生产代码或重建正在执行的库。

最终审计：`output/audits/compfrr-input-run11-instrumented-verified/`；此前不带 `-verified` 的审计目录
保留为中间产物，最终版额外检查全部 runtime JSON 与输出文件集合。产物包括 source-identity、
summary、runtime-equivalence、runtime-accounting、candidate-features/labels、完整 PF sweep/reference。
28 个原始 CSV 与 corrected canonical 逐字节一致，7 个 runtime JSON 一致（仅忽略 wall-clock）；
execution 元数据及新增审计文件单列，不混入语义比较。现有 helper 独立检查了 4,130 条 Frequency 决策、
placement/quota、实际 WU/bytes/storage 与恢复账本；800/800 完成、83/83 恢复成功，
TAIL/REMOTE_REDO/MIGRATE_TAIL 为 73/8/2，无 Recompute。没有因 instrumentation 改变故障实现或时序。

409 个成功 START（389 TASK_RUNNING、20 FAULT_EPOCH），无重复或未准入候选；
全部有 actual post-batch validation、完整 canonical 轨迹，共 2,056 个未来采样点。
393 个 actual pair 不同于 Frequency reference pair；405 个跨星当前路径可准入、4 个 LocalDelivery。
20 个 epoch START 的 initial state 非零；快照仍为初始化前，不等同于 checkpoint-ready。
本组所有 first sample 均为 NEXT_CANONICAL，finishExclusive=true；pending-current 分支由合成测试验证。

新组为 **409 / 72 NEEDED / 11 FAULT_NONCRITICAL / 326 NO_FAULT / 0 UNKNOWN**；
F1/F2 视图为 **408 / 71 NEEDED**，保留全部 no-fault 负样本。
F3 仍仅 task120、P_F=0.018517819600187、INPUT critical wait=645.504897ms，单列为 out-of-model hazard。
注意：Stage A 的旧组采用 **FA-LRL**，Stage B 采用 **N5C FULL + corrected maintenance**，
不是同一 placement/机制版本，不能把 71→72 单独归因于某一修复，更不是本轮记录器导致的变化。
相对旧 Deferred anchor，30/85/192 从 NEEDED 变为 NONCRITICAL，46/187/402/727 反向变化；
旧组原有 409/71 统计不改写，两组不合并为独立样本或公平算法对照。

新组再次完整 sweep 405+404 个唯一 cut，6 个 reference；以下仍只是固定 Recall landmarks，**不选阈值**：

| 视图 / 诊断点 | 选中 / NEEDED | Precision | Recall | 计划网络 GB | 计划 NO_NEED GB | BytePrecision | 捕获实际等待 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ALL / ALL_STAGE | 409 / 72 | 17.60% | 100% | 106.721 | 82.930 | 22.29% | 100% |
| ALL / 首个 ≥90% Recall | 165 / 65 | 39.39% | 90.28% | 43.923 | 22.354 | 49.11% | 91.43% |
| F1/F2 / ALL_STAGE | 408 / 71 | 17.40% | 100% | 105.921 | 82.930 | 21.71% | 100% |
| F1/F2 / 首个 ≥90% Recall | 163 / 64 | 39.26% | 90.14% | 43.354 | 22.052 | 49.14% | 93.60% |

ALL 的该诊断点较 ALL_STAGE 少 244 个任务，**计划**网络字节减少 58.84%；捕获的是观察到的等待，
不是实际节省时延。全组观察等待 17.987768934s，F1/F2 组 17.342264037s；
ORACLE_NEEDED 计划网络量为 23.790/22.990GB。没有真正发送任何 Selective INPUT 来验证这些潜在收益。

A0 完整；A1 仍不完整。虽然现在风险轨迹、当前路径、初始化和资源估计 inputs 齐全，
它们仍不直接给出每个未来风险时刻的合法 checkpoint receipt/state/tail。`G_I / P_Iimpact / P_Iddl`
跨星值保持 UNKNOWN，advanced ranking skipped；LocalDelivery 的零 INPUT-only 网络收益单独保留。
没有为了填值另造 checkpoint predictor 或 recovery surrogate。

最终维护 Python tests 为 244 项（1 个既有 skip），包含 10 项新 trace 合成测试；
两个阶段均已完成并停止。**不实现 production Selective INPUT、不挑 threshold、不启动正式矩阵/CI**。
未来若基于本组设计规则，正式性能验证必须另用独立 runs（例如12–15），先等待人工审阅。

## Profile and INITIALIZING-based Local INPUT Value Audit

按人工确认的 Co-initialization v2 及三项修正完成**纯离线扩展**。复用上述 Stage B 的原始运行及
`-verified` 证据，不新增仿真、不改生产代码，不混入 Stage A 的 FA-LRL cohort。
最终产物为 `output/audits/compfrr-input-coinitialization-value-run11/`：12 个 CSV 和 `summary.json`，
包括时间来源、profile 分解/分位数/旧 P_F landmarks、逐任务 timing、固定 screen、完整四分数排名、
排名构成和静态物理尺度。原始证据的文件大小/修改时间在分析前后不变；没有新增 SHA-256。

### 时间与模型边界

逐个关联 snapshot、physical START、ACCEPTED placement 和 protection task summary，
并检查执行版本 `e8a90d466` 的 controller/manager 源码：**409/409 同 ns、同 actual pair**。
START 日志在初始 reservation 前产生，单独不能证明准入；必须再有 active inventory/ACCEPTED。
因此当前 run 的 `t_init_start` 可由同步合同因果确定，但仍与 decision snapshot 分别命名。
一般情况下若实际初始化更晚、且时间在决策时未知，保留 retrospective 时间，timing feature 为 UNKNOWN；
若有明确的 decision-known schedule，使用它计算提前量。故障早于或等于初始化时，提前量为零，
其首次故障质量仍保留，不能删去或重新归一化。

这里的 co-initialization 是假想完整 INPUT 可从准入时开始，不等待 cL；**不是两条 UDP 同时启动**。
20 个非零初始 state 仍先经过 cL，再传 INIT_STATE，最后 cR merge；389 个初始 state 为零，不创建
INIT_STATE UDP，但保留既有生成/合并生命周期。INPUT 不增加逻辑 ON-ready barrier，不能据此保证
它不会因争用带宽而延迟实际初始化。本轮没有创建 INPUT flow 或模拟争用。

四个指标只使用 actual pair 的当前 post-batch rate 和 canonical future first-failure weights：
`T_I = INPUT bytes / 当前 bytes/s`，每个风险时刻的发送提前量为 `max(0, t_k - t_init_start)`；
`U_pot` 是按首次故障质量加权的可提前发送比例，`G_I_pot = T_I × U_pot`，
`M_pot = U_pot - (1-P_F)`。full/partial/zero-lead 三类质量之和为 P_F。
不计 propagation，不重新预测 checkpoint/state/tail，不使用最终故障/恢复目标生成特征。
这不是实际 catch reduction，也不是实际恢复收益的严格上界；exact U/G_I/P_Iimpact/P_Iddl 仍 UNKNOWN。

### Profile 与物理尺度

系统级仍为 ALL 409/72、F1/F2 408/71。四分数比较统一用跨星 ALL 405/72、F1/F2 404/71；
4 个 LocalDelivery（3 NO_FAULT、1 NONCRITICAL）单列，网络字节为零、normalized U/M 为 N/A。
NO_NEED 包含 NO_FAULT 与 FAULT_NONCRITICAL；它表示 anchor 不依赖该 INPUT 等待，不等于实际浪费字节。

| Profile | 候选 / 跨星 | NEEDED / NONCRITICAL / NO_FAULT | NEEDED 比例 | P_F 中位数：NEEDED / NO_NEED | 计划 INPUT GB / NO_NEED GB |
| --- | ---: | ---: | ---: | ---: | ---: |
| compression | 118 / 117 | 31 / 0 / 87 | 26.27% | 0.6019 / 0.0351 | 38.846 / 26.391 |
| dense-image | 99 / 98 | 13 / 0 / 86 | 13.13% | 0.6524 / 0.0742 | 28.263 / 24.341 |
| sparse-inference | 133 / 131 | 23 / 1 / 109 | 17.29% | 0.8559 / 0.0156 | 39.611 / 32.198 |
| llm | 59 / 59 | 5 / 10 / 44 | 8.47% | 0.6216 / 0.0355 | 35,113 Byte / 32,162 Byte |

**Q1：P_F 不只是区分 profile。**各类内部 NEEDED 的中位风险都高于 NO_NEED。
沿用 Stage B 全局 ≥90% Recall cut，不做 per-profile 调参，四类分别选中 52/44/47/22 个任务，
覆盖 26/31、11/13、23/23、5/5 个 NEEDED；说明各类内部仍有筛选信息。
profile 的基准发生率也不同，单批描述性统计不足以量化“主要”由哪一因素贡献；LLM 仅 5 个正样本尤其有限。
完整 min/P10/P25/P50/P75/P90/max、样本数和小样本提示见 CSV。

| Profile（跨星） | INPUT serialization 中位数 | Kvar / INPUT 中位数 | 全状态 serialization 参考中位数 | 第一个风险点前已发送完 / U≈P_F |
| --- | ---: | ---: | ---: | ---: |
| compression | 241.326 ms | 0.542481 | 130.915 ms | 89 / 90（共117） |
| dense-image | 226.474 ms | 1.000008 | 226.476 ms | 74 / 76（共98） |
| sparse-inference | 238.611 ms | 0.001869 | 0.446 ms | 98 / 103（共131） |
| llm | 0.5096 μs | 380,154.629 | 196.162 ms | 59 / 59 |

所有跨星当前 INPUT/backup rate 均为 1.25 GB/s；propagation 为 1–10 ms，只作为描述列，未加入主公式。
cR、initial state/INPUT 和 future-check lead 分布已输出。全状态参考不是未来真实 tail；
LLM 的小请求与其较大的 KV 状态不矛盾。

**Q2/Q3：U_pot 增加了有限的提前量信息。**320/405 在第一个 future check 前已完成假想 serialization，
328/405 的 U_pot≈P_F（绝对差≤1e-12，仅用于诊断，不是排名阈值）；两者差别来自部分早期采样的风险质量为零。
剩余 77 个的差值可见，最大 P_F-U_pot=0.227888，但对下表主要 Recall landmarks 的排名影响很小。
完整 sweep 不做 epsilon 分箱、不按 task ID 拆同分；零 timing loss 时利用等价式 U=P_F 保持数学同分。

**Q6：LLM 的归一化潜力并不低，绝对 serialization 潜力很低。**59 个请求为 325–845 Byte，
T_I=0.260–0.676 μs，最早 future-check lead 为 563.377 μs。
因此全部 U=P_F；G_I_pot 中位数仅 0.0205 μs、最大 0.5038 μs。
实际 LLM INPUT critical wait 仍可能是毫秒级，因为实际传输包含 propagation 等本轮未建模因素，不能混为一谈。

### 四分数公平比较与固定 screen

以下 ALL 比较分母均为 **405 network / 72 NEEDED**；GB 使用十进制。
所有字节是计划完整预置量，等待比例是捕获的实际 anchor INPUT-critical-wait，**均不是实际干预后的节省**。

| 首个 Recall landmark | 分数 | 选中 / NEEDED | Precision | 计划 GB / NO_NEED GB | BytePrecision | 捕获等待 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| ≥80% | P_F / U_pot / M_pot | 119 / 58 | 48.74% | 33.151 / 14.211 | 57.13% | 80.54% |
| ≥80% | G_I_pot | 110 / 58 | 52.73% | 39.357 / 17.732 | 54.94% | 91.54% |
| ≥90% | P_F / M_pot | 165 / 65 | 39.39% | 43.923 / 22.354 | 49.11% | 91.43% |
| ≥90% | U_pot | 164 / 65 | 39.63% | 43.621 / 22.052 | 49.45% | 91.43% |
| ≥90% | G_I_pot | 175 / 65 | 37.14% | 56.909 / 33.330 | 41.43% | 99.49% |
| 100% | P_F / U_pot / M_pot | 272 / 72 | 26.47% | 70.172 / 46.382 | 33.90% | 100% |
| 100% | G_I_pot | 367 / 72 | 19.62% | 106.721 / 82.930 | 22.29% | 100% |
| reference | ALL_STAGE | 405 / 72 | 17.78% | 106.721 / 82.930 | 22.29% | 100% |
| reference | ORACLE_NEEDED | 72 / 72 | 100% | 23.790 / 0 | 100% | 100% |
| reference | NONE_STAGE | 0 / 0 | N/A | 0 / 0 | N/A | 0% |

**Q4：G_I_pot 的重排明显包含 INPUT 绝对尺度。**当前带宽完全相同，G 本身就是 U 乘以 INPUT/B。
在 ≥90% 任务 Recall 处，相比 P_F，G 把 compression/dense/sparse 的选择数从 52/44/47 提至 61/60/54，
LLM 从 22 降至 0。它捕获更多大 INPUT 的观察等待，但计划流量增加 **29.56%**、BytePrecision 下降；
不是无需成本的时序预测提升。≥80% 点也有同类取舍（计划流量增加18.72%）。U 的 ≥90% 点只减少1个任务、
302.311 MB，不能据此宣称广泛优势。未选择 final score。

F1/F2-only 分母为404/71，保留全部 no-fault 负样本，≥90% 点如下；完整80%/100%及构成见产物：

| 分数 | 选中 / NEEDED | 计划 GB / NO_NEED GB | 捕获等待 |
| --- | ---: | ---: | ---: |
| P_F / U_pot / M_pot | 163 / 64 | 43.354 / 22.052 | 93.60% |
| G_I_pot | 165 / 64 | 53.890 / 31.111 | 99.47% |

F1/F2 的 ALL_STAGE / ORACLE 分别为105.921 /22.990 GB；NONE为零。
唯一 F3 task120 仍为 NEEDED、P_F=0.018517819600187、关键等待645.504897ms。
它是 out-of-model hazard，不计为 F1/F2 selector 预测错误。跨星和系统级100% P_F选择数不同，
仅因为本节排除了4个 LocalDelivery，不是改写前节的275/206系统级 landmarks。

**Q5/Q7/Q8：固定 theoretical break-even 并没有让图像几乎全入选，而且覆盖偏低。**
M>0 的 POSSIBLE 为68/405，其中45个NEEDED，计划19.996 GB、NO_NEED 3.788 GB；Precision66.18%、
BytePrecision81.06%、捕获等待69.27%。各类 POSSIBLE 是compression24/117、dense10/98、sparse21/131、LLM13/59；
三类图像合计仅55/346（15.90%）。其余都是“在本固定等价字节 screen 下”的 REJECT，不代表真实恢复一定不值得预置。

因为 U≤P_F，M>0 必须 P_F>0.5；本批两个集合恰好相同。
337个P_F≤0.5的跨星候选包含27个NEEDED，所以 ALL 的上限及实际 Recall 均为 **45/72=62.50%**；
排除F3后为 **45/71=63.38%**，捕获等待71.85%。固定 screen 的80%/90%/100%目标均 UNREACHABLE，
不能自动放松判据。**完整 M 排名则扫描全部负 cut，能达到100% Recall**；它与 M>0 筛选不是一回事。
因此该固定 screen 不能满足本批高覆盖目标，不宜未经审阅直接作为 production rule；也不反向推出某个新阈值。

### 验证与停止

**Q9：仍不足以构造 exact recovery-value model。**没有未来合法 checkpoint receipt/state/tail 的因果轨迹，
A1 仍 INCOMPLETE；本轮只是 current-rate INPUT serialization potential，不是第二套 recovery surrogate。
后续是否需要更丰富的量，应另行讨论，不自动加入优化器或 production Selective INPUT。

新增31项合成测试，维护 Python 全套275项通过（1项既有外部切片skip）；目标构建 no-op。
重新关联原始日志验证了既有 P_F/labels/source identity，得到3,224个唯一cut点、30个landmarks/reference、
12,896条profile构成记录；独立用原始 q_F1/q_F2 连乘再直接加权核对405个跨星分数，
最大 U 数值误差1.11e-16，4个 LocalDelivery 的 N/A 合同通过。
此前28 CSV/7 JSON runtime-equivalence证据沿用；本轮不修改 C++、不重复运行仿真。
初次数值中间产物保留在 `compfrr-input-coinitialization-value-run11-initial-numerics/`，不作为最终证据。
按分阶段实施方式先通过因果/公式测试，再完成真实数据审计与全套测试；现已停止等待人工审阅。
**不挑 production threshold/score、不跑独立runs或CI、不自动推送/合并。**
