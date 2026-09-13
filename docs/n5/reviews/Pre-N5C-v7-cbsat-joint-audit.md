# Pre-N5C：CB-Sat 修订与 CompFRR V7 INPUT-JIT

2026-09-13，按项目外 `CompFRR_V7_and_CBSat_Audit_Adjustment_Taskbook.md` 与已确认的
V7 Event-Aware JIT 模型实施。G0–G15 已完成：代码、本地回归、13 组正式仿真和独立审计通过。
语义验收通过不等于 JIT 性能占优；当前场景的权衡见下表。默认仍为 eager，不自动替换旧方案。

## 身份与边界

- 修改前：`n5@2ccfa392e`，CB 分支原 head `5387bb4b9`，干净工作树。
- CB 修复 `b1a41d7d9`，分支交付 `96ef0d38f`；V7 实现 `b3d0fa78b`。
  全部新正式仿真统一执行干净版本 `367f23f39`，离线审计版本 `ba196fc7b`。
  后续文档提交不冒充仿真执行版本；完整身份保存在各执行 JSON 和联合 comparison.json。
- 保持 66 星/66 计算星、800 任务、352513119 WU、400 WU/token、100000 WU/s、
  10 Gbps、fixed 1 ms、20 s 网络更新、1300 s、原 compute deadline、每星 10 GB 备份池。
  seed1/run11、F1/F2/F3、MTBF profile、路由、placement 与原 Frequency 优化结构不变。
- CB 主 baseline 为 busy=recompute；relocate 只称扩展迁移版，不称性能上界。
- 用户确认的四条执行细节优先于任务书中“尚待确认”段：未准入可合法重试；
  已建立预取失败不自动重传；total/used/unused 同一完整生命周期；同星无 UDP。
  JIT 不保证故障前传完，第 9.2 节网络注册只适用于跨星。
- PR #98 面向 n5；V7 PR #99 以 CB 修复分支为 base，分开审阅。
  不自动合并 n5/main、不打 tag、不触发本轮 GitHub CI、不删除未合并分支、不进入 N5C。

## G0 行为核对与实现结果

| 接口 | 修改前 | 本轮结果 |
|---|---|---|
| CB recovery | HasState 即可利用 q，之后补 INPUT | HasState 且 HasInput 才可 DIRECT/RELOCATE，否则从零恢复 |
| CB X=0 | 原即时合并分支与首次分配易混淆 | 保留 H/X，已有日志紧急合并显式 EMERGENCY_COMPACTION |
| input policy/flow | eager/deferred | 独立 jit/PREFETCH_INPUT，旧枚举语义不变 |
| Frequency | 原 START/ON | START 固定一次 INPUT 计划；ON 原 (delta,n) 目标不变 |
| runtime events | 初始化/epoch/capacity 接线 | 实际初始化提交、完整 fault batch 后存活事件及合法容量重试 |
| CheckpointManager | 恢复时取消普通保护流 | 独立 S 对象、真实预留及原流跨故障交接 |
| recovery dependency | deferred 无条件新建 INPUT | READY/IN_FLIGHT/ABSENT 按真实目标和对象解析 |
| network estimate | 新流准入估计 | 增加只读原流剩余估计，不重复注册/预留/选路 |
| LocalDelivery | 既有同星交付 | INPUT 沿用相同语义，不造 UDP、不排除同星合法节点 |

## CB 完整 INPUT 与 X=0

旧目录 `output/cb-sat-v2/20260912T171143276614Z-formal`（执行 `494e133c5`）保持只读。
664 条恢复记录中，FFP/FA-FFP × 两种 busy 各有一条 task325：
`root_ready=true,input_ready=false,DIRECT,resume=22435 WU`。这是四条记录、一个不同任务。
因此必须修复并重跑全部八组，不能只换离线审计版本。

新八组 task325 的上述四条记录全部改为 `RECOMPUTE,resume=0`，最终均完成；
不完整 INPUT 的非法 q 恢复记录为 **0**。完成数与旧八组一致，但相关资源/恢复统计已变化，
旧表保留为历史记录，不作为修订后结果。

| placement | recompute 完成/失败 | relocate 完成/失败 |
|---|---:|---:|
| FFP | 795 / 5 | 800 / 0 |
| LRL | 797 / 3 | 800 / 0 |
| FA-FFP | 795 / 5 | 800 / 0 |
| FA-LRL | 798 / 2 | 800 / 0 |

各组 83 次恢复机会；recompute 失败仍为 REMOTE_BUSY 后超过原 compute deadline。
FFP/FA-FFP：114、252、450、456、475；LRL：46、114、252；FA-LRL：114、252。
全部任务/传输终态，CB 存储、服务预留、placement 活动负载归零。

新旧正式结果的两条 X=0 均为 FFP 两组 task64 首次分配的 `NO_REMAINING_LOGS`，
无 MERGE_START，不是已有日志紧急压缩。保留已有日志分支及明确诊断；不改 H/X，
不将这些正式记录伪称为紧急压缩覆盖。边界行为由受控测试检查。

## V7 状态与时间合同

