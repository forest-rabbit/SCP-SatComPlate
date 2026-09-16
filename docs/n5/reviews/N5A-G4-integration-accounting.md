# N5A-G4 集成与资源账本验收

**N5A-G4 = PASS，待用户最终审阅。** 日期：2026-09-10。
N5A 通用保护执行底座已完成；不代表 N5B/N5C 算法完成，尚未合并。

## 身份与边界

- 分支 `feature/n5a-protection-runtime`；Draft PR #95，base=`n5`。
- 经验证代码 `fc2409de70b4656070ad8da64c7a8ffd527db6d6`；最终运行启动时工作区干净。
- N5 base `2a64595c7307fbac1cc647e55e1ec24d19985ea0`，G3 审批依据用户提供的
  `N5A_G3_Audit_and_G4_Start_for_Codex.md`，审阅头 `0c0d6281013fd9ef81a3dc0d51597f04f9f113d9`。
- 用户另批准仅验收用途的冻结回放入口；不恢复旧生产 replay，不改在线 F1/F2/F3 模型。
- 本轮改动集中于 fault trace/controller/CLI、ComputeService/任务 finalizer、保护池/事件账本、
  metrics 和既有测试/场景脚本；完整文件列表可查上述代码提交。不修改上游 src、业务输入或故障参数。

## 账本合同

