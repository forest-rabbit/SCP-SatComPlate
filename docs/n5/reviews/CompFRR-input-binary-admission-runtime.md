# CompFRR INPUT binary admission：实现与开发验收

人工批准任务书：`CompFRR_INPUT_Binary_Admission_Production_Kickoff_for_Codex.md` 及四项补充。
基线 `34177d0cf258fb4d58d3a9eb28c5872b1a39af0d`，开始时工作区干净；实现分支
`feature/compfrr-input-admission-runtime`。不混入 JIT/V7、不改 Frequency/placement/RNG/正式场景，默认 admission=none。

## 增量与边界

1. 纯规则：整数纳秒、canonical trajectory、严格大于；固定 405 网络 snapshots 全部逐任务匹配，
   SER=68、NET=115、OLD_ONLY=0。P=0/1、相等、部分 lead、零传播、取整、溢出和非法窗口测试通过。
2. 中性生命周期：独立 INPUT_STAGING 与 PREFETCH_INPUT；只读 resolver、最终接受后 handoff、
   真实 receiver join、实际 compute start 才 USED。Checkpoint cleanup 不再释放其他机制持有的 INPUT。
   checkpoint quota 与独立 INPUT 容量相加；不把 INPUT 当 Kvar 或增量维护额度。
3. 在线接线：START 前冻结 actual pair snapshot、初始化准入后才执行一次性请求；无 epoch 重试。
   `none` 不实例化 optional owner，Shared Recovery 无 CompFRR-private selector 依赖。
4. 验收：先本地 gate，再固定代码执行 D/E/S/N 开发 run11，最终停在人工审阅点。

小 gate：439 个 Python 测试通过（1 个既有跳过），其中纯 selector 6 项、运行账目/CLI 6 项；
20 个真实生命周期场景 282 checks，两规则 online 接线 230 checks。
完整构建、C++、smoke（含16组 placement）及 regression 已通过。
与 N5R corrected 基线的 1,965 份既有 CSV/JSON 等价，仅排除既有 wall-clock 字段和输出路径；
新测试目录额外产生 40 份文件，不替换历史 golden。证据及日志索引为
`output/compfrr-input-admission/small-gates.json`。
生命周期覆盖接收前/后正常结束、IN_FLIGHT 故障后继续发送、READY 源星 F3、holder F3、
两种合法 refetch、接管后流失败、同纳秒 ready-first/fault-first 与同批 primary/holder 故障。
故障前 INPUT 无论发送多少都不会靠分析定时器变成 READY。

## 版本与证据口径

新增日志默认关闭；只有非 none 的 optional INPUT 运行才输出其审计文件。
旧 Stage B 输出不修改。68/115 仅验证纯 selector，不约束真实四组的 START 数、流数或实际故障数。
本轮不自动提交/推送；开发执行记录真实 dirty implementation，以源码归档与字节比较记录四组同版本，
不把基线 Git ID 伪称为包含实现的 clean commit，也不使用 SHA-256。

四组固定为 D=Deferred+none、E=Eager+none、S=Deferred+SER、N=Deferred+NET，
均为 CompFRR+N5C FULL+relocate、seed1/run11、800任务、1300s。允许共享资源反馈改变真实故障轨迹，
不会强制 replay。全体结果与可匹配故障子集分开，不把未恢复样本记成零等待。
`B_prefetch_total` 为同一流完整生命周期的实际发送量；独立 used/unused 和 normal/post-fault 分解。
网络总量按业务表与保护表的 transfer ID 并集统计，不把仅业务的 `transfer-summary.csv` 误当全网络。
执行浪费采用既有守恒 helper：实际执行 WU 减去成功任务的唯一有效 WU；再加常态保护与恢复预留空闲 eq-WU。

## 开发运行复现

在项目根目录使用项目 uv 环境；输出目录必须不存在，入口拒绝覆盖证据：

```bash
source .venv/bin/activate
python contrib/satcompute/tests/integration/regression/run-input-admission-development.py \
  --output-root output/compfrr-input-admission/20260915-development-run11 \
  --gates output/compfrr-input-admission/small-gates.json
```

四组逐项完整 argv 见各自的 `execution.json`；同目录保存运行日志、实际退出码及 wall-clock。
根目录的 `execution-source.zip` 保存本次实际执行源码，`implementation.patch` 保存 tracked diff；
执行入口在每组前后逐字节验证源码没有变化。`comparison.json` 与 `paired-comparison.json`
分别保存全体统计和真实同故障子集统计，不将基线 Git ID 当作实现提交 ID。

## 四组结果：DEVELOPMENT / CALIBRATION

四组均到达 1300 s、800 个唯一 terminal、800 完成、0 失败、0 deadline miss；
87 条真实故障记录（F1=84、F2=2、F3=1），83 个直接受影响任务，83 次恢复接受并成功。
四组可匹配的 primary fault 均为 83 个，不是通过 replay 强制配对。
D/S/N 的恢复路径均为 TAIL=73、REMOTE_REDO=8、MIGRATE_TAIL=2；
E 为 TAIL=70、REMOTE_REDO=7、RECOMPUTE=5、MIGRATE_TAIL=1。