正常为 `ABSENT → IN_FLIGHT → READY → RELEASED`；同星也维护逻辑对象生命周期。
未准入无流，保持 ABSENT，允许后续合法事件重试；已建立流失败则结束本次预取，
保留已发送字节，只在真实故障后的恢复流程重新获取 INPUT，不在 epoch 自动重传。

同目标 READY 无原 source 路径依赖；同目标 IN_FLIGHT 保留 transfer/object ID 和原 receiver，
恢复计算等待 INPUT 与 state 都实际就绪；换目标不能继承旧 holder 的 READY。
source=holder/recovery 沿用 LocalDelivery，网络发送量为零。used 只在真实恢复计算开始、
实际消费这份 INPUT 时确认；仅接收或交接成功不算 used。

START 使用同一 pair 的固定 INPUT 计划及收益，不随 (delta,n) 改变；无预取收益严格退化回 V6。
START 保留 canonical predictor 的原窗口；ON 排除已结束的当前 sample 与完成时刻及之后的 sample，
重新条件化后续首次故障质量。没有新 timer/RNG/optimizer，没有 F3 未来信息。

在途估计按 receiver-remaining、已有速率/路径、包头与传播计算；固定无丢包路径下偏保守，
不是任意未来排队、丢包、拓扑变化下的保证。真实 READY 仍由 receiver 回调决定。
INPUT total/used/unused 覆盖故障前及故障后原流续传的相同生命周期；不把 after-fault 再加到 total。

## 验证与正式执行

| 检查 | 结果 |
|---|---|
| 项目 uv 环境增量构建 | 通过，examples/tests OFF；最后复核 Ninja 无需重编译 |
| CB policy / runtime / recovery | 89207 checks；15 cases / 881296 checks；26 cases / 5099 checks |
| CB 独立证据检查 | 26 fixtures，通过；13 类损坏全部拒绝 |
| JIT 纯策略 / 原 N5B policy | 120 / 215027 checks，通过 |
| JIT runtime | 13 个对象/网络/恢复场景 + 2 个 online generate 场景，15 cases / 340 checks |
| JIT 独立证据检查 | 15 fixtures + 同纳秒初始化正例，通过；13 类损坏全部拒绝 |
| Python 维护回归 | 135 项，134 通过、1 项既有可选轨道切片跳过 |
| 维护 C++ / 公共 smoke / 集成 regression | 全部通过；旧 eager/deferred、R0/R1、16 placement 小场景保持通过 |
| 故障/预测器回归 | N4B 100 任务：88 完成/12 按合同失败，483 条实时模型/预测概率全部一致 |
| 旧方案新执行等价 | R4/R5/R7 FA-LRL，每组 18 类业务文件 + 9 类保护文件匹配 |

独立临时工作区未配置 ns-3，因此一次全 Python 尝试的 CLI/C++ 接线项因缺构建失败；
合回主工作区后按原 uv/Ninja 环境复核，上表 135 项结果全部符合预期，未为此重配或改仿真代码。

| 证据 | 目录（仓库根目录下） |
|---|---|
| CB 修复后八组 smoke + 重复 | `output/cb-sat-v2/20260913T060835526989Z-smoke`，执行 b1a41d7d9 |
| CB 新八组正式 | `output/cb-sat-v2/20260913T070155626344Z-formal` |
| 两组 JIT + 三组旧方案锚点 | `output/v7-cbsat-adjustment/20260913-jit-formal` |
| 旧 CB 影响审计 | `output/v7-cbsat-adjustment/20260913-input-audit` |
| 新 CB INPUT/X=0 审计 | `output/v7-cbsat-adjustment/20260913-cb-corrected-audit` |
| 最终联合 JSON/CSV | `output/v7-cbsat-adjustment/20260913-joint-comparison` |

13 组全部真实执行 1300 s、退出码 0。CB 单组墙钟 4342–4594 s；CompFRR 1876–2611 s，
有并行资源竞争，墙钟仅作执行记录，不是算法性能指标。
最初 CB 四并发尝试在约 4 分钟后停止并以八并发重启；
`output/cb-sat-v2/20260913T065727246652Z-formal/aborted-run.json` 明确标为未完成，未纳入比较。

JIT 矩阵初次离线检查误要求初始化提交时间严格小于预取请求时间，而合法回调可在同一纳秒发起请求。
`ba196fc7b` 改为时间非晚于且 CSV 原始 append 顺序中先提交、后请求；反向顺序仍被拒绝。
同纳秒故障的 READY 截止仍要求严格早于故障，没有放宽。
使用 `run-v7-jit-matrix.py --audit-only` 重审已完成输出，不重跑、不改 CSV 或 execution.json。
`matrix-audit.json` 分别记录执行/审计版本；初次 runner 日志保留在 `/tmp/v7-jit-formal-matrix.log`。

## 正式比较：固定 FA-LRL

网络为正常保护 + 故障恢复的**实际应用额外发送量**，十进制 GB，包含取消前发送。
`active = execution waste + normal protection`；`total = active + reserved idle`。
active/total 单位百万 eq-WU，idle 是容量机会成本，不是实际 CPU 执行。

