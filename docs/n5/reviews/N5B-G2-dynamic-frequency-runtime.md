# N5B-G2：动态频率运行时审阅

状态：**G2 实现与本地验收完成，STOPPED AT N5B-G2**。本阶段只验证因果接线，不给出算法性能结论。

## 1. 分支与授权

- 分支：`feature/n5b-compfrr-frequency`；同一个 Draft PR #96，base=`n5`。
- G1 已审阅提交：`8fb450cc4b85238025ac3b022063aa5d0754416f`。
- `n5` 基线：`b626a154d423219bc1503f060252962e1965b4cf`。
- 本报告随 G2 提交；准确 G2 HEAD 以 `git rev-parse HEAD` / PR #96 最新 head 为准。
- 授权来源：用户提供的 `N5B_G1_Audit_and_G2_Start_for_Codex.md`，G1=PASS，允许完成 G2 后停止。
- 未修改 main/n5，未合并 PR，未运行 N5 CI，未进入 G3/N5C。

## 2. 修改范围

文件均在 `contrib/satcompute/`，除本报告外不另建阶段任务书。

| 位置 | 变化 |
|---|---|
| `fault/runtime/fault-model-engine.h/.cc` | 当前概率前后同步观察接口，共用 canonical prediction input 构造；不改模型或抽样 |
| `protection/runtime/frequency-protection-controller.h/.cc` | 任务注册、FFP/资源适配、提案及故障后提交、N5A checkpoint/recovery 接线 |
| `protection/runtime/frequency-storage-estimator.h/.cc` | 真实库存对应的 additional peak 估计 |
| `protection/mechanism/checkpoint/checkpoint-manager.h/.cc` | 只读库存、未来 cadence 更新/暂停、实际初始化完成通知 |
| `metrics/core/frequency-metrics.cc` | 独立决策 CSV；没有第二份 actual accounting |
| `satcompute.cc`、`para.h`、`CMakeLists.txt` | 显式 `protectionMode=compfrr`、generate/F1/F2 门禁及构建接入；默认 off 不变 |
| `tests/unit/frequency-runtime-test.cc` | 受控边界与真实 generate 的 13 个小案例、四类状态/字节/容量检查 |
| `tests/unit/fault-risk-query-test.cc`、`test_protection_contract.py` | 无副作用概率观察、CLI 与依赖边界 |
| `tests/integration/smoke/run-frequency-smoke.py`、已有两个 run-all 入口 | 四任务 CLI 与维护测试接入 |
| 平台/protection/tests README | 仅更新新入口、接口和测试说明 |

## 3. 故障 epoch 与概率合同

实际顺序由同步调用保证，不依赖偶然的 ns-3 event UID：

```text
推进当前 F1/F2 状态
  -> 使用当前 sampler 已算出的 q
  -> canonical P_finish（包含当前检查点）
  -> 当前 FFP/资源快照 -> Evaluate -> Propose
  -> 原有 F1、F2 独立抽样
  -> FaultController 同步应用整个当前故障批次
  -> 检查 primary 是否仍运行 -> Resolve -> 实际机制动作
```

`MakeFrequencyRisk()` 对当前 sampler q 与 predictor 当前 step 使用精确相等检查，没有 epsilon/clamp。
同一模型/状态/剩余计算时间/检查间隔传入已有 `PredictComputeFailureBeforeFinish`。
不使用 next-1s query、未来实际故障、未来队列或未来 F3。
任务启动只注册 OFF；短任务在下一全局检查点前完成，不创建决策或初始化。

F3 的预定时间不进入观察接口。若当前检查点同时执行 F3，仍按原合同不抽该节点的 F1/F2；
策略事前不知道这个结果，事后 CSV 用 `actual_fault_sampled=0, actual_fault_hit=1` 表达。
故障恢复/终态观察可能在 Resolve 前同步发生，因此仅延后 gate 的终态通知，
不延后真正的故障、任务失败或恢复。当前故障仍只能使用旧的真实保护状态。

## 4. 真实资源与机制

