# CompFRR-P：Stage 2B 残余 deadline 分解

## 范围与证据合同

Stage 2A 已人工通过并推送，代码基线为 `b69299e6a04cf2b57a1445175ea6c38d84d9bae1`；
原 Stage 2A 执行元数据仍为 `ebd8a474…` 加当时开发补丁，不改标为 clean 正式结果。
本轮在同一 `fix/compfrr-p-candidate-coverage` 分支进行，只有默认关闭的被动采集与离线分析。

范围固定为 11 个任务：**5、12、13、193、244、300、302、304、513、584、766**。
旧日志有 34 个 OFF search event，但这些任务没有 P ranking 或 Selective 准入记录；
只保留最后候选的标量，无法证明全部候选的存储状态与预测轨迹，因此需要补充采集。
唯一补充运行保持 **1 Gbps / seed1 / randomRun11 / 800 tasks / 1300 s /
CompFRR-P CUMULATIVE / Selective / Relocate / online generate**，不是 validation-replay。
只替换输出路径并增加任务过滤日志开关；场景、故障、deadline、Frequency、SER、P ranking、恢复都不改。

原始证据保持不动，新产物统一位于
[`output/compfrr/1g-residual-deadline-audit/`](../../../../output/compfrr/1g-residual-deadline-audit/)。
执行补丁包含新增诊断源码；本轮明确标为 development-only，不作正式性能结果。

## 怎样解释三个时间

- `TI`：原 Frequency 中当前 INPUT source→当前候选 remote 的 fault-time INPUT 代价。
  复用候选实际准入速率；LocalDelivery 为零网络代价，不借用 FA-FFP 节点或统一链路带宽。
  原 Frequency 的 TI 是序列化成本，不能冒充包括排队/传播/接收完成的真实恢复等待。
- `TRmin`：保持 Deferred checkpoint/storage layout，在原 `δ=10…100‰`、
  `n=1…100`、`nδ≤1` 且节点/路径/存储可行的配置中，寻找原有平均恢复模型的最小值。
  不用 production objective 最小值代替 recovery 最小值；原数学表达式只抽取为共享函数。
  它是模型可行性证据，**不是运行时最坏情况上界或一定能恢复的证明**。
- `D`：原 compute deadline 扣除当前 recovery rate 下剩余正常计算的 slack，不删除、不放松。

分类 A：`TI>D` 且 `TRmin≤D`；B：`TI≤D` 且 `TRmin>D`；
C：单独均不超过 D，但两者之和超过；D 类：两者单独均超预算。
缺失证据为 UNKNOWN；资源可行集为空和原 Deferred 已可行另外明确记录，不强塞入 A–D。

A/C 的最低提前时间是 `TI+TRmin−D`，再除以 TI 得到理想提前比例下界。
时间分析只使用当前可得的 canonical future steps，不使用后来真实故障时刻选预测窗口。
首个正 first-failure mass 样本和所有后续样本分别检查，并保留原初始化 ready estimate。
同批次 fault-hit 行仍导出分解，但不能充当可行动的提前 INPUT 机会。

Frequency 的历史 FAULT_EPOCH 含当前检查、`finishExclusive=false`；
假设 Selective 使用 `QueryTaskPrediction()` 的下一合法样本、`finishExclusive=true`。
TASK_RUNNING/CAPACITY_RELEASE 保持 query 原语义，不统一改写窗口。
诊断中的纯 SER 使用**当前候选 pair**，不是已通过 P ranking/故障批次后重验的 committed pair。
因此 SEND 不等于已准入，更不等于真实 INPUT receiver completion；
这 11 个任务的假设提前流和 checkpoint 从未建立，实际提前进度/反事实恢复结果保持 UNKNOWN。

## 验证与结果

