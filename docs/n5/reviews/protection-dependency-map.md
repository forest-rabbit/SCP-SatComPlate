# Protection 依赖图与接口审计

基线、范围与最终目录建议见[主报告](Protection-architecture-consolidation-audit.md)。本文件区分编译依赖、实际运行协作和测试入口引用，不将文件名相似视为调用关系。

## 1. 扫描方法与边界

对 Git 跟踪的 `contrib/satcompute`、`docs`、项目入口和 CI 配置作只读扫描：解析 C++ include，解析 Python import AST，扫描脚本/CMake/文档里的字面路径，再人工核对关键动态加载点及执行 owner。没有 import/执行被审计脚本，没有启动仿真。

初始扫描共 471 个可读文本文件，另补 root MILESTONES 与 docs 二进制/数据资产清单。236 项源码/保护目录清单加模块 CMake 成为 237 项源码 manifest；扫描工具位于临时目录，不引入项目新的 audit framework。

静态候选引用共 4959 条：559 include、14 Python import、254 build reference、872 code literal、540 doc reference，以及 2720 条 basename 歧义候选。数字包含同一关系在不同类型/行上的重复，**不是 4959 次实际调用，也不是依赖边的精确总数**。每份 manifest 保留带行号和种类的无歧义候选入边；basename 歧义仅计入 `ambiguous_reference_count`，不混入已解析的调用者列表。通用 `README.md` 等 basename 会产生大量此类候选，不能据其认定 API 依赖或安全删除。

源码清单 `direct_dependencies` 是可解析的 project include/import；`referenced_by` 同时包含 conservative code/build literal 引用。未将系统/ns-3/第三方头复制进清单。Python `__file__`、拼接目录、glob、人工命令和仓库外入口不可能仅凭静态扫描完全排除。因此没有入边也不能自动置 `delete_safe=true`。

另人工解析两个间接加载点：

| 调用位置 | 真实目标 |
| --- | --- |
| `tests/unit/test_backup_feasibility.py:14` 的 `runpy.run_path(TOOL)` | `tools/validation/pre-n5-backup-feasibility/analyze.py`，TOOL 定义于前一行 |
| `tests/unit/test_direct_recovery_deadline.py:6` 的 `spec_from_file_location(...SCRIPT)` | `tests/integration/regression/audit-direct-recovery-deadline.py`，SCRIPT 定义于前一行 |

以下路径无特别说明均相对 `contrib/satcompute/`。

## 2. 当前运行依赖图（不是理想化重画）

```text
para / satcompute CLI + composition root
  ├─ FixedProtectionController → FixedProtectionPolicy → ProtectionRuntime
  │                                                → CheckpointManager
  ├─ FrequencyProtectionController
  │    ├─ FaultPredictionEngine / past-current snapshots
  │    ├─ single CompfrrFrequencyPolicy + FrequencyDecisionGate
  │    ├─ FA reference pair → fixed configuration → N5cPlacementPolicy
  │    ├─ N5cPlacementTracker [history / quota / export DTO]
  │    └─ CheckpointManager [capture / batch / apply / transfer ledger]
  ├─ RecomputeController → RecoveryController → zero-capacity manager ledger
  ├─ OnePlusOneController → ReplicaManager → shared transfer ledger
  └─ CbSatController → CbSatPolicy / CbSatManager / CbSatRecovery

checkpoint / recovery / replica / CB execution
  ├─ TaskCoordinator / ComputeService / attempt / original deadline
  ├─ NetworkTransferEngine ↔ routing + admission / actual network state
  ├─ LocalDelivery [same node; logical bytes, no UDP]
  ├─ BackupStoragePool [objects / reservations / actual occupancy]
  └─ common actual events → metrics + scheme-specific export adapters

fault batch → task/node availability + object invalidation + controller callbacks
            → after complete same-ns batch: valid commit / replica takeover
```

图中共享的是执行合同，不意味着所有方案都调用 `ProtectionRuntime::Dispatch`，也不表示通用资源服务能触发 checkpoint、tail 或 replica。当前 CB 是独立纵向 controller/manager/recovery；main baseline 的能力不能通过依赖图上的公共边扩张。