- **FFP**：OFF 只读当前候选；NONE 不分配对象。START 存活后交给机制，ON 固定原 pair，不启用 LRL。
- **算力/时限**：remote 的真实 ComputeService rate；Rmax 使用原 deadline、当前实际 WU、remote rate。
- **路径**：当前确定性接口顺序路由、残余瓶颈速率和传播；初始化走 primary→remote，L1/tail 使用当前路径。
  source=remote 的 INPUT 重算保留 LocalDelivery；分析中用最大有限带宽表示零序列化极限。
- **存储**：读取实际 base/batch、r/l、每条已捕获记录的 H 与分配/接收状态、used/reserved/free。
  OFF 覆盖初始化双对象临时峰值、CommittedStateBytes 和后续容量；ON 计未分配记录及未来合法增量，
  remote 峰值扣除已占用 state/batch，旧 batch 原地融合后才能创建下一 batch，不重复收取已有对象。
- **保守性**：local 不提前信用未来网络释放；OFF 用剩余状态上界覆盖未知的初始化排队延迟。
  这是 additional peak 的保守充分条件，不是最小所需容量。准入估计本身不预留资源。
- **START**：Resolve 存活后执行真实初始化；gate 只在物理初始化对象融合回调进入 ON。
- **delta**：只替换尚未触发目标，使用实际 WU/上次触发点/TaskStateAdapter；delta 不变不推迟原目标。
- **n**：新值用于尚未形成 batch 的记录；已形成/排队/传输/接收/cR-pending batch 均保留原工作边界和字节。
- **PAUSE/resume**：保持 ON、r/l 和已有操作；只取消未来 capture、阻止新 batch；下一存活 UPDATE 恢复。
- **实际账本**：继续 N5A cL/cR、UDP、存储、recovery、planned/actual WU 与 W_waste；没有预测值充当实际成本。
  既有 protection-task-summary 的 delta/n 保留首次配置，后续配置历史以 frequency-decisions 为准。

## 5. 审计输出

仅 compfrr 模式新增 `frequency-decisions.csv`；off/fixed 清理旧同名输出，默认不打开概率审计 CSV。
记录 task/profile/time/phase、q/P_finish、真实 WU/x、FFP/free/rate/bandwidth、J/Rbar/normal/Rmax/Tinit、
当前/最优候选/提交 delta/n、sampled/hit/committed、phase_after/reason、额外存储峰值。
不适用字段留空；NONE 可以保留最优候选分数，用于解释不启动原因。
`proposed_action` 与 `decision_committed` 分离；actual metrics 原 CSV 保持原口径。

## 6. 验收证据

### 同轮故障与在线 generate

| 案例 | 实际证据 |
|---|---|
| 受控 START+hit | 0.2 s 提议 `(56,5)`，未提交、没有初始化对象/流，故障按 OFF 进入重算 |
| 受控 UPDATE+hit | 0.2236 s 已 ON，旧 `(56,5)`，提议 `(12,5)`；未提交，保留旧配置并真实恢复 |
| 原生 generate | 0.1 s START `(32,6)`；真实物理 init commit 在 0.133420058 s，之后才出现 ON 决策 |
| 原生同轮 UPDATE+hit | 0.8 s，q=`0.18149763749787706`；旧 `(45,6)`，提议 `(25,6)` 未提交，进入 RECOVERING |
| 原生 F3 | 0.1 s F3，不抽 F1/F2；事前仍提出 START，事后未提交、无初始化 |
| 短任务 | 下一个 0.1 s 检查点之前计算完成，零决策、零初始化 |
| 真实资源 PAUSE/resume | 占满实际池后提议 PAUSE，仍 ON 且停止新 capture；释放竞争预留后 UPDATE 恢复 |

原生 generate 案例同时启用 F1/F2：8 条策略概率与 fault-model 概率记录逐值匹配，
P_finish 同样逐值匹配；当前命中的那条决策没有提交。受控注入只存在于测试可执行程序，
不增加生产 fault CLI/随机覆盖接口；CSV 的 `sampled=0` 明确区分受控注入与随机抽样。
已有 risk-query 测试同时对比观察器开/关的实际事件、温度、F1/F2 draw count 与 occurrence count，保持一致。