唯一补充运行完成全部 **1300 s**，仿真返回 0，墙钟 **1183.40 s（约 19.7 分钟）**。
38 份原有 CSV/JSON 逐字节一致；`run-summary.json` 只忽略主机 `wall_clock_ns/s` 后完全一致。
初次全文件比较报告的唯一差异正是这两个主机耗时字段；离线审计明确排除它们，
不排除任何仿真时刻、task outcome 或资源字段，没有为此重跑仿真。
10,048 个历史文件大小/mtime 不变，生产源码与执行前记录的补丁一致。

- build 通过；原 Frequency policy 215,029 检查、P 8,574 不变量、配置 139 检查通过。
- Frequency runtime **12,191** 检查通过；旧 649 文件与默认关闭版本逐字节一致；
  开/关日志两版也是 649 文件一致，只新增 9 份诊断 JSON。
- Python **272** 项，1 项既有跳过，其余通过；新增 12 项离线分类/时序/身份测试。
- actual-ledger、物理网络字节与计算守恒、存储释放、候选覆盖及同因果预测窗口审计通过。

结果仍为 **785/800**，committed START **367**；catch mean/P50/P90 仍为
**524.639 / 161.707 / 1447.751 ms**，FT **88.209594 GB**，总等效浪费
**9.390585 M eq-WU**，最忙链路全程利用率 **11.793195%**。这是等价验证，不是新的性能提升。

### Q1 / Q3：全部 11 个任务属于 A，不是 recovery 本身无解

共 **34 个实际决策、1,857 个候选快照，全部 A 类**。
其中 11 个已命中主任务的故障批次含 619 个快照，仅保留分解；
剩余 **23 个非 veto 决策、1,238 个候选** 才进入机会分析。
逐任务 A=**11**，B/C/D/UNKNOWN 均 **0**；将 fault-time INPUT 去掉后仍无模型可行配置的任务为 **0**。

所有候选的 2,315 个原配置均通过存储检查，最小恢复配置均为 **`δ=10‰, n=1`**。
此时原公式两项 `(n−1)` 成本为零，`TRmin=0.005×W/recoveryRate`。
这只是原模型的最小恢复代价，**不是 production 会选择该频率，也不是实际恢复只需这么久**。

以下均为最早非 veto、至少存在一个未来风险样本满足模型时间下界的代表候选；
“数量”是该次 fixed-local 搜索的 remote 数，不代表对未来真实资源的保证。时间单位为秒。
每个任务均满足 `TI>D`、`TRmin≤D`，均可进入后续 START–Selective 研究。

|任务|分析时刻|local→remote（数量）|D|TI|TRmin|最低提前时间|首风险 lead|纯 SER|
|---|---:|---|---:|---:|---:|---:|---:|---|
|5|946.160904148|0→1（55）|0.463806|0.824539|0.007730|0.368463|0.839096|DEFER|
|12|964.915704243|7→0（56）|1.261425|2.242531|0.021024|1.002129|0.084296|DEFER|
|13|719.669050206|0→1（54）|0.682221|1.212833|0.011370|0.541983|0.330950|DEFER|
|193|551.128704907|1→0（54）|1.753215|3.116825|0.029220|1.392830|0.871295|SEND|
|244|736.946703111|7→0（56）|1.555950|2.766129|0.025933|1.236112|0.053297|SEND|
|300|205.489821374|43→33（26）|1.341879|2.385557|0.022365|1.066043|0.510179|DEFER|
|302|103.605941493|41→0（51）|0.529227|0.940845|0.008820|0.420439|0.394059|DEFER|
|304|756.194138773|7→0（58）|1.147053|2.039204|0.019118|0.911269|0.805861|DEFER|
|513|896.520578394|0→1（57）|2.442150|4.341598|0.040703|1.940150|0.479422|SEND|
|584|813.029601962|9→4（51）|1.133823|2.015681|0.018897|0.900755|0.970398|DEFER|
|766|203.420492573|8→4（52）|1.009824|1.795238|0.016830|0.802244|0.579507|DEFER|

