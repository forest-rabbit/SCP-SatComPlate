# Pre-N5C：CompFRR InputDeferred

状态：实现、维护验证及 R6/R7 两组完整仿真通过合同审计，等待用户审阅。
分支 `feature/n5-baselines`，[Draft PR #97](https://github.com/forest-rabbit/SCP-SatComPlate/pull/97)
以 `n5` 为 base；不合并、不进入 N5C，`main` 不变。

## 合同与实现

依据 `CompFRR_Backup_Frequency_Model_InputDeferred_v5.md` 及其实现审计任务书。
两份材料中，实际并行传输和统一 actual 浪费以审计任务书为准：不为套用加法近似而强制串行，
也不恢复旧的“normal + catchup”总浪费口径。F1/F2 仍独立抽样，联合概率只供策略使用。

R6/R7 执行提交为 `dea566206f5e68029d76349392559fe14b7c04d7`，启动时工作区均干净。
旧 R4/R5 沿用 `cfb7a0498d0f1d2182809372c12267cdf942b8c4` 的原始结果，未重跑。
旧六组结果及原 `evaluation.json` 保持只读；此前结论见
[六组 baseline 审阅](Pre-N5C-baselines-recompute-oneplusone.md)。

| 变更范围（相对 `contrib/satcompute`） | 内容 |
|---|---|
| `para.h/.cc`、`satcompute.cc` | `inputStagingPolicy=eager` 默认兼容；`deferred` 仅对 CompFRR 显式启用 |
| `protection/common/task-state-adapter.*` | 保留 eager 接口；显式 deferred committed state 为 K(w)，无原始 INPUT |
| `protection/mechanism/checkpoint/checkpoint-manager.*`、`storage/backup-storage-pool.*` | 状态初始化、逻辑零状态、相应存储布局与全网同时占用峰值 |
| `protection/policy/compfrr/frequency/`、`runtime/frequency-{protection-controller,storage-estimator}.*` | INPUT 成本一致性、第四条路径约束、状态存储准入 |
| `protection/runtime/recovery-controller.*`、`metrics/core/{recovery,protection}-metrics.cc` | 一次 INPUT、依赖汇合、F3 取消、状态就绪时刻及真实等待 |
| `tests/unit/`、`tests/integration/{smoke,regression}/` | 受控验证、冻结 runner、独立 InputDeferred 比较及既有 accounting 的显式策略适配 |

Eager 的初始化仍是完整 INIT_BASE 与当前 INIT_STATE 并行，接收后一次 cR；
非 LLM committed size 仍含剩余原始输入，LLM 为已完成 token 的 KV。
Deferred 正常期没有 INIT_BASE，也不为 INPUT 预留备份池；cL 后发送当前状态与 H，收齐后一次 cR 才进入 ON。
零进度有非零对象 ID 的逻辑零状态，仍付 cL/cR，不创建零字节 UDP。
四种 profile 的 deferred committed size 均为 K(w)，包括迁移 STATE；L1/batch/H/合法边界不变。

Frequency 的 Joff 保持 `P_finish*(S/B_I+xW/muB)`；deferred Jstart 在原相对评分上
加 `P_finish*S/B_I`，保证两边都承担故障 INPUT。ON 相对评分及 delta/n 枚举不变。
Deferred 可行域包含 `S/B_I+Rbar<=Rmax`，初始化估计为 `cL+Tstate+cR`；
source→remote 是新增的必要路径，沿用公共路径预览、FFP 排名及容量释放重试，不新增 placement 算法。

每次 accepted deferred recovery 向最终 recovery node 请求一次完整原 INPUT。
同星 LocalDelivery 有真实逻辑字节、无 UDP/网络字节；跨星走原 NetworkTransferEngine。
RECOMPUTE 从 0 WU 开始；checkpoint recovery 使用真实 committed state 和可选 tail。
INPUT、STATE、TAIL 在同一决策时刻请求，由网络竞争决定实际排队；
计算必须等待 INPUT 与对应状态依赖均就绪。只有非空 tail 应用付一次 cR，单纯复制 STATE 不付 cR。
来源星在 INPUT 接收前发生 F3 会终止恢复，接收后不再是该 INPUT 的依赖。
保留同纳秒 fault batch、原 compute deadline、恢复 attempt 的 F1/F2 免疫、唯一终态与资源清理。

统一浪费为：全部实际 primary/recovery/replica WU，减去按时成功逻辑任务的一份有效 W，
再加实际 normal 等价 WU 和实际 reserved-idle 等价 WU。失败任务的全部已执行量都计入。
planned catchup、actual catchup、post-catchup 单独报告，不再次加进总浪费；并行等待不重复相加。
详细接口与公式见 [保护模块 README](../../../contrib/satcompute/protection/README.md)。

## 验证与场景

- 项目配置保持关闭 ns-3 全局 examples/tests；目标构建通过。
- Python：101 项中 100 项通过，1 项依赖已有原生位置切片的测试按既有条件跳过；未用此跳过宣称重新生成了场景。
- 23 个 C++ 测试程序、11 组 smoke、4 组 maintained regression 全通过。
  其中状态合同 25,458 项、频率纯策略 211,081 项、频率运行时 4,811 项、恢复运行时 2,351 项检查。
- 修改前后二进制的 eager 小场景共 347 份 CSV/JSON 全部匹配，只有 run-summary 的壁钟耗时字段不比较。
  14 个 deferred 受控恢复输出另经离线依赖审计，覆盖四 profile、零状态、INPUT/state 两种先后次序、
  原来源 F3、LocalDelivery、busy recompute/relocate、迁移失败、一次 cR 与清理。
- 本轮未触发 GitHub CI；维护测试本地执行，不合并 PR #97。

本地测试日志为 `/tmp/scp-input-deferred-{build,python-tests,cpp,smoke,regression}.log`；
小场景证据位于 `output/input-deferred/compat-before`、`compat-after`。

两组均为冻结的 LEO-66（6×11）、800 tasks、1300 s、每星 100,000 WU/s、
10 Gbps、fixed 1 ms/link、deadline factor=1.3、20 s 网络更新、10 GB/node 备份池；
global-capacity-aware-hrw、size-aware、generate、seed=1/run=11、routing seed=1。
任务组成 240/240/240/80；INPUT=194,119,753,287 B，W=352,513,119 WU。
task 120、F3 node/time、故障参数、cL/cR、delta/n 范围和所有数据映射均未调整。
R4/R5/R6/R7 完整命令的非策略参数已逐项比较一致，不要求在线负载闭环产生完全相同的故障 trace。

| 组 | INPUT | REMOTE_BUSY | 原始输出位置 |
|---|---|---|---|
| R4 | eager | recompute | `output/n5-baselines/R4-compfrr-ffp-recompute-busy` |
| R5 | eager | relocate | `output/n5-baselines/R5-compfrr-ffp-relocate-busy` |
| R6 | deferred | recompute | `output/n5-input-deferred/R6-compfrr-ffp-recompute-busy` |
| R7 | deferred | relocate | `output/n5-input-deferred/R7-compfrr-ffp-relocate-busy` |

## 结果

R6/R7 都完整运行到 1300 s，退出码为 0，实际壁钟分别约 18 分 4 秒、17 分 39 秒。
`run_status=PARTIAL` 表示有任务按合同失败，不表示仿真提前停止。
每组均有 800 个唯一终态，恢复/存储清理及应用流量、FlowMonitor、执行 WU 守恒通过。
两组均为 F1=84、F2=2、F3=1，直接受害任务 83；请求/接纳恢复均为 83/83。
这些故障数量与旧 R4/R5 相同；未把故障 trace 相同设为验收条件。

以下 GB 为十进制 `10^9 B`，百万 eq-WU 是统一实际浪费；网络不换算成 WU。

| 指标 | R4 eager/recompute | R6 deferred/recompute | R5 eager/relocate | R7 deferred/relocate |
|---|---:|---:|---:|---:|
| 完成 / 失败（均 deadline miss） | 797 / 3 | 794 / 6 | 800 / 0 | 797 / 3 |
| 恢复成功 / 失败 | 80 / 3 | 77 / 6 | 83 / 0 | 80 / 3 |
| 实际总浪费，百万 eq-WU | 4.799179 | 8.782061 | 2.344499 | 6.118987 |
| 正常期额外网络，GB | 220.665042 | 100.850279 | 220.565773 | 100.850280 |
| 故障期额外网络，GB | 5.828236 | 25.064487 | 5.514117 | 25.423730 |
| 总额外网络，GB | 226.493278 | 125.914766 | 226.079891 | 126.274010 |
| T_catch P50 / P90，s | 0.091916 / 0.439208 | 0.333968 / 1.283738 | 0.096401 / 0.457682 | 0.329063 / 1.248434 |
| T_catch 最大值，s | 2.173660 | 2.373995 | 2.173660 | 2.373995 |
| 全网全程平均链路利用率 | 0.532432% | 0.450435% | 0.532558% | 0.450905% |
| 单链路最高全程平均利用率 | 4.143671% | 2.306578% | 4.194537% | 2.285034% |

T_catch 从实际故障到恢复追平故障前进度；只有确实追平的样本进入分布，
不是只选择最终成功任务。R4/R6/R5/R7 样本量分别为 80/80/83/82；未追平者不填 0。

### 三组对照与失败原因

- R4→R6：总额外网络减少 **44.41%**，正常期减少 54.30%；但实际浪费增加 **82.99%**，完成数少 3。
- R5→R7：总额外网络减少 **44.15%**，正常期减少 54.28%；但实际浪费增加 **160.99%**，完成数少 3。
  这不是“所有指标都更优”：主要代价来自恢复输入等待、START 时机及可用检查点进度的变化。
- R6→R7：忙时迁移多完成 3 个任务，实际浪费减少 **30.32%**；额外网络增加 0.359243 GB（0.2853%）。
  同一批 REMOTE_BUSY 任务为 114、456、475、551：R6 四个均重计算，1 成功/3 失败；
  R7 四个均 MIGRATE_TAIL，全部成功。实际 catchup 分别为 732,280 / 15,122 WU，
  实际 reserved-idle 分别为 139,452.9331 / 139,347.8107 eq-WU。
  迁移多传 STATE=321,233,659 B、TAIL=38,009,382 B；两组这些任务的 INPUT 网络均为 1,717,646,008 B。

R6 失败 task 114、399、456、475、574、596；R7 失败 task 399、574、596。
全部为 `COMPUTE_DEADLINE_EXCEEDED`，不是丢包、重复 INPUT 或依赖提前放行。

| R6/R7 共同失败任务 | 故障时状态 | 证据与原因 |
|---|---|---|
| 399，60 s | OFF，RECOMPUTE | TASK_RUNNING 时 Joff=0.097906、Jstart=0.163348，未启动；60 s 提议 START 时同一批故障已命中。完整重计算需要 2.23293 s，故障后 deadline 仅余 2.180566 s，另有 0.127504 s INPUT 等待 |
| 596，861 s | OFF，RECOMPUTE | TASK_RUNNING 时 Joff=0.055843、Jstart=0.071681；下一检查时故障命中。完整重计算需要 2.43821 s，deadline 仅余 2.364308 s，另有 0.136355 s INPUT 等待 |
| 574，180 s | ON，TAIL | 177–179 s 因 REMOTE_BUSY 暂停更新。task 552 占用 remote 0 至 179.263548 s（R5 为 178.975988 s），故 R5 能在 179 s 更新而新组不能。故障前已执行 782,779 WU，最新 local 只有 476,579 WU；恢复需 355,277 WU=3.55277 s，deadline 仅余 2.986337 s。STATE/tail 在 180.016661 s 就绪，INPUT 在 180.446967 s 就绪 |

上述三例即使仅去除 INPUT 等待、保持新组实际检查点不变，也不足以完成；
因此不能把失败全部归因于 INPUT 多占用了几百毫秒。旧 R5 三个任务均已 ON 且检查点更近，全部完成。
本轮没有更改全局检查节奏、busy 后更新语义、deadline 或场景来修复这些策略取舍。

task 120 同样需要单独标注：R5 在 1026 s START、F3 时已 ON，走 REMOTE_REDO；
R6/R7 延至 1027 s START，F3 于 1027.055770726 s 到达时仍 INITIALIZING，
因此是 STATE_MISSING→RECOMPUTE，完整 INPUT 等待 0.645505 s、T_catch=2.373995 s，最终按时完成。
不能沿用“task 120 已通过检查点成功保护”的旧实验描述。

### 实际浪费与恢复执行

| 指标，百万 WU 或 eq-WU | R6 | R7 |
|---|---:|---:|
| 成功任务额外执行 WU | 2.170767 | 2.064356 |
| 失败任务全部已执行 WU | 4.174110 | 1.617577 |
| task execution waste WU（前两项之和） | 6.344877 | 3.681933 |
| 正常保护 actual eq-WU | 0.417940 | 0.418020 |
| 实际 reserved-idle eq-WU | 2.019244 | 2.019034 |
| 统一 W_waste_actual eq-WU | 8.782061 | 6.118987 |
| planned catchup WU（不加入总浪费） | 4.278027 | 2.523316 |
| actual catchup WU（已包含在实际执行） | 3.188211 | 2.471053 |
| actual post-catchup WU | 24.141550 | 24.247501 |
| actual total recovery WU | 27.329761 | 26.718554 |

两组 primary actual 均为 328,177,528 WU，replica actual 为 0。
planned reserved-idle 估计分别为 1,989,432.9306 / 1,989,232.9306 eq-WU，
仅单独留档，不替代 actual 2,019,243.8940 / 2,019,033.6492 eq-WU。

恢复路径按 TAIL / REMOTE_REDO / MIGRATE_TAIL / MIGRATE_REDO / RECOMPUTE 排列：
R6=`58 / 3 / 0 / 0 / 22`；R7=`58 / 3 / 4 / 0 / 18`。

### INPUT 等待与流量分账

两组正常 INPUT/INIT_BASE 均为 0；故障 INPUT 共 83 次，逻辑总量均为 24,030,626,256 B，
其中 78 次跨星实际发送 22,404,319,294 B，5 次 LocalDelivery 不产生网络流量。
83 次均接收完成、依赖汇合完整；没有把未完成传输的等待当成 0。
两组都为 INPUT 先就绪 18 次、state 先就绪 65 次、同时就绪 0 次；
INPUT 因而在 65 次恢复中处于启动计算的关键路径。

| 等待，s | R6 P50 / P90 / max | R7 P50 / P90 / max |
|---|---:|---:|
| INPUT 从请求到接收 | 0.230623 / 0.393669 / 0.808734 | 0.230623 / 0.392828 / 0.808734 |
| `max(0, INPUT_received - state_ready)` | 0.230623 / 0.391118 / 0.776895 | 0.227397 / 0.377177 / 0.776895 |

第二行是在相同状态依赖下 INPUT 额外阻塞的时间，不是不同策略的因果总差异；
83 个样本的总量分别为 17.683158 / 17.366904 s。实际并行等待只按真实占用区间记一次。

| 实际应用层发送字节 B | R6 | R7 |
|---|---:|---:|
| INIT_BASE | 0 | 0 |
| INIT_STATE | 27,421,193,787 | 27,421,193,787 |
| L1 | 49,723,589,655 | 49,723,590,063 |
| REMOTE_BATCH | 23,705,495,985 | 23,705,495,985 |
| RECOVERY_INPUT | 22,404,319,294 | 22,404,319,294 |
| RECOVERY_STATE | 0 | 321,233,659 |
| RECOVERY_TAIL | 2,660,167,668 | 2,698,177,050 |
| business INPUT + RESULT | 293,253,389,630 | 293,731,030,366 |
| FT normal | 100,850,279,427 | 100,850,279,835 |
| FT fault-time | 25,064,486,962 | 25,423,730,003 |
| FT total extra | 125,914,766,389 | 126,274,009,838 |

这里按每条物理应用流源端实际发送 payload 计一次，包含未成功任务实际发送的字节，
不是逻辑声明字节、UDP 头或逐跳累计链路字节。链路利用率另据实际序列化忙时计算。
相对 R4/R5，全网全程平均链路利用率分别降低 15.40% / 15.33%，与应用额外流量降幅不是同一指标。

### 存储及 profile

| 备份池峰值，GB | R4 | R6 | R5 | R7 |
|---|---:|---:|---:|---:|
| 单节点最高 Used+Reserved | 2.003345 | 1.646002 | 2.039466 | 1.646002 |
| remote 部分单节点最高峰值 | 1.754457 | 0.983048 | 1.754457 | 0.983048 |
| local 部分单节点最高峰值 | 0.755679 | 0.755679 | 0.755679 | 0.755679 |
| 全网同一时刻 Used+Reserved 总峰值 | 未采集 | 2.235613 | 未采集 | 2.235613 |
| allocation failures | 0 | 0 | 0 | 0 |

R6/R7 的逐节点峰值相同，完整值保存在各组 `protection-node-storage-summary.csv` 和比较 JSON。
单节点最大值下降 17.84% / 19.29%，符合不再预留原始 INPUT 的存储合同。
全网新指标为直接观测的 2,235,613,184 B，不能用各节点不同时刻峰值之和冒充；
旧输出未采集该指标，本轮不重跑、不补造旧全网峰值。备份池不含 recovery working INPUT 内存。

| profile | R6/R7 发起保护数 | R6/R7 完成数 | R6/R7 浪费，百万 eq-WU | R6/R7 正常 FT，GB | R6/R7 故障 FT，GB |
|---|---:|---:|---:|---:|---:|
| dense-image（240） | 101 / 101 | 239 / 239 | 0.998014 / 0.998014 | 36.129737 / 36.129737 | 4.396688 / 4.396688 |
| sparse-inference（240） | 138 / 138 | 238 / 239 | 2.251059 / 1.337841 | 0.069609 / 0.069610 | 7.656039 / 7.656917 |
| compression（240） | 115 / 115 | 237 / 239 | 4.734272 / 2.984415 | 21.297446 / 21.297446 | 11.351988 / 11.710353 |
| llm（80） | 47 / 47 | 80 / 80 | 0.798717 / 0.798717 | 43.353487 / 43.353487 | 1.659772 / 1.659772 |

旧 R4/R5 均对 426 个任务发起保护，新 R6/R7 均为 401；
新组受害任务中有 66 个曾发起保护，故障时 65 个 ON、1 个 INITIALIZING，其余 17 个 OFF。
LLM 正常状态量仍较大：Deferred 只移除 INPUT，不移除真实 KV checkpoint 字节。

## 审计证据与停止点

新增比较结果在 `output/n5-input-deferred/evaluation.json`，由以下只读聚合生成：

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/analyze-input-deferred.py \
  --r4 output/n5-baselines/R4-compfrr-ffp-recompute-busy \
  --r5 output/n5-baselines/R5-compfrr-ffp-relocate-busy \
  --r6 output/n5-input-deferred/R6-compfrr-ffp-recompute-busy \
  --r7 output/n5-input-deferred/R7-compfrr-ffp-relocate-busy \
  --output output/n5-input-deferred/evaluation-review.json
```

分析器拒绝覆盖已存在的结果；上述复核输出必须不存在，不会重新仿真。
最终复核旧 R0–R5 原始文件及旧 `evaluation.json` 共 193 个文件大小、mtime 均未变化；
未增加 SHA-256。故障、任务、routing 源码及冻结输入未修改，`main`、`n5` 引用均未移动。

本轮合同门禁通过，但只覆盖一个冻结 seed/run，不能外推多种负载下的统计最优性。
解析 INPUT 加法为求解器近似，真实恢复仍按并行依赖和资源竞争执行；
working-set 内存峰值未量化，旧 eager 全网同时存储峰值缺失，均不补造数据。

建议保留 eager 默认、deferred 显式开关，让用户基于网络节省与 deadline/计算浪费取舍审阅。
不得以本轮网络下降为理由自动替换实验默认；不调整任务、token 映射、故障、seed、成本或频率目标。
本轮只提交、推送并更新 Draft PR #97，不合并、不打标签、不清理分支、不触发阶段 CI。

**STOPPED AT PRE-N5C COMPFrr INPUT-DEFERRED AUDIT — STOPPED BEFORE N5C。**
