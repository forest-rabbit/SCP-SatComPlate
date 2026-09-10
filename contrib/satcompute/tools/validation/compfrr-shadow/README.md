# CompFRR 旁路决策评估（G4）

这里实现 `CompFRR_Backup_Frequency_Model_Simplified_v4.md` 的 shadow consumer。
G4 validator 是模型预演和结果验证工具，不是 N5 production implementation。
未来 N5 正式模块不得依赖本目录（包括通过 `ns3/compfrr-shadow-*.h` 间接依赖）；
可以在测试中使用这里的纯模型作为 oracle，单向验证正式实现。
只在真实任务 RUNNING 时评估保护决策、有效状态与资源成本；不发送备份包、不占用真实
算力/链路、不修改任务完成状态、不参与故障随机抽样。真实备份执行属于后续 N5。

## 文件与边界

| 文件 | 职责 |
|---|---|
| `compfrr-shadow-model.h/.cc` | G1 字节/WU/合法边界映射、固定成本档位、START/ON 枚举、追赶和资源纯公式 |
| `compfrr-shadow-task-state.h` | OFF → INITIALIZING → ON 状态、初始化终止、已完成 L1 与 remote 历史、成本计数 |
| `compfrr-shadow-evaluator.h/.cc` | 只读任务事件观察、在线风险查询、虚拟事件和故障当刻记录 |
| `compfrr-shadow-recorder.h/.cc` | 独立 CSV 写出；不适用的值为空，不伪造零概率 |
| `summarize.py` | 真实输出一致性、虚拟账本审计、资源分账和 START/成本档位离线统计 |

入口参数位于外层 `para.cc`：`--compfrr-shadow=1` 显式开启，默认关闭；输出默认位于
`outputDir/shadow`，可用 `--compfrr-shadow-output=目录` 覆盖。要求在线 generate、
显式 G1 类别/映射的任务和实际 10 Gbps ISL。拒绝其他带宽，不能悄悄覆盖真实配置。
CSV 审计开关与 shadow 独立；不需要打开 `faultProbabilityAudit` 才能做决策。

## 决策与状态

本目录的保护 `START` 表示开始初始化，`ON` 才表示影子保护已建立；它们不是
`fault-events.csv` 中表示真实故障执行的 `compute START`。

任务首次开始计算时立即评估，之后按相对该任务开始时刻的 1 秒网格评估，完成/失败时停止。
`q_comp_1s` 查询未来 1 秒 F1/F2 联合风险，`p_finish` 查询到计算完成时的联合风险；
复用 `FaultModelEngine::QueryComputeRisk` 的 `(当前时刻, 窗口终点]` 全局检查网格，
不包含当前已完成的抽样，不读取未来任务、故障 CSV 或 F3 计划。F3 不进入这两个概率。

记 `S` 为 INPUT 字节、`W` 为总 WU、`K` 为完整变量状态字节（含变量索引，不含固定 H），
`x` 为真实 WU 完成比例，`μ` 为实际计算星 WU/s，`B=10 Gbps/8`。
`δ` 是目标增量进度，`n` 是组成一批 remote 的已完成 L1 数量。

- 成本按 **完整 K** 的十进制 MB 分档，不能按 S 或单次增量分档：
  `K≤100 MB` 为 `cL/cR=0.1/0.5 ms`，`100<K≤500 MB` 为 `0.5/2 ms`，其余 `2/8 ms`。
- 全枚举 `δ=1.0%…10.0%`（0.1% 步长）、`n=1…100`、`nδ≤1`。
  恢复余量为 `deadline-now-W(1-x)/μ`，负值无可行候选；相同目标值按 `(J,δ,n)` 排序。
- OFF 代价：`Joff=p_finish*(S/B+xW/μ)`。
  START 候选代价：`Jstart=cL+cR+(1-x)*(cL/δ+cR/(nδ))+p_finish*Rbar`。
  仅当最优 `Jstart<Joff` 且初始化来得及时 START；相等仍 OFF。