代表性源码锚点：

| 文件 | 已核对的依赖或职责 |
| --- | --- |
| [runtime/protection-runtime.cc](../../../contrib/satcompute/protection/runtime/protection-runtime.cc) | 支持动作的 mechanism 唯一分派和 policy fallback，职责很窄 |
| [frequency controller](../../../contrib/satcompute/protection/runtime/frequency-protection-controller.cc) | BuildResources、before/after fault epoch、资源回收事件、调用 solver 与 manager |
| [frequency-n5c-adapter.cc](../../../contrib/satcompute/protection/runtime/frequency-n5c-adapter.cc) | controller 成员定义；`ReadyAfter` mode 分支、`N5cPeers`、`SelectN5cRemote` 和 concrete cast |
| [n5c-placement-policy.h](../../../contrib/satcompute/protection/policy/compfrr/placement/n5c-placement-policy.h) | include Frequency header；N5cForecast 携带 FrequencyInput，当前 P 与 F 类型耦合 |
| [checkpoint-manager.cc](../../../contrib/satcompute/protection/mechanism/checkpoint/checkpoint-manager.cc) | 初始化、capture/batch、传输注册/完成、Freeze/Quiesce/retain/release |
| [recovery-controller.cc](../../../contrib/satcompute/protection/runtime/recovery-controller.cc) | direct → optional relocation / recompute，真实 INPUT/state/tail、merge、compute/result |
| [cb-sat-recovery.cc](../../../contrib/satcompute/protection/policy/baseline/checkbullet/cb-sat-recovery.cc) | 自有 q_restore 与 root/log/input 计划；不请求 CompFRR local tail |
| [replica-manager.cc](../../../contrib/satcompute/protection/mechanism/replication/replica-manager.cc) | 一次请求、真实 INPUT/RESULT、fault batch / winner / cleanup |

## 3. Eager/Deferred 的真实依赖矩阵

| 方向 | 当前依赖、位置 | 最小边界与必须保持的语义 |
| --- | --- | --- |
| Frequency → Input | `compfrr-frequency-policy.*` 的 FrequencyInput.inputPolicy，StartWindow / Evaluate 初始化与 replay 成本 | 单 solver 消费同一 InputCostTerms；不为两个 mode 复制优化器 |
| Input → Frequency | 目前没有独立 Input 类反调 solver；mode 枚举在 protection-types | 未来也不允许反调；Input 只返回不可变布局/依赖描述 |
| Frequency controller → Input | BuildResources 的 source→remote replay、primary→remote BASE/state、primary→local、local→remote 路径成本 | Deferred INPUT 是 hard candidate prefilter；Eager OFF 的 replay 成本按原合同处理，不能无差别硬过滤 |
| P adapter / scorer → Input | adapter `ReadyAfter`；`N5cCatchSeconds` 的 Deferred INPUT 与 recoverability 估计 | 用同一 mode 描述，但 scorer 仍只评估已固定配置，不解新 Frequency |
| CheckpointManager → Input | Execute 初始化的 INIT_BASE；StateRecord/committed 字节；零字节身份 | 保留 Eager primary→remote，Deferred 不传 INIT_BASE，不把 source/BASE 所有权换掉 |
| task-state-adapter → Input | `CommittedStateBytes` | Eager 图像剩余 INPUT + 增量状态、LLM KV 与 Deferred 状态布局原式原取整 |
| Recovery → Input | RecoveryController 的 Deferred 完整 INPUT 获取与本地交付依赖；direct/relocate/recompute 各自计划 | 建议 `DescribeRecoveryInput`；可读 bundle 与完整 fetch 分开，不统一所有恢复操作候选 |
| Storage estimator → Input | `MakeFrequencyStorageEstimator` 中 initial input=0/S、committed layout、未来 batch/merge peak | estimator 保持纯函数；物理 pool 只见字节/object；actual 与 peak quota 按 owner 不重复 |
| Metrics → Input | input-staging summary、protection transfer kinds、state/storage 和 recovery records | 只导出 mode 与事实，不反向改变 policy；CSV/JSON 名称和字节范围保持兼容 |
| CLI/config → Input | para 定义、satcompute.cc mode 校验及 controller 构造 | 旧 `inputStagingPolicy=eager/deferred` 与默认值不变；当前 Deferred 限 CompFRR，Fixed 仍用 Eager |
| CB / 1+1 / Recompute → INPUT | 各自完整请求/可读对象合同，不依赖 CompFRR 的 Frequency | 可消费中性 LocalDelivery/bytes 描述，不能继承其 checkpoint state 或 input timing 算法 |