这些代表行的最低提前比例约 **44.687%**，不是人为统一设置的阈值。
三类图像的统一 bytes→WU 映射、100,000 WU/s、相同 deadline factor 和本次候选速率
使比例接近一致；整数 WU/时刻舍入造成微小差异。不能把它写进 production SEND 条件。
代表行初始化估计分别为：task 13/304/584 的 0.6 ms，task 513 的 10 ms，其余 2.5 ms。
更晚的快照可能因已完成状态增大而有更长初始化，分析逐行重新检查，并未沿用起点常数。

### Q2：首个风险来得及为 6 个，不能和“将来某点来得及”混同

在各任务的**任意一次非 veto 合法分析时刻**，同时满足“最低提前时间”和“原初始化估计”
的首个风险样本见证共有 **6 个任务**：

|任务|见证分析时刻 s|触发|到首风险 s|初始化估计 s|
|---|---:|---|---:|---:|
|5|946.160904148|TASK_RUNNING|0.839096|0.002500|
|13|720|FAULT_EPOCH|1.000000|0.098166|
|302|104|FAULT_EPOCH|1.000000|0.217219|
|304|757|FAULT_EPOCH|1.000000|0.004400|
|584|813.029601962|TASK_RUNNING|0.970398|0.000600|
|766|204|FAULT_EPOCH|1.000000|0.173600|

这里包含后来一次合法决策的见证，所以不能用上表最初 TASK_RUNNING 的 lead 判断这 6 个数量。
在最初 TASK_RUNNING 本身，满足首风险两项条件的只有 **5、584**。
FAULT_EPOCH 见证的 lead=1 s 指 query 的**下一次**合法采样，不把已经进行的本轮当成发送窗口。

若放宽问题为“整个预测窗口中，是否存在正 first-failure mass 的某个后续样本满足两项模型下界”，
则 **11 个都有**。但这不能覆盖更早到来的故障，也不能保证当时流可准入、checkpoint 已接收。
例如 task 12 最初 lead 仅 0.084296 s，而模型至少需要提前消化 1.002129 s INPUT；
其后续窗口有机会，不代表第一轮风险能避开。

### Q4：可以研究 policy-aware admission，但没有证据保证直接救回 11 个

因果候选快照套用**未改动的纯 SER**，非 veto 时仅 **193、244、513** 出现 SEND，
其余 **8 个任务在记录的非 veto 候选上均为 DEFER**。
三个 SEND 任务各有后续风险点满足两项模型时间下界，但都没有首风险及时见证；
因此“纯 SEND 且首风险两项模型条件满足”的任务交集为 **0**。
这不是说提前 INPUT 无效，而是说明**期望收益 SEND、最早风险覆盖、实际成功恢复是三个不同问题**。

证据足以确认当前硬约束证明的是“完整 Deferred INPUT 不可行”，而不是“任何 INPUT 策略都不可行”，
可以在人工审阅后研究 START–Selective policy-aware admission。
但不能据此直接将 11 个任务全部准入，不能强制原 SER 的 DEFER 任务发送，也不能先宣布完成数会上升。
后续若实施，仍需保留 START 收益、初始化、batch 后 actual pair/path/storage 重验、真实 receiver completion
与原恢复/deadline 合同；本轮没有实现这些新决策逻辑。

### 产物

输出目录含 `residual-deadline-decomposition.csv`、`residual-input-analysis-time.csv`、
`residual-task-summary.csv`、`classification-summary.json`、`representative-cases.md`；
门禁与执行记录为 `small-gates.json`、`execution-equivalence.json`、`old-evidence-inventory.json`，
完整因果快照保留在 `instrumented-run11/residual-deadline-candidates.json`。

## 纯离线 S-vs-N 补充审计

本补充只读取上述 11 个任务的 Stage 2B 快照，排除同批次 fault-hit 后保留
**23 个 decision event、1,238 个 candidate snapshot**；没有重新运行 ns-3，也没有修改 production。

