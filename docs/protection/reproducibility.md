# 验证、指标与历史证据

## 公共输出与小规模 fixture

四个输出由 `metrics/core/protection-metrics.h/.cc` 写入普通 outputDir，fixed/compfrr 开启：

| 文件 | 内容 |
|---|---|
| `protection-events.csv` | START、初始化、捕获/生成、接收、commit、清理；每行含 l/r/x 与存储对象身份/池快照 |
| `protection-transfers.csv` | 真实保护流类型、稳定 ID/端口、请求/启动/发送完成/接收/终态时间及实际字节 |
| `protection-task-summary.csv` | 放置、S/W/K、delta/n、cL/cR、有效进度、生成/提交计数和停止原因 |
| `protection-node-storage-summary.csv` | 各计算星备份池容量、used/reserved、各类峰值及容量拒绝次数 |

`transfer-summary.csv` 与 run-summary 的业务应用字节/完成数仅含 INPUT/RESULT；
FlowMonitor、链路负载及路由容量账本仍包含所有真实保护包。真实 sender-finished 时间从
发送器的完成字节/最后发送时刻取得，接收完成与其分列；没有网络 ACK。

四类 fixture 位于 `tests/fixtures/protection/`：16 星、计算星均为 100000 WU/s，
4 个任务依次在主星 3 计算，固定 local=2、remote=0，15 s 仿真、10 Gbit/s、1 ms。
任务 1 为 dense-image：S=52428800 B、W=78644 WU、Kvar=52429200 B，
delta=5%、n=4、每星额外池 10 GB；其余为 sparse-inference、compression、5000-token LLM。
fixture 仅用于执行验收，不改变正式任务场景或 para 默认保护关闭。

## 资源账本

跨方案计算资源指标统一标注 **eq-WU（equivalent cost）**：

- `W_execution`：全部物理 attempt 的实际执行 WU，减去每个成功逻辑任务一份有效 W；
  成功任务的多执行与失败任务的全部已执行量分列，在资源汇总中按等值 eq-WU 记账。
- `W_normal`：已完成保护事件的等价成本，单位 eq-WU；cL/cR 不占主 ComputeService，
  不能将这部分声称为测得的额外 CPU 执行量。
- `W_reserved_idle`：真实恢复/副本预留期间未计算的容量机会成本，单位 eq-WU；
  不表示当时必然存在被阻塞的其他任务，更不是实际 CPU 重算。
- **Active equivalent compute overhead**：`W_active = W_execution + W_normal`（eq-WU）。
- **Total compute-capacity equivalent waste**：
  `W_total = W_active + W_reserved_idle`（eq-WU），对应统一 analyzer 的 `w_waste_actual`。

planned 不补 actual；catchup/post-catchup 是执行剖面，不再次加到 `W_total`。
原始任务工作量、已完成/检查点进度仍是整数 WU，网络与存储仍用 Byte，不改变字段合同或量纲。
故障时已执行量写为 `W_f`，local/remote 有效检查点写为 `W_L/W_R`，
检查点落后量为 `W_f-W_L`、`W_L-W_R`（WU）；比例需要另除以任务总 W。

历史 Pre-N5C 例子（不是 N5R 新结果）：FA-LRL 的 R5/R7 均完成800任务：`W_active` 为1.152619/1.068858百万eq-WU，
`W_total` 为1.452554/3.056674百万eq-WU。deferred 的主要增加项是预留等待机会成本，
不是更多 CPU 重算。完整冻结证据见
[32组正式报告](../n5/reviews/Pre-N5C-placement-baselines-final.md)。

`T_catch` 是故障到恢复计算追平 `W_f` 的时间，不是 RESULT 交付时延；
未追平的任务没有样本，不记为零。比较同时给出样本数、mean/P50/P90/max；
严格逐任务时延比较使用双方成功且故障时刻一致的样本，并说明选择范围。

LRL/FA-LRL 是 N5C 的强 baseline，必须在同一受控合同下公平比较，不预设结果；
不为获得优势调整冻结输入。multi-seed 不是本阶段门槛，结论只适用于受控场景。

### 计划与实际执行

`recovery-summary.csv` 区分 planned 与 actual 三列：catchup_redo、post_catchup、total，单位 WU。
TAIL（含 MIGRATE_TAIL）/REMOTE_REDO（含 MIGRATE_REDO）/RECOMPUTE 的起始进度分别为 lf/rf/0，计划 catch-up 为 `xf-start`，
计划 post 为 `W-xf`，计划 total 为 `W-start`。xf/lf/rf 均为整数 WU，不是百分比。
实际 WU 由 ComputeService 在真实服务完成/取消/停止时保留，使用
`min(planned, floor(actual_service_ns * rate / 1e9))`；正常完成 actual=planned，
失败只计执行前缀。post-catchup 须统计，但不属于重复计算 waste。
若用归一化进度 x_f 表示，Recompute 的 `x_f * W` 仅是 planned full catch-up；
发生再次中断时不能把这个计划值记作 actual。

正常成本只计实际完成事件：初始化 `INIT_STATE_GENERATED*cL + INIT_COST_COMMITTED*cR`；
后续 `L1_GENERATED*cL + REMOTE_COST_COMMITTED*cR`，初始化不再计入后续次数。
尚未完成生成/物理提交的取消操作不按完整 cL/cR 收费；这是事件完成计费，不是假设占用了真实 CPU。
物理提交成本事件与名义 RemoteCommit 区分，同 ns fault 导致未物理提交的操作不计提交成本。
任务保护表保存四项次数、成本 ns、主星速率和 normal eq-WU（equivalent cost）。