这些模式分支不全是重复代码：布局、分析成本、物理执行是不同责任。只合并相同语义的描述，不因都出现 `DEFERRED` 就合并状态机。

目标依赖方向：

```text
CLI/composition → CompFRR Input implementation
                     ↓ immutable descriptions
               neutral InputContract / cost-layout DTO
                 ↙             ↓                ↘
        single Frequency   storage estimator   recovery/checkpoint executor
                 ↓
       fixed-configuration P adapter / scorer

Input implementation ─╳→ solver / remote selection / RNG / real transfers
generic runtime      ─╳→ CompFRR concrete scorer or input implementation
metrics              ─╳→ policy control flow
```

当前有一处应拆的类型反向耦合：P forecast 用完整 FrequencyInput。后续可变成只读风险/资源/固定配置视图，Frequency adapter 负责转换；此转换不重算或截断 first-failure 权重，不引入未来 realized fault trace。

## 4. 生命周期、能力许可与资源所有权

| Owner | 必须继续拥有/保证的内容 |
| --- | --- |
| scheme controller | START/ON/PAUSE、原有合法事件触发、是否请求某机制；不能由 storage 空闲自动触发保护 |
| checkpoint manager | checkpoint 序列、capture reserve、不可变记录、in-flight 与 merge 保留；resource hold 与 policy PAUSE 分开 |
| transfer dispatcher（待抽） | 同一 ID/注册队列、真实网络 terminal 回调、已发送部分账本；不决定 tail/重传/节点 |
| recovery policy | 根据已有可读对象和 operation-specific feasibility 决定 direct/tail/redo/relocate/recompute；cause-aware 许可 |
| recovery executor | 并发 INPUT/state/tail、cR 与 compute join、原始 deadline、对象保留/释放与结束清理 |
| physical storage pool | 原子 reserve/commit/merge/release；不承诺算法 future quota、不授予某 scheme checkpoint 权限 |
| placement observer / quota | observer 记录实际；quota 是各 owner 的承诺，Frequency 更新替换、终止释放，actual 与 promised peak 取不重复口径 |
| baseline-specific manager | CB 的 H-boundary retry / root-log 连续性；replica 的一次申请/同 ns fault batch/winner；不强行归一 |

特别保留：ON capture/remote batch 不要求 local/remote ComputeService idle；START 与 recovery compute 仍要求健康、available、idle。F1/F2 下可读 storage 与 F3 永久销毁不是同一种状态。peer recoverability 预测中的 availability 检查也不是 ON maintenance 的阻塞条件，不能批量清除。

Policy PAUSE、local-capture blocked、remote-batch blocked 分离。已经开始的 cL、排队/在途对象和 cR 保持现有完成/失败/释放规则；不能丢弃中间序列后从新 WU 起跳。CompFRR terminal 失败流不自动重传；CB 的周期重试是自身合同，不能因抽公共 flow engine 而相互覆盖。

## 5. 测试/分析真实复用链

下图为已经核对字面 loader 的关系，不是依据文件名猜测；完整入边见测试 manifest。