历史 N 直接复用 `a4315e2b8` 的精确定义：S 与 N 使用相同的
`cost=(1-P_F)×T_ser` 和严格 `>`；S 的收益是
`Σw_k min(T_ser,lead_k)`，N 的收益只替换为
`Σw_k min(T_net,lead_k)`。`T_net` 使用 native
`AdmissiblePathEstimate::TransferTimeNs()` 的整数向上取整与传播时延，LocalDelivery 语义不变。
冻结的 409 条历史锚点复核为 **405 个网络候选、S=68、N=115、4 个 LocalDelivery**；
历史 S/N runtime CSV 共 818 行逐决策、逐 `T_ser/T_net` 全部匹配。没有重新推导、阈值或容差决策。

|任务|非 veto candidate|S SEND candidate|N SEND candidate|S task SEND|N task SEND|N 新增|
|---|---:|---:|---:|---|---|---:|
|5|55|0|0|否|否|0|
|12|117|0|0|否|否|0|
|13|108|0|0|否|否|0|
|193|54|54|54|是|是|0|
|244|113|113|113|是|是|0|
|300|86|0|0|否|否|0|
|302|105|0|0|否|否|0|
|304|176|0|0|否|否|0|
|513|290|290|290|是|是|0|
|584|51|0|0|否|否|0|
|766|83|0|0|否|否|0|

任务级 SEND 采用“任一非 veto candidate snapshot 判 SEND”的存在性汇总；它不选择 remote，
也不代表 P ranking 或批次后重验会接受该 candidate。结果为：S 和 N 均只覆盖
**193、244、513，共 3/11 个任务**；candidate 级也完全一致，均为 **457/1,238**。
N 相比 S 新增 **0 个任务、0 个 candidate**。

对 S/N 判 SEND 的 457 个 candidate，离线将原 Frequency deadline 可行域中的完整
fault-time INPUT 项置零；其余节点/路径、2,315 个 `(δ,n)` 搜索配置、storage 与恢复模型均保持。
**457/457** 均从原 `DEADLINE_INFEASIBLE` 变为存在模型可行配置，涉及上述 3 个任务。
但 N 没有新增 SEND，所以“N 新增 SEND 中变为可行”为 **0 个任务、0 个 candidate**。

原因不是 N 被错误实现，而是这批 residual 均为大 INPUT 图像任务：传播时延相对
0.825–4.342 s 的序列化时间太小。N 对任一 candidate 增加的期望收益最高约 **4.711 ms**；
在 S 仍 DEFER 的集合中最高约 **1.753 ms**，而最接近翻转的 task 584 仍低于成本
约 **45.310 ms**，因此没有 action flip。

结论：对**当前 residual 11-task policy-aware START 候选集**，S 已足够；没有证据支持把
N 重新引入 production。这个结论不否认历史 10 Gbps 全 409 候选中 N 曾多选 47 个网络任务，
只说明那些历史优势没有落在当前 1 Gbps residual cohort 上。仍不实现 START–Selective。

补充输出：`residual-selective-s-vs-n-candidates.csv`、
`residual-selective-s-vs-n-tasks.csv`、`residual-selective-s-vs-n-summary.json`。
复核入口（纯离线）：

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/audit-residual-selective-s-vs-n.py
```

离线复核命令（不启动仿真）：

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/audit-residual-deadline.py
```

## 后续场景约定与停止点

用户已接受未来对 task 120 使用统一的带宽归一化 release：原 `1024.682825747 s`，
1 Gbps 提前 `5.76 s`，10 Gbps 不变，100 Gbps 延后 `0.576 s`。
这是独立新场景、同一带宽下所有算法使用同一输入，用于近似对齐计算开始/F3 相位，
不是保证某方案保护成功；**本轮没有应用该变化，也没有改 task 120/F3**。

本轮完成后停止人工审阅，不自动实现 START–Selective、改变 INPUT 发送源/顺序、搜索新 local、
调整任何阈值或算法，不启动 10/100 Gbps 或 run12/13 矩阵，不自动 CI/提交/推送/PR。