以下 GB 均为十进制实际应用层发送量，不是链路逐跳 wire bytes；FT 不包含本来就需要的业务 RESULT。
预取完整生命周期跨过故障点的发送量仍计入同一个 flow。计算“总浪费”包含实际执行浪费、常态保护
与预留空闲 equivalent cost，不把预留空闲当成真实 CPU 执行。

| 指标 | D：Deferred | E：Eager | S：SER | N：NET |
|---|---:|---:|---:|---:|
| fault→compute start 均值 / P50 / P90 / max，ms | 237.813 / 241.855 / 410.114 / 805.580 | 35.102 / 14.969 / 80.545 / 394.397 | 99.805 / 22.531 / 326.403 / 645.505 | 99.567 / 22.531 / 326.403 / 645.505 |
| fault→catch 均值 / P50 / P90 / max，ms | 314.408 / 279.189 / 492.991 / 2373.995 | 116.611 / 69.088 / 258.764 / 1728.490 | 175.073 / 75.914 / 393.600 / 2373.995 | 177.962 / 80.418 / 394.685 / 2373.995 |
| 实际 INPUT critical wait 总量，ms | 17987.769 | N/A¹ | 6584.153 | 6494.226 |
| 实际任务执行浪费，WU | 635735 | 676523 | 624725 | 650677 |
| 常态保护，eq-WU | 442750 | 431500 | 443890 | 442490 |
| 预留空闲，eq-WU | 1973849.979 | 291344.576 | 828382.674 | 826408.393 |
| 总浪费，eq-WU | 3052334.979 | 1399367.576 | 1896997.674 | 1919575.393 |
| PREFETCH_INPUT，GB | 0 | 0² | 19.995838 | 20.109077 |
| 故障后新 RECOVERY_INPUT，GB | 23.790304 | 1.166878 | 8.544910 | 8.431696 |
| INPUT-related FT 总量，GB | 23.790304 | 111.696653 | 28.540748 | 28.540773 |
| 常态 FT / 故障 FT，GB | 83.041906 / 25.585059 | 196.272078 / 3.282594 | 102.561531 / 10.760337 | 102.632482 / 10.734724 |
| 总 FT，GB | 108.626965 | 199.554672 | 113.321868 | 113.367207 |
| 全网络应用层总发送，GB | 402.913004 | 493.840710 | 407.607907 | 407.653245 |
| 全程平均链路利用率，% | 0.413897 | 0.424937 | 0.419997 | 0.420024 |
| 单链路最高全程平均利用率，% | 1.146398 | 1.815213 | 1.199597 | 1.216368 |
| 全局同时 storage peak，GB | 1.489452 | N/A¹ | 1.798157 | 1.798157 |
| 最大单节点 storage peak，GB | 0.890773 | 1.728243 | 1.194257 | 1.194257 |
| 独立 INPUT_STAGING 同时 peak，GB | 0 | 0² | 1.000000 | 1.000000 |

所有恢复时间分布样本数均为 83，无未恢复样本被填成 0。
¹ 既有 Eager schema 没有 Deferred 专用 `state_ready_time_ns` 与全局同时存储峰值导出，
因此不把缺失值填成 0，也不把各节点峰值相加冒充同时峰值；其开始计算/追平时间及逐节点峰值均有实际记录。
² Eager 的输入预置属于既有 INIT_BASE（实际 110.529775 GB），不是新 PREFETCH_INPUT / INPUT_STAGING；
INPUT-related 总量包含 INIT_BASE、PREFETCH_INPUT、RECOVERY_INPUT，避免漏算 Eager。
常态/故障 FT 按既有机制角色拆分，新 proactive flow 则严格按实际故障点分割同一生命周期的字节。
逐节点峰值保存在 `protection-node-storage-summary.csv`，独立 INPUT 峰值在 `input-prefetch-summary.json`。

## INPUT 生命周期验收

| 统计 | S | N |
|---|---:|---:|
| 成功 START 的 selector snapshots | 409 | 409 |
| SEND / 实际网络准入 / LocalDelivery | 72 / 68 / 4 | 119 / 115 / 4 |
| primary fault 时 READY / IN_FLIGHT / 未请求 ABSENT | 51 / 3 / 29 | 56 / 3 / 24 |
| 实际 recovery compute-start USED | 52 | 57 |
| no-fault proactive 任务数（含同星） | 18 | 60 |
| no-fault proactive 实际 GB | 3.788130 | 3.788155 |
| unused proactive 实际 GB | 4.750443 | 4.750469 |
| WRONG_TARGET_REFETCH 次数 / 原流 GB | 2 / 0.962314 | 2 / 0.962314 |
| 已建立预取失败 / 未准入 | 0 / 0 | 0 / 0 |
| 故障后原 proactive flow 继续发送，GB | 0.484454 | 0.484454 |