```text
unit/test_checkpoint_maintenance.py
  ├─ audit-checkpoint-maintenance.py
  ├─ run-checkpoint-maintenance.py
  └─ analyze-checkpoint-maintenance.py
       └─ analyze-recovery-u-revalidation.py
            ├─ analyze-recovery-deadline-reruns.py
            │    ├─ analyze-n5c-placement.py
            │    └─ audit-direct-recovery-deadline.py
            └─ analyze-n5c-rational-multirun.py
                 └─ analyze-n5c-rational-u.py
                      └─ analyze-n5c-u-audit.py

placement/frequency audits → analyze-frequency-evaluation.py
                          → analyze-baseline-evaluation.py
                          → analyze-protection-accounting.py

unit/run-cpp-tests.sh → analyze-n5c-placement.py --fixtures
CB cb_tools.py       → regression/run-final-scenario.py
CB audit-cb-sat-run.py → regression/analyze-baseline-evaluation.py
```

因此即使一个 `run-*.py` 只启动历史矩阵，其参数构造和 execution identity 仍可能被 analyzer/unit import。将入口归档前须把被复用部分抽出；若入口路径还公开使用则保留 wrapper。

建议的 helper 提取 manifest（不实际创建）：

| 原文件/函数族 | 未来 reusable helper | 留在原入口的内容 |
| --- | --- | --- |
| analyze-protection-accounting: rows/read/number/close/stats/compare | support/protection/io.py、accounting.py | argparse/历史输出目录选择 |
| analyze-baseline-evaluation: execution_waste/task_execution/physical_network/planned_wait/accounting_gates | accounting.py / baseline_contract.py | 历史 scheme 对照表与具体 run cohort |
| analyze-frequency-evaluation: verify_pair_retries/active_weight/frequency | frequency_contract.py | 某阶段组别和报告生成 |
| analyze-n5c-placement: distribution/near/resources/spatial/recovery_composition | placement_contract.py / comparison.py | 原 CLI `--fixtures` 兼容入口 |
| analyze-n5c-rational-u: service_history/features/winner/audit_actual | history_contract.py | 某一研究 variant 的报告叙述 |
| analyze-n5c-rational-multirun: task_signature/task_network/cohort/recovery_index/three_way_catch | comparison.py | run11–15 矩阵驱动与目录约定 |
| audit-direct-recovery-deadline: direct_estimate/audit/cb_audit | recovery_contract.py | 修复前/后特定路径与历史资料 |
| analyze-checkpoint-maintenance、audit-checkpoint-maintenance: maintenance/prefix/impact checks | maintenance_contract.py | 既有正式矩阵/历史影响清单 |
| run-final-scenario: arguments / scenario identity；CB cb_tools 的公共引用 | scenario.py | 正式仿真启动入口；默认仍不在普通单测中运行 |

提取次序：最低层 IO/账本 → 机制独立断言 → history/placement/frequency → 上层轨迹/paired/去单个最大长尾审计 → 更新 unit、regression、CB 调用者 → 兼容 wrapper → 再判断入口归档。保持 `tests/support/` 位置，不新增第三套测试根。

对每项归档，检查 CMake/test runner、AST imports、runpy/importlib、文档命令、fixture 和工作流引用。动态脚本若仍用相对 `parents[...]` 或 `with_name(...)`，需小型 import/fixture 验证；“无 grep 命中”不是充分 gate。根 CI 目前调用既有 smoke/regression 总入口，其依赖不能因子脚本名字旧而忽略。

## 6. 兼容和证据边界

文件移动不应改 CMake target / 导出 ns3 header 的可用性。新内部名称采用 thin header/type alias 或 adapter 逐步迁入；外部 `n5c`、`rational-U`、CSV 列/文件、错误原因字符串及原分析入口先不变。old golden 不可通过放宽字段比较来迎合 refactor。

CB frozen MTBF JSON 是运行配置而非可删日志；parameter provenance 与历史校准相连。tools 中 shadow reference 及统计 oracle 不移入 production，以免改变因果隔离或独立校验价值。保留历史执行提交身份不等于增加 SHA-256 安全机制；本轮不引入校验哈希，也不改故障随机流。

本图的结构建议仍需 small equivalence gate 实证；本轮依赖扫描完成不等于已允许移动任何节点。branch closeout、canonical 文档提取、RECENT_U 功能撤出各有单独审批条件。