详细定义集中在 [protection README](../../../contrib/satcompute/protection/README.md#g4-资源账本)。

- planned 的 catch-up/post/total 分列；TAIL/REDO/RECOMPUTE 从 lf/rf/0 追赶 xf 后继续剩余工作。
- actual 来自真实 ComputeService，整数 `floor(service_ns*rate/1e9)` 且不超过 planned；
  保存完成/取消/停止前缀，不用 planned 为失败恢复计费。
- normal 按实际生成和物理提交完成事件计费，初始化与后续 cL/cR 分开；
  同纳秒 fault/commit 的成本与实体提交保持一致，取消的未完成操作不收取完整事件成本。
- `waste = normal_eq_WU + reserved_idle_eq_WU + actual_catchup_WU`；不加 post-catchup。
  reserved-idle 覆盖 accepted 到 compute start（未开始则到失败），fault-time cR 不重复加算。
- 任务 local/remote 存储峰值按本任务占用统计；每节点保留各类峰值、容量拒绝和最终 used/reserved。
- 网络按类型列 declared/sent/received，主备份网络开销取实际 sent；跨星 recovery RESULT 属于业务流，
  与零网络字节的 LocalDelivery 单列。deadline 沿用首次主计算 deadline，compute 按时与 RESULT 完成分别统计。

## 验证

- 完整定向构建通过，未启用 ns-3 全局 examples/tests。
- 19 个 C++ executable、63 项 Python unit（无 skip）、9 组 smoke 全部通过。
- 现有全部 regression：五种路由、完整工作负载、故障生命周期、N4B 联合验收全部通过。
- recovery 专项 848 项检查；增加 catch-up 前/后故障、从未开始的预留、actual WU、无双计 cR、
  初始化事件计数、存储/锁/在途流清空和同纳秒 UID 反转。
- 四任务 smoke 验证 generate/validation-replay 业务及恢复 CSV 一致；fixed 截断后四任务均
  `FAILED/SIMULATION_ENDED`，资源清空。OFF 不创建保护资源，离线资源账本为零。

### 三个人工核算锚点

统一计算速率 100000 WU/s。TAIL 两行 normal 事件为初始化生成1次、L1生成3次（各0.5ms），
初始化物理提交1次（2ms），共4ms=400 WU；RECOMPUTE 行未启用 checkpoint。

| 受控任务 | accepted / compute start（ns） | reserved-idle（ns / WU） | planned catch / post / total | actual catch / post / total | actual waste（WU） |
|---|---|---|---|---|---:|
| TAIL success | 180000001 / 198280840 | 18280839 / 1828.0839 | 2199 / 82301 / 84500 | 2199 / 82301 / 84500 | 4427.0839 |
| RECOMPUTE success | 80000001 / 80000002 | 1 / 0.0001 | 7699 / 92301 / 100000 | 7699 / 92301 / 100000 | 7699.0001 |
| TAIL recovery F3 failure | 180000001 / 198280840 | 18280839 / 1828.0839 | 2199 / 82301 / 84500 | 2199 / 2972 / 5171 | 4427.0839 |

第三行仅执行51719160ns，`floor(51719160*100000/1e9)=5171`；其中2972 WU为正常剩余计算，
不加到主 waste。三个锚点由 C++ 常量检查并与 recovery CSV 的整数服务、分账和时间逐项核对。
原始小规模证据在 `output/n5a-g4/controlled/`（最终测试临时目录同样运行这些检查）。

## 正式场景与运行清单

LEO-66 / 800任务 / 1300s，10Gbps、1ms、deadline1.3、seed1/run11，工作负载、放置不变。
输入只读复用 `output/n4-release-validation/fault-trace.json`，共87条，F1/F2/F3=84/2/1。
FIXED 为每星额外10GB、delta5%、n4，不调参提高救回率。

**FIXED EXECUTION VALIDATION — NOT CompFRR ALGORITHM RESULT。**

| 目录（均在 output/n5a-g4/） | 用途 | 状态 |
|---|---|---|
| off-replay | 入口初验，代码迭代期间 | 717完成/83失败；542.700s |
| fixed-replay | 账本初版集成检查，代码迭代期间 | 796完成/4失败；2490.710s |
| off-final | 最终提交的正式 OFF 对照 | 717完成/83失败；586.688s；18份业务输出等价 |
| fixed-final | 最终提交的正式 FIXED 验收 | 796完成/4失败；2492.319s |
| fixed-repeat | 同提交/输入重复，验证确定性 | 796完成/4失败；2556.006s |

入口初验及最终 OFF 的13份业务 CSV 逐字节、5份 JSON 结构与冻结 N4 完全一致，仅忽略 run-summary 两项 wall-clock。
在线概率审计与旧 shadow 不属于回放业务输出，没有复制其文件冒充本次输出。原始 N4 证据未修改。
最终 OFF/FIXED/repeat 均运行完整1300s，退出码0，输入故障与 N4 逐项结构相同。
OFF 的直接 RUNNING victim 为 F1/F2/F3=82/0/1，业务结果保持717/83。
两次最终 FIXED 共27份业务/恢复/资源输出一致，仅 run-summary 忽略两项 wall-clock，
无其他忽略项。初版 FIXED 与最终结果也一致；没有更换场景、seed 或参数寻找更好结果。
全部命令、提交/dirty 标记及耗时保存在各目录 execution.json / execution-result.json / time.txt。
跨运行检查保存在 `output/n5a-g4/final-validation.json`；统计为各 run 下的
`protection-accounting.json` 和 `protection-accounting-by-task.csv`，均为 gitignore 排除的本地证据。

## FIXED 最终结果

800 个保护 START，故障时78个 ON、5个 INITIALIZING；83次恢复全部获得服务预留，
79次成功、4次 deadline 失败。最终796个任务完成、4个失败，没有残留运行态或重复 logical terminal。
4个失败 task ID 为114/252/456/475，均从 ON 回退 RECOMPUTE，剩余 deadline 减去输入等待后
不足以完成完整重算；其中456已达到 catch-up，其余三个未达到，actual 按真实前缀计费。

| 路径 / 终态 | 次数 | planned total WU | actual total WU |
|---|---:|---:|---:|
| TAIL / COMPLETED | 55 | 16800128 | 16800128 |
| REMOTE_REDO / COMPLETED | 18 | 5345064 | 5345064 |
| RECOMPUTE / COMPLETED | 6 | 2275054 | 2275054 |
| RECOMPUTE / FAILED | 4 | 2410383 | 1006387 |

| 统计（83次恢复） | sum | mean | P50 | P90 |
|---|---:|---:|---:|---:|
| planned total WU | 26830629 | 323260.590 | 304206 | 637743.8 |
| actual total WU | 25426633 | 306344.976 | 300527 | 586147.6 |

execution ratio 平均0.973609、P50/P90均为1；分位数用线性插值，planned=0的比值留空。
按路径/终态的完整 mean/P50/P90 同时保存在上述 JSON，不只给出总体均值。

主 waste 分项：normal=1522840 WU，reserved-idle=462341.9765 WU，actual catch-up=2143633 WU；
合计 **4128814.9765 WU**，除以原始351623833 WU为 **1.17421363%**。
post-catchup 正常剩余计算不加进 waste；4个失败恢复的 planned/actual 差1403996 WU没有虚增计费。

### Recovery concentration under first-feasible placement

4个 deadline 失败任务114/252/456/475的 remote 均为卫星0。故障当刻，它分别正在执行
普通任务158、恢复任务734、恢复任务722、恢复任务493，不能立即接管。已有 committed
state 不会自动迁移到其他节点，因此回退到空闲节点完整重算，且原 deadline 不重置。

| 卫星 | 原始任务数 | 接受恢复任务数 | 全程计算利用率 |
|---|---:|---:|---:|
| 0 | 3 | 49 | 13.42% |
| 1 | 3 | 26 | 6.90% |
| 2 | 5 | 8 | 2.64% |

原始 workload 热点是卫星12/8/11，任务数分别为110/105/103；不是这些低 ID 恢复节点。
当前按 stable ID 选择首个可行节点的 **FFP（First-Feasible Placement）** 自身造成了恢复集中。
这是 placement baseline 的限制，留给 N5C 研究 backup assignment / recovery pressure，
不在 N5A 调参或修改恢复机制。低全程利用率不保证故障当刻空闲。
证据来自本次 fixed-final 的 task-summary、task-events、recovery-summary 和 compute-node-summary。

### 四类任务

| profile | tasks / STARTed | ON / INITIALIZING at fault | 恢复成功 / 尝试 | deadline met | TAIL / REDO / RECOMPUTE |
|---|---:|---:|---:|---:|---:|
| dense-image | 240 / 240 | 12 / 1 | 13 / 13 | 240 | 11 / 1 / 1 |
| sparse-inference | 240 / 240 | 21 / 3 | 23 / 24 | 239 | 14 / 6 / 4 |
| compression | 240 / 240 | 30 / 1 | 28 / 31 | 237 | 20 / 6 / 5 |
| llm | 80 / 80 | 15 / 0 | 15 / 15 | 80 | 10 / 5 / 0 |

| profile | normal WU | idle WU | actual catch-up WU | planned / actual recovery WU | backup sent B |
|---|---:|---:|---:|---:|---:|
| dense-image | 472020 | 57425.8096 | 166672 | 2974282 / 2974282 | 176219531207 |
| sparse-inference | 99580 | 117921.8745 | 542466 | 7636532 / 7115178 | 64082661925 |
| compression | 358040 | 186397.2922 | 1042109 | 10490415 / 9607773 | 130890842441 |
| llm | 593200 | 100597.0002 | 392386 | 5729400 / 5729400 | 114051094416 |

任务 local/remote 峰值按类别取最大分别为（B）：dense 255330802/1255330802、
sparse 264048/555473015、compression 138795477/1138795477、LLM 251166720/891469824。

### 存储、网络与清理

66个节点的最大同时池占用2704701610 B；节点峰值P50=96994581.5 B、P90=231727104 B。
分配失败0次、涉及任务0个；所有 final used/reserved 均为0。
实际备份 sent payload 共485244129989 B，按类型如下；完整 declared/received 值在 JSON/原始 CSV。

| 类型 | sent payload B |
|---|---:|
| INIT_BASE | 192999633219 |
| INIT_STATE | 0（初始化捕获进度为0，无变量 payload） |
| L1 | 153107914234 |
| REMOTE_BATCH | 133840286954 |
| RECOVERY_INPUT | 3123620860 |
| RECOVERY_TAIL | 2172674722 |
| RECOVERY_RESULT_NETWORK（业务，排除于上述备份总量） | 9658069232 |

78个跨星恢复 RESULT 完成交付；另1个同星 RESULT 交付106845642 B，网络字节为0。
最终无恢复锁、活动 runtime transfer、待注册保护请求、待提交融合或有效定时器，
`protection-finalization.json.quiescent=true`；逐任务唯一终态和各 WU 守恒检查均通过。

## 限制与门禁

冻结事件只用于执行验收；FIXED 改变负载后的在线概率不等于历史 trace 中的概率。
正常成本为事件完成等效计费而非额外 CPU 暂停；失败恢复可能执行不足一个整数 WU的时间，
时间照实保留，actual WU向下取整。固定策略的性能不能解释为 N5B/N5C 算法结论。
本正式场景没有F2直接 RUNNING victim，不能据此声称其恢复覆盖率；F2语义由受控测试覆盖。
验收允许4个真实 deadline 失败，没有为救回率调参。用户最终审阅确认 **G1/G2/G3/G4 PASS，
N5A COMPLETE**，授权 PR #95 合入 n5 并清理功能分支；main 保持不变，不运行 N5 阶段 CI、
不打 n5-complete tag。后续 N5B 仅推进 G1，正式算法实验采用在线 generate；本次冻结回放
仍只作为执行验收，不扩展为生产预测入口。