| 方案 | 完成/失败 | active | total | 额外 GB | 平均链路利用率 |
|---|---:|---:|---:|---:|---:|
| R4 eager + recompute | 799 / 1 | 2.060747 | 2.360050 | 199.864 | 0.516596% |
| R5 eager + relocate | 800 / 0 | 1.152619 | 1.452554 | 199.371 | 0.516398% |
| R7 deferred + relocate | 800 / 0 | 1.068858 | 3.056674 | 107.725 | 0.431624% |
| JIT + V6 START（消融） | 800 / 0 | 1.069990 | 2.722262 | 191.297 | 0.543605% |
| JIT V7 + relocate | 800 / 0 | 1.071560 | 2.723832 | 193.255 | 0.545218% |
| CB-Sat recompute（主 baseline） | 798 / 2 | 3.660245 | 3.884104 | 299.753 | 0.701663% |
| CB-Sat relocate（扩展） | 800 / 0 | 2.350467 | 2.599335 | 299.869 | 0.701983% |

| 方案 | 故障至恢复计算启动均值（ms） | 故障至追平均值（ms） | 追平样本数 |
|---|---:|---:|---:|
| R5 eager | 36.137 | 123.312 | 83 |
| R7 deferred | 239.496 | 315.176 | 83 |
| JIT V7（消融组相同） | 199.069 | 274.939 | 83 |
| CB-Sat recompute | 26.971 | 240.958 | 81 |
| CB-Sat relocate | 29.984 | 251.427 | 83 |

JIT 相对 eager：额外网络 **降低 3.07%**，追平均值 **增加 122.96%（151.63 ms）**。
相对 deferred：额外网络 **增加 79.40%**，恢复启动等待 **降低 16.88%**，
追平均值 **降低 12.77%（40.24 ms）**。包含业务的总应用网络为 eager 493.263 GB、
deferred 401.617 GB、JIT 487.147 GB，对应降低 1.24% / 增加 21.30%。
应用字节不能替代逐跳利用率：JIT 平均链路利用率较 eager/deferred 分别提高 5.58%/26.32%。

JIT total 比 deferred 低 10.89%，主要来自 reserved-idle 减少；active 反而高 0.25%。
相对 eager，active 低 7.03%，total 高 87.52%。因此当前场景不能说 JIT 总体优于两者：
它付出了接近 eager 的应用网络流量，只换得相对 deferred 的有限恢复加速。
全 V7 相比 V6 START 消融多 4 次预取、约 1.959 GB 额外网络和 1570 eq-WU 常态成本，
本场景没有额外恢复收益；如实保留，不为获得优势调参数。

与 CB 主 baseline 相比，JIT 多完成 2 个任务，active/额外网络分别低 70.72%/35.53%；
CB 追平只统计达到该里程碑的 81 个任务，不能忽略失败而宣称其无条件更快。
与同为 800 完成的 CB relocate 相比，JIT active/网络低 54.41%/35.55%，
但追平慢 9.35%、total 高 4.79%，不存在所有指标同时占优。

## JIT 生命周期与解释边界

| 指标 | V6 START + JIT | 完整 V7 |
|---|---:|---:|
| evaluation / admitted | 1869 / 343 | 1883 / 347 |
| fault READY / IN_FLIGHT / ABSENT | 17 / 0 / 66 | 17 / 0 / 66 |
| READY / IN_FLIGHT 实际复用 | 16 / 0 | 16 / 0 |
| 预取 total / used / unused（GB） | 87.892 / 4.269 / 83.624 | 89.096 / 4.269 / 84.828 |
| 新恢复 INPUT（GB） | 19.281 | 19.281 |
| LocalDelivery 预取 | 5 | 5 |
| 重复完整 INPUT / 存储泄漏 / 活动预取流 | 0 / 0 / 0 | 0 / 0 / 0 |

V7 330 份预取因正常计算结束释放，1 份因恢复目标变化而未使用，16 份在真实恢复计算时使用。
按同一生命周期字节，used 占 total 4.79%；不是“成功收齐的比例”，也不是预测准确率。
正式场景没有故障时 IN_FLIGHT 样本，跨故障原流续传、失败、同纳秒 F3 等由受控测试覆盖，
不伪称在大场景观察到了这些路径。运行时与独立审计一致，最终 quiescent=true。

本次 eager/deferred/JIT 的 83 个主任务故障 (task_id, time_ns, type) 实际一致；
这不保证不同在线策略在其他运行中必然得到相同故障实现。仅为固定单 seed 受控比较，
不是多 seed 显著性结论，不据此改变冻结场景。
旧 32 组仍只读；只有 R4/R5/R7 有新执行等价锚点，其他组明确标为历史未重跑。
R0、1+1 的历史对照在 comparison.json/CSV 中保留；1+1 takeover 不等于 checkpoint 追平时间。

复核入口见 `tests/integration/regression/{run-v7-jit-matrix,analyze-v7-cbsat-joint,audit-jit-input}.py`；
完整 INPUT 与 X=0 独立入口为 CB tools 的 `audit-cb-sat-adjustment.py`。
正常运行不需要重复这 13 组正式实验。原始输出与新比较目录均未覆盖历史证据。