- ON 每秒最小化 `Jon=(μ/W)*(cL/δ+cR/(nδ))+q_comp_1s*Rbar`，其中
  `Rbar=K(n-1)δ/(2B)+cR(n-1)/n+Wδ/(2μ)`。

初始化捕获 START 时**已经完成**的最大合法边界，耗时
`max(S/B,cL+K(x_start_legal)/B)+cR`。成功时才令 `l=r=x_start_legal`；
这期间真实任务继续计算。初始化尚未完成就发生故障时，按未保护重算；不杜撰部分恢复。
初始化在 ON 成功时只记一次 `cL+cR`，不计入后续 L1/remote 次数。

合法边界完全沿用 G1：dense/compression 为 524288-byte tile（保留尾块），sparse 为
参考平均文件大小的等分合成文件，LLM 为完整 token。先对应用 extent 向上取整，再映射 WU，
同 WU 边界去重；图像状态按整数 `floor(K*work/W)`，LLM 按完整 token 的 114688 B 计数。
这些是已披露的应用状态布局假设，不是额外测量。

未来 L1 目标从 `max(最近已触发边界WU, 当前已完成WU)+δW` 映射到下一个合法边界，
不会补造过去错过的检查点。L1 经过 cL 后才推进 l；批次字节是实际已完成增量之和。
同任务 remote 串行，经过 `D_R/B+cR` 后才推进 r。频率变化不重写已触发 L1 或在途批次，
不清空 pending L1；新 n 可立即消费保留的 pending。ON 暂无可行候选时不退回 OFF，
暂停新目标，既有操作按原语义完成，后续有可行候选再继续。

同纳秒真实模型/计算完成事件先于后来建立的虚拟完成事件；实际任务终止会取消所有剩余虚拟事件。
虚拟耗时按纳秒向上取整，允许至多 1 ns 的保守延后，不提前宣告状态有效。
固定服务时间下 `Tinit<Trem` 已排除普通的初始化中自然完成；仍保留并测试提前完成中止的状态语义。

## 真实故障后的解析统计

只将实际 RUNNING 的 F1/F2 victim 纳入主要恢复统计，分别标记 OFF、INITIALIZING、ON。
F3 单列附录，不进入主要风险和恢复收益。正常成本分母是**全部任务**，包含没有故障的保护任务
及 F3 victim 在失效前的正常维护成本，不能只给故障任务算成本。

- 未保护/初始化未完成：追赶 `S/B+xW/μ`；额外执行 `xW`，输入等待闲置 `μS/B`，重算只计一次。
- 已 ON：追赶 `K(l-r)/B+cR*[l>r]+W(x-l)/μ`；`l=r` 时不收 cR。
  尾部传输等待换算为闲置 WU，融合及重算为额外执行 WU；真实故障后不再乘概率。
- 正常成本：`Cinit+N_L*cL+N_R*cR`，乘实际 μ 得额外 WU。
- 分别输出 recovery-only saving 和包含全部正常维护成本的 net lifecycle saving。
  两者都允许负值；deadline 结果只是 `fault+catchup+remaining≤deadline` 的 what-if，不是实际救回。

G4 候选节点/路径可用、存储不约束；记录本地/远端有效状态、未上传尾部、pending 和在途批次峰值。
这些记录用于暴露假设，**不是**真实节点选择、容量预留或拥塞模型。

输出：`shadow-decisions.csv`、`shadow-task-summary.csv`、`shadow-faults.csv`、
`shadow-events.csv`、`shadow-assumptions.json`。
已有输出目录不会因关闭 shadow 被删除；比较实验请使用新目录，以免误读历史 CSV。
测试指令与复现入口见[测试 README](../../../tests/README.md)，结果见
[G4 审阅报告](../../../../../docs/n4c/reviews/G4-shadow-decision-evaluation.md)。