### 四类真实机制

下表是受控 cadence API 测试，不是四类任务的算法优劣实验；START 来自求解器，
后续显式更新机制接口以确保 n/delta、pending、in-flight、PAUSE/resume 边界都被覆盖。

| profile | 实际 protection 事件 | 实际注册保护流 |
|---|---:|---:|
| dense-image | 239 | 29 |
| sparse-inference | 271 | 33 |
| compression | 239 | 29 |
| llm | 351 | 43 |

逐事件验证 `r<=l<=x`；每条捕获验证合法边界及 `RecordBytes(previous,work)`（含 H）；
每个 batch 验证真实 L1 字节和。将至少 3 条已接收记录的 n 从 20 改成 2，立即使用旧 pending 形成批次；
再改为 n=1 不改变既有批次。暂停期间原 batch 继续提交、无新目标；恢复继续真实流。
所有池最终 used/reserved=0、峰值不超过容量。重复受控运行的决策 CSV 完全相同。

### 全部本地检查

- 21 个 C++ 测试程序通过；新增 frequency-runtime **3,003** 项检查。
- 65 项 Python 单元测试通过，无 skip。
- 10 组 smoke 全部通过；新增四任务/15 s CLI 测试有 5 条 OFF 决策，概率完全匹配，审计开关不改决策。
  该低风险短 workload 没有 START 是预期结果；真实 START/UPDATE/hit 由上面的原生 generate 小案例证明。
- 既有全部 regression 通过，包括路由、任务、故障生命周期及 N4B 100 任务维护验收。
  后者仍为 88 完成/12 失败、11 START、483 条实际概率与预测概率一致，与 G1 基线相同。
- 原 FIXED smoke 仍为 4 任务、81 个保护流全部完成、17 次含初始化的 remote commit、零存储泄漏。
- 目标编译通过；`NS3_EXAMPLES=OFF`、`NS3_TESTS=OFF`。没有运行上游 examples/test.py。

## 7. 运行与输出

持久本地证据（由 gitignore 忽略，不提交生成物）：

- `output/n5b-g2-frequency-runtime/`：13 个小案例的 decision/protection/recovery CSV。
- `output/n5b-g2-frequency-smoke/audit/`：真实 CLI probability-vs-probability 核对输出。
- `/tmp/scp-n5b-g2-{cpp,python,smoke,regression}.log`：本地完整检查日志。

复现新增验证：

```bash
.venv/bin/cmake --build cmake-cache --target satcompute_app_satcompute satcompute_test_satcompute-frequency-runtime-test -j 4
./ns3 run --no-build "satcompute-frequency-runtime-test --outputDir=output/n5b-g2-frequency-runtime"
.venv/bin/python contrib/satcompute/tests/integration/smoke/run-frequency-smoke.py --outputDir=output/n5b-g2-frequency-smoke
```

全部维护检查沿用 tests README 的 C++ / Python / smoke / regression 入口。
新功能运行均为 16 星小 fixture、单例 4 s 或 4 任务 15 s；1000 s 的运行仅是既有 N4B 维护回归。
没有运行正式 800-task/1300 s frequency comparison、多 seed、论文性能测试或 N5 CI。

## 8. 限制与 G3 前置问题

1. Rbar 是解析均值，不要求等于真实 recovery；真实队列、状态与 deadline 仍可能导致恢复失败。
2. 存储适配偏保守；正式 G3 应观察实际拒绝原因、峰值与开销，不据此宣称容量最优。
3. ON 不迁移 FFP pair；节点忙碌/故障可能导致暂停或恢复失败，这是保留 baseline，而非 N5C 优化。
4. START 准入不是对全部未来空间/带宽的预留；实际并发竞争仍由 N5A 机制拒绝并记录。
5. G3 待用户审阅后确定正式对比参数与验收范围，不在本阶段提前给出性能结论。

**STOPPED AT N5B-G2 — Draft PR #96 保持 Draft；不进入 G3，不合并，不清理仍在使用的 N5B 分支。**