这里 68/115 恰好仍成立，但没有把它设为 runtime gate。对本次 S/N 各 409 个真实 snapshot
重新执行纯 C++ selector，逐任务决定全部匹配。新增网络 SEND 为 47 个（46 个 LLM + task 457）。
两个 wrong-target 任务为 140、455；合法 refetch 保留原 962313615 B，不记为 duplicate bug。
三条 IN_FLIGHT 原流在故障后继续复用，确认实际 receiver completion 后才允许计算；
上述 0.484454 GB 已计入完整 proactive 总量与 used/unused 同一生命周期范围。

四组通过唯一 terminal、实际服务时间与 compute-node ledger、WU、FlowMonitor 与物理 flow-ID 并集
字节守恒检查；无活动 flow、路径预留、storage 或请求/merge/lock 泄漏，无非法重复 INPUT/逻辑结果。
D 与旧最新 Stage B 的 35 份共同原始 CSV/JSON 再次等价。

初次运行结束后的汇总器遇到旧 Eager 缺少上述专用列的 KeyError，四个仿真进程本身均已退出 0。
只修正离线 analyzer 的缺失值处理、增加全局账本守恒断言和一个回归测试，再读取原输出完成四组审计；
未改仿真源码、未重跑实验。最终 Python 为 440 tests（1 个既有跳过），13 个 INPUT-focused tests 通过。
`postprocess-provenance.json` 区分执行源码与这两份后处理源码；`additional-lifecycle-accounting.json`
保存上表的派生统计。执行耗时 D/E/S/N 分别为 1104.820 / 1150.852 / 1118.262 / 1122.159 s。

## 结论：KEEP_SER_BREAK_EVEN

本轮优先保留 S 进入后续人工决策；不改默认 none，不删除 N，不追加实验或调整公式。

- 相比 D，S 总 FT 增加 4.694903 GB（4.32%；全网络总流量仅增加 1.17%），
  平均恢复计算等待下降 58.03%，平均追平时间下降 44.32%，总浪费下降 37.85%。
- 相比 E，S 总 FT 减少 86.232803 GB（43.21%），但平均追平时间增加 58.463 ms（50.13%），
  总浪费高 35.56%。这是流量与恢复速度的取舍，不能说在所有指标上优于 Eager。
- N 相比 S，多发 proactive 113239273 B，但少发 recovery INPUT 113214001 B，
  所以 INPUT-related 净增仅 25272 B；包含 checkpoint/tail 闭环反馈后总 FT 净增 45338135 B。
  平均恢复计算启动快 0.238 ms，但平均追平慢 2.889 ms，总浪费增加 22577.720 eq-WU（1.19%）。

S/N 的 83 个同故障样本中，task 457 因 INPUT 预置追平快 92.207 ms；
task 795、551、352 分别慢 203.366、68.715、57.623 ms，抵消该收益。
原始证据显示：795 在 N 中出现 `CAPTURE_BLOCKED_PATH`，故障时 local 已提交 WU
由 320079 降为 301204，catch-up redo 从 4893 增为 23768；551 的 redo 从 3568 增为 10645；
352 的 remote watermark 从 521405 降为 322438，需传输更大的 tail。
这是实际资源/维护轨迹反馈的记录，不是 selector 公式出错，也不据此重调 Frequency 或节点策略。
`paired-comparison.json` 保留所有任务差值，不只展示获益任务；本次单 run11 结论不等于独立 runs 上的普遍优越性。

开发验收后停在人工审阅点；用户随后授权提交并推送本实现与报告。
未运行 CI、未创建 PR/合并、未启动正式 independent-run matrix。

## Final INPUT Closeout

用户批准最终收尾任务书及四项补充；从已推送、干净的 `a4315e2b8` 继续同一实现分支。
旧四组结果与本报告快照保存在 `output/compfrr-input-closeout/` 和原输出目录，不覆盖历史证据。

### G1：尚未建立的预取不伪装为在途流

跨星 `REQUESTED`（包括 flow ID 尚未分配、已经注册但 sender 尚未准入）统一返回 FETCH，
使用当前 fresh-path 估计；独立诊断为 `PREFETCH_NOT_ESTABLISHED`，不是失败预取 refetch。
候选查询不改状态；最终 target 接受后才取消 pending request、释放对象，guard 阻止后续复活。
真正 IN_FLIGHT 仍用当前已建立流估计，真正 compute start 仍等待 receiver；同星 LocalDelivery 不变。

新增未注册取消 owner-port 测试，以及同纳秒注册/准入之间真实故障恢复测试。
22 个生命周期场景、374 checks 通过；完整 build、18021 contract checks、230 online INPUT checks、
13 项 INPUT Python tests 通过。日志位于 `output/compfrr-input-closeout/g1-*.log` 与 `pending-gate.log`。
接下来退役 N 正式入口、完成完整本地 gates，再冻结同一 clean commit 运行 D/S。