N5A 历史机制诊断列为
`normal_protection_eq_wu + recovery_reserved_idle_eq_wu + recovery_catchup_actual_wu`（eq-WU）。
它不包含最终失败任务的全部执行浪费，不能直接当作当前跨方案的 `W_total`；
统一统计使用 `tests/support/protection/baseline_audit.py`（旧 CLI `analyze-baseline-evaluation.py` 转发） 从实际账本重建上述 `W_execution/W_total`。
normal 用主星速率换算；reserved-idle 用恢复星速率，区间是 accepted 到 compute start，
从未开始则到释放/失败。等待 tail 的 cR 已在该区间内，不重复加一次。
该历史机制诊断不计恢复后的正常剩余计算；当前 `W_total` 仍计入失败任务的全部实际执行量。
唯一 winner 的业务 RESULT 不计入额外备份网络开销。

存储表保留各节点 used/reserved/total 峰值及 final used/reserved；任务表的 local/remote peak
是本任务在该节点的同时 used+reserved 峰值，不拿整个共享池峰值冒充。
`protection-finalization.json` 检查存储、恢复锁、在途 runtime flows、待注册请求/提交/定时器清空，
并列出容量分配失败的任务 ID。fixed 仿真结束还将未终结任务标为 `FAILED/SIMULATION_ENDED`；
off 的原有截断合同不变。

历史机制校验脚本 `tests/integration/regression/analyze-protection-accounting.py` 从真实 CSV 校验并生成
全局、四类任务、恢复路径/终态分组的 planned/actual WU、机制诊断 eq-WU、存储/网络与完成/deadline 统计。
网络分别保留 declared/sent/received，主备份开销使用实际 sent payload；同星交付 network=0，
跨星恢复 RESULT 单列。脚本默认不随平台或 CI 启动，不修改原始 CSV。

测试与指令见 [tests](../../contrib/satcompute/tests/README.md)，早期执行验收证据见
[N5A-G1](../n5/reviews/N5A-G1-architecture-storage.md)、
[N5A-G2](../n5/reviews/N5A-G2-fixed-backup-path.md)、
[N5A-G3](../n5/reviews/N5A-G3-recovery-loop.md)、
[N5A-G4](../n5/reviews/N5A-G4-integration-accounting.md)。

## N5R small semantic-equivalence gate

N5R 不跑新的正式 800 任务 / 1300 s 矩阵。基线来自 PR #102 corrected tree，
`tests/integration/regression/run-protection-equivalence.py` 运行现有 C++ fixture 及上述轻量 CLI 场景。
11 个组合覆盖 F Eager/Deferred、P 两种正式 pressure、恢复、Recompute、1+1 和 CB；
比较全部 CSV/JSON 文件集合。CSV 包括 schema、顺序、数值，逐字节一致；
JSON 只规范化输出位置与 wall-clock 耗时。fault/WU/bytes/storage/path 等语义字段不豁免。

最终 owner 收口中，CB 的实际 profile 文件位置随授权迁移而变化，两个 CB CLI 参数 JSON
诚实输出新 `profile_path`。`--allow-cb-profile-relocation` 单列这一精确元数据映射，先验证冻结
profile 逐字节不变；其他参数及全部 CSV 仍严格比较。不开启时，原 gate 会拒绝这两个路径差异。

实际对照目录、阶段 gate 与提交见 [N5R 执行记录](../n5/reviews/N5R-implementation.md)；
详细命令见 [tests](../../contrib/satcompute/tests/README.md)。
不覆盖旧输出，不刷新 golden 以消除差异，不用 SHA-256 代替语义验证。
这些小测试证明覆盖案例的等价性，不等于完整正式性能矩阵刷新。

长期复用的 Python 审计逻辑位于 `tests/support/protection/`：
accounting、frequency、baseline、placement、risk-start、INPUT 和 scenario。
旧 `integration/regression/analyze-*.py` / `run-final-scenario.py` 入口保留兼容转发；
测试 oracle 不调用 production solver。

## 历史保留与现行证据

- [#102](https://github.com/forest-rabbit/SCP-SatComPlate/pull/102) 已将 #100/#101、
  deadline/INPUT-path recovery 与 checkpoint-maintenance 修复整链合入 n5。
  [原只读架构审计](../n5/reviews/Protection-architecture-consolidation-audit.md)及其 manifest 是重构前快照，不追改其路径和结论。
- [N5C 早期验证](../n5/reviews/N5B-closeout-N5C-kickoff.md)、
  [U 五轮](../n5/reviews/N5C-U-multirun-audit.md)、
  [Rational-U 主场景](../n5/reviews/N5C-rational-U-main-scenario.md)、
  [Rational-U 五轮](../n5/reviews/N5C-rational-U-multirun.md)
  保留执行提交和原结果；不能与 corrected chain 混称同一合同。
- [Recovery 修复](../n5/reviews/Recovery-direct-deadline-feasibility.md)之后，
  [Checkpoint maintenance 修复](../n5/reviews/Checkpoint-maintenance-semantics-audit.md)
  完成 15 组 corrected U 验证。当前比较以该链为基础；旧结果只有维护轨迹与完整资源账本等价才可复用。
  completion 相同或没有 pause 后故障都不构成等价证明。
- [RECENT_U](../n5/reviews/N5C-recent-U-evaluation.md) 是已停止的历史实验。
  正式 CLI 拒绝 `recent-U`；历史 enum、fixture、CSV 字段和分析入口存在真实依赖，暂不物理删除。
  部分输出不能作为完成的性能证据。noU 仅为消融，不成为第三种正式 pressure。
- JIT/V7 保留在原实验分支，不进入 N5R。旧正式 runner 的冻结提交/source guard 仍有效；
  不为方便重跑而放宽它，也不在 N5R 自动启动历史矩阵。正式刷新留待 N6/N7 单独批准。
