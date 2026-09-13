# Pre-N5C：CB-Sat 修订与 CompFRR V7 INPUT-JIT

2026-09-13，按项目外 `CompFRR_V7_and_CBSat_Audit_Adjustment_Taskbook.md`
及已确认的 V7 Event-Aware JIT 模型实施。两条工作流分提交、测试和执行证据。
本报告同时作为本轮进度记录，不另建中间合同。

## 身份与边界

- 起点：`n5@2ccfa392e`；CB-Sat 分支及 PR #98 head 为 `5387bb4b9`，工作树干净。
- CB 修复先保留在 `feature/pre-n5c-cb-sat`；其后 V7 使用独立分支，未授权自动合并。
- 旧正式目录 `output/cb-sat-v2/20260912T171143276614Z-formal` 只读，执行版本
  `494e133c5`；旧 32 组 `output/pre-n5c-placement-final` 也不覆盖。
- 保持 800 任务、352513119 WU、400 WU/token、10 Gbps、1 ms、原 deadline、
  MTBF profile、故障参数/RNG、路由和 placement。只改获批 CB INPUT 有效性与 V7。
- 任务书第 1.3/14.3 节“尚待确认”已被用户后续确认替代：未准入可合法重试；
  已建立预取失败不自动重传；网络 total/used/unused 覆盖同一完整生命周期；
  同星 LocalDelivery 无 UDP；JIT 不保证故障前完整到达。第 9.2 节网络注册仅适用于跨星。
- CB `busy=recompute` 为主 adaptation；relocate 仅称扩展迁移版，不宣称性能上界。
- GitHub CI 沿用 N5 最终集成阶段门，不为本轮增量触发；不合并 main、不打 tag、不进入 N5C。

## 阶段与验收

| 阶段 | 结果与验证 | 进度 |
|---|---|---|
| G0 | 当前接口、旧结果影响、工作身份只读核对 | 已完成初始核对 |
| G1–G3 | CB 完整 INPUT 门槛、X=0 诊断、受控测试与历史影响审计 | 修复及本地回归通过；历史审计/矩阵待记录 |
| G4–G10 | 独立 JIT policy/对象、现有流交接、START 收益和审计字段 | 待实施 |
| G11–G13 | 纯策略、真实 runtime、旧方案回归及无泄漏验收 | 待实施 |
| G14–G15 | CB 八组重跑；V6 START+JIT 与完整 V7 独立实验；比较报告 | 待实施 |

## G0 当前行为核对

| 接口 | 当前行为 | 获批目标 |
|---|---|---|
| `CbSatRecovery::Decide/Accept` | 仅 HasState 即可 q 恢复，随后补 INPUT | HasState 且 HasInput 才可 DIRECT/RELOCATE，否则从零恢复 |
| `CbSatManager::TryMerge` | 有有效日志时 X=0 也可立即合并 | 保留公式/行为，标记紧急压缩，区别无剩余日志 |
| Input policy / transfer kind | 只有 eager/deferred，无预取角色 | 增加独立 jit/PREFETCH_INPUT，旧枚举语义不变 |
| Frequency policy/gate | START 风险加权；ON 原 (delta,n)，先 proposal 后 fault resolve | JIT 独立时机，START 计划固定，不改旧目标或 gate |
| Controller Initialized / epoch / capacity | 初始化仅切 ON；现有 fault/capacity 入口 | 实际提交及完整 fault batch 后评估，容量重试仅限未准入 |
| CheckpointManager | deferred state-only；Quiesce 取消 generation-0 流并清理对象 | 保留可复用 INPUT 流和对象身份，故障时移交所有权 |
| RecoveryController | deferred 无条件检查源路径并新建 INPUT | 统一按目标的 READY/IN_FLIGHT/ABSENT 解析依赖，包括 F3 |
| NetworkTransferEngine | 现有收包量/状态；新流路径估计 | 只读估计现有流剩余时间，不重复注册/预留/抽样 |
| Storage / LocalDelivery | 已有实际对象和本地逻辑交付 | 复用；INPUT 不重复 S，不预留故障前算力 |

只读扫描旧八组共 664 条恢复记录：FFP 和 FA-FFP 的 recompute/relocate 四组均有
task325 `root_ready=true,input_ready=false,DIRECT,resume=22435 WU`。这是四条记录、
一个不同任务，不是四个故障任务。按 G2，修复后必须重跑八组，不能只换审计版本。

旧 X 决策初筛：仅 FFP 两组各有 task64 的一条 X=0，原因 `NO_REMAINING_LOGS`，
同刻为首次分配而非 MERGE_START。不能把这两条记录伪称为紧急压缩覆盖；真正的
X=0 已有日志合并需受控测试证明。

## 执行记录

CB 修复后的本地验证（2026-09-13）：

- `./ns3 build -j 2`：通过；不重新 configure。
- `test_cb_sat_tools.py`：12 项通过。
- `satcompute-cb-sat-policy-test`：89,207 checks 通过。
- `satcompute-cb-sat-runtime-test`：15 cases / 881,296 checks 通过。
- `satcompute-cb-sat-recovery-test`：26 cases / 5,099 checks 通过；
  证据 `/tmp/v7-cb-input-g1-recovery`，缺 INPUT 的已提交 root 改为从零恢复。
- `audit-cb-sat-fixtures.py`：26 fixtures 通过，13 种损坏证据全部拒绝，包含
  INPUT 未就绪与对象身份不匹配。
- `tests/integration/smoke/run-all.sh`：全部通过，包含旧 eager/deferred、恢复、
  frequency、baseline 及 16 组 placement。

尚未执行修复后的正式重跑。离线审计版本与真实仿真执行版本分别记录。

CB 修复执行提交 `b1a41d7d9`（干净工作树）：

- 历史只读影响审计：`output/v7-cbsat-adjustment/20260913-input-audit`，
  四条受影响记录均为 task325，强制八组正式重跑；两个 X=0 均为首次分配无剩余日志。
- `run-cb-sat-matrix.py --stage smoke --jobs 4`：八组与 FA-LRL relocate 重复运行通过；
  `output/cb-sat-v2/20260913T060835526989Z-smoke`。
