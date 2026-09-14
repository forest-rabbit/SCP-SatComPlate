# Protection 架构收口只读审计

审计日期：2026-09-14。源码基线：`fix/checkpoint-maintenance-semantics`，提交 `5fc83fd08c76448fe6380f1c014ee9bb2211a2b3`。本文是 N5R 实施建议，不是实现完成、合并或发布声明。

## 1. 结论与范围

推荐采用 INPUT **方案 B**：Frequency Core 与 Input Staging 平行组织，概念上共同属于 CompFRR-F；只保留一个 START / `(δ,n)` solver。CompFRR-P 只负责既定配置下的 proactive placement，不把真实恢复执行塞入 scorer。Relocation 是可复用、按 policy contract 启用的 optional recovery mechanism；论文中的 deadline-aware relocation safeguard 仍可属于完整 CompFRR-P 方案。

本次全量扫描文件及静态依赖，人工审阅关键决策、checkpoint、恢复和基线路径。在这些检查中没有确认新的算法阻塞项；但静态审计不能替代执行验证，也不宣称所有实现已经证明正确。当前主要问题是职责混合、命名与文档状态滞后，以及历史脚本承载了长期依赖。

本轮只新增任务书要求的六份审计文件：未移动/删除原文件，未改算法、默认参数、INPUT timing、故障随机流或场景；未运行新正式性能矩阵、构建、CI；未提交、推送、变更分支或 PR。用户确认后才进入 N5R。

| 产物 | 内容与范围 |
| --- | --- |
| [源码 manifest](protection-file-manifest.csv) | 237 项：protection 全部 80 项、平台其余非测试 C++ 文件及模块 CMake；含 PUBLIC_SUBSTRATE、逐文件迁移建议和命名出现位置 |
| [依赖图](protection-dependency-map.md) | 真实编译/运行依赖、INPUT 跨层矩阵、脚本复用链及扫描限制 |
| [测试 manifest](tests-cleanup-manifest.csv) | tests 全部 147 项、CB 工具 7 项、相邻 tools Python 11 项 |
| [文档 manifest](docs-cleanup-manifest.csv) | 80 项既有 Markdown/里程碑/docs 证据与上游资产；不把本文等六份新增产物反填为既有文件 |
| [分支收口计划](branch-closeout-plan.md) | 实测 ancestry / PR 状态、整合顺序、需审批的保留/关闭方案 |

CSV 的路径相对仓库根目录；`proposed_path`、`replacement` 是未来建议，不表示目标已经存在。`delete_safe=false` 表示本轮无可直接删除授权或充分证据。清单不强行填满分类：本次没有确认 DELETE_CANDIDATE，也没有仅凭标题判断 DUPLICATE。

## 2. 当前真实分层与重点耦合

当前并不是“所有 policy → 一个 ProtectionRuntime → 所有 mechanism”的统一管线。`ProtectionRuntime` 主要进行动作分派；Fixed、Frequency、CB-Sat、replica 和 recovery 各有事件接入。目录名不能替代调用事实。

| 当前部位 | 实际职责 | 推荐处置 |
| --- | --- | --- |
| common / storage | 公共类型、任务状态字节、物理对象池；其中 mode 与 checkpoint 字段仍混合 | 先分离不可变 contract；物理池只处理合法资源操作，不授予机制能力 |
| CheckpointManager | capture、batch、apply、生命周期，加上保护/恢复流的统一队列及 ID 记账 | 保留 checkpoint owner；先抽中性 transfer dispatcher / ledger，不复制第二个流编号器 |
| FrequencyProtectionController | F/P 适配、预测、资源快照、reference pair、提交、maintenance、指标 | 把纯求解、方案适配、执行请求拆开，保留原事件入口与提交顺序 |
| frequency-n5c-adapter | 实际是 Frequency controller 的成员函数定义，尚非独立接口 | 建立只读 fixed-configuration placement adapter，解除 concrete dynamic_cast 耦合 |
| n5c-placement-tracker | 实际 busy/storage 观测、P quota、研发输出 DTO | 分开 observer / quota owner / export adapter；实际观测不能整体变成 P 私有数据 |
| RecoveryController | 决策、路径与 deadline 判断、INPUT/state/tail 传输、compute/result 和记账 | 分开允许做什么与怎样执行；从零恢复不应依赖完整 checkpoint manager |
| policy/baseline/checkbullet | 完整纵向包：policy、state、controller、manager、recovery、metrics、calibration | 不是纯 policy；保留独立包与能力边界，不强行套 CompFRR 状态机 |

优先拆的是上述边界，不是按文件行数机械切割。Replica / Recompute 当前使用零容量 checkpoint manager 承载公共传输记账，这是执行耦合，不等于它们已经拥有 checkpoint/tail 能力。

## 3. 公共底座与可选能力

源码 manifest 中 `category=PUBLIC_SUBSTRATE` 可直接筛出公共底座及其支撑文件，共 152 项；Frequency/P 专属输出适配另标 `SCHEME_EXPORT_ADAPTER`，混合职责文件不冒充纯公共接口。物理位置仍保持在现有 task / traffic / routing / fault / topology / metrics 等模块，不为目录整齐而全部搬进 protection。

- 公共执行：NetworkTransferEngine、LocalDelivery、ComputeService、TaskCoordinator、attempt / generation / 原始 compute-deadline、完整 fault batch、物理 storage pool、实际 WU / bytes / event 账本。
- 公共观测：合法路径/容量快照、实际正常与恢复占用历史、相同存活 exposure、因果预测输入。预测值与离线真实故障信息严格分开。
- 可选机制：增量 capture、连续序列、merge/apply、batch、tail acquisition、checkpoint relocation、replica 与 takeover。需要 scheme 明确请求，不因共用网络或 storage 自动启用。

| Scheme | capture / 增量 apply | remote batch | local tail 恢复 | checkpoint relocation | replication / takeover |
| --- | --- | --- | --- | --- | --- |
| Recompute | 无 | 无 | 无 | 无状态迁移；故障后选择节点从零重算 | 无 |
| 1+1 | 无 | 无 | 无 | 无 | 首次 TASK_RUNNING 一次性申请；正常副本不免疫；完整 fault batch 后 takeover |
| CB-Sat 主 baseline | 自有 full INPUT / root / 连续 log 与 q_restore | 自有 H 周期，不等于 CompFRR 的 n-batch | 无 | 不自动继承 CompFRR-P；见下方现有 fallback 细则 | 无 |
| CB-Sat+relocate | 同 CB-Sat | 同 CB-Sat | 无 | 显式增强合同；携带本方案 INPUT/root/log，不引入 tail | 无 |
| Fixed + FFP/LRL/FA 组合 | 双层 checkpoint | 固定配置 | 合同允许 | 由 recovery policy 与失败原因共同决定 | 无 |
| CompFRR-F + FFP/LRL/FA 或 CompFRR-P | 双层 checkpoint | 单一 Frequency 求解配置 | 合同允许 | optional 共享执行；P 完整方案可将 deadline-aware relocation 作为 safeguard | 无 |

重要细则：当前 `AllowsCheckpointRelocation` 对 `REMOTE_BUSY`、`DIRECT_DEADLINE_INFEASIBLE` 检查 `remoteBusyRecoveryPolicy`；其他不可用原因沿用现有 fallback。因此 `remoteBusyRecoveryPolicy=recompute` **不是全局禁止 relocation**。CB-Sat 的同名开关也不能被重新解释为“任何原因都只重算”。若要更改此事实，属于另案合同变更，不能混入整理。FFP/LRL/FA 是 placement 组合，不是能力授权器。

检查到的基线接线没有把 CompFRR tail 自动交给 CB-Sat，1+1 也没有隐藏的 checkpoint/recompute 兜底。但共享具体 manager 的结构确有误用风险，后续应通过显式依赖接口与隔离测试固定，而不是现在改变能力集合。Multi-tree 只保留可组合接口方向，不建空实现。

## 4. INPUT 方案 B 与最小接口

目前 `InputStagingPolicy` 是 `EAGER/DEFERRED` 枚举，不是已经存在的策略对象。推荐将纯 mode 语义放在 `policy/compfrr/input/`，与 `frequency/` 平行；公共消费者只见中性 `InputContract` / layout DTO，不能 include Frequency solver 或回调其求解器。

为何不选方案 A：Recovery、CheckpointManager、storage estimator 都实质依赖 INPUT 语义。将其放在 Frequency 内容易让执行器反向依赖求解器；复制 Eager/Deferred solver 更会产生两份数值合同。方案 B 不要求每个 mode 再建一层目录，两个小实现文件足够。

建议最小的三类不可变描述接口（名字是提案，不是本轮新增 API）：

| 接口 | 输入 → 输出 | 消费者 / 禁止事项 |
| --- | --- | --- |
| `DescribeInitialization` | task/layout、当前进度、primary/remote → BASE/state 的逻辑依赖、源节点、payload/storage 字节 | CheckpointManager；描述对象，不启动流、不采样、不求解 Frequency |
| `DescribeCommittedLayout` | task/layout、committed WU → 已承诺对象组成及字节 | state adapter / storage estimator；保持原有整数取整与 merge peak，不双计 INPUT |
| `DescribeRecoveryInput` | 当前可读对象、恢复操作与目标 → 无额外 INPUT / bundled / 完整 INPUT 获取依赖，包含 LocalDelivery | recovery / 只读候选估计；不选 remote，不访问未来真实故障 |

一个小型 cost adapter 根据上述描述和只读路径快照生成 `InputCostTerms`（初始化可就绪时间、replay 路径可用性和 INPUT 成本）。Frequency 与 P 的可行性估计消费这些值；**不能在 input adapter 中另建 START、候选搜索或 `(δ,n)` optimizer**。物理峰值仍由 storage estimator 计算，不让 INPUT policy 预留资源。

迁移时必须保留的非直觉事实：

- Eager 的 `INIT_BASE` 当前是 **primary → remote** 的 S 字节，而不是一概改成原 source → remote。初始化 state 与 cL/merge 的先后不变。
- Deferred 初始化可以拥有零字节 BASE 身份而不建 UDP；恢复时由原 source 获取完整 INPUT。逻辑对象存在不等于网络发送了 S。
- Eager 图像 committed layout 包括尚未处理的 INPUT 部分及状态；LLM 的 committed KV 状态遵循现有公式。不能统一改写成永远 `S + state`，也不能额外创建重复存储对象。
- Eager OFF / Deferred 的 INPUT 路径成本与 hard feasibility 含义不同。Deferred 的 INPUT 路径需在候选过滤中检查，不能仅事后检查选中的节点。
- Recovery 真实 INPUT 与 state/tail 链保持并发及 dependency join。分析模型中的保守成本和 runtime 完成时间表达式不必相等；此次整理不把二者“统一”成另一套公式。
- 同星 source/remote 或 source/recovery 保持 LocalDelivery：零 UDP/网络发送字节，仍有真实逻辑 INPUT 大小、对象生命周期与就绪依赖。

未来 INPUT timing 研究只通过此边界获取预测 timeline / first-failure weights、传输估计和观测账本。离线故障时刻只用于 evaluation；本轮不实现 JIT、prefetch、触发器、新目标或优化器。

## 5. CompFRR-P 与命名

P scorer 内组织为 placement policy + recovery-conflict / storage-pressure 纯函数 + compute-pressure 子模块。小函数可同文件，避免只为目录树建立空壳。长期 U 正式名称为 **Cumulative Historical Compute Pressure**；Rational-U 内部逐步改称 **Idle-Aware Historical Compute Pressure**。

保持现有数学与调用合同：START 用 FA-FFP 只读 reference pair 求一次配置，保留 local；成立后在能兑现该配置的 remote 中排名。不得每个 remote 重跑 Frequency。ON 使用 committed pair 更新，不重新运行 P。不可兑现时沿用 pause/retry，不自动换位。恢复候选搜索仍按 recovery contract，不悄悄换成 P ranking。

idle-aware 使用当前任务 remaining horizon 与 remote 连续空闲时间衰减全历史；原来的全历史正常/恢复 busy 分子与 survival exposure 分母范围一致。不得改变采样/合并区间或为了得到 [0,1] 只在末尾裁剪错误比值。R/U/M 的 max、平局规则、propagation/stable-ID 顺序及诊断 dominant dimension 保持原样。

| 名称/入口 | 分类及处理 |
| --- | --- |
| `N5c*` 类型、tracker/adapter/metrics 文件名、开发期注释 | `RENAME` 内部职责名；导出的 ns3 header/type 先留兼容 include/alias |
| `n5c` CLI、variant 字符串、CSV 文件/字段、fixture 与旧脚本入口 | `KEEP_AS_PUBLIC_TERM` 兼容层；不做全库字符串替换 |
| `rational-U` / `rational_pressure` | 内部使用 Idle-Aware；旧外部名字保留映射，不能静默改列 |
| `recent-U` / `RECENT_U` | 当前仍有生产 scorer / CLI 分支；是否移出 production 单列审批，纯 refactor 先保留 |
| `noR/noU/noM` | 明确保留 ablation capability，不当“历史废码”清除 |
| N5A/B/C、V4/V6/V7/JIT 的历史材料/验证注释 | `ARCHIVE_ONLY`，保留证据；本次扫描未据此确认可删实现 |

逐文件的命中行与原文在源码 manifest 的 `stage_name_occurrences`；`name_classification` 可同时包含内部改名与外部保留，因为同一文件经常同时定义两者。当前 production 没有独立 JIT / exponential-U / dual-U 实现需要迁入；历史 JIT PR 单独保留。

## 6. Runtime A/B/C 分类与目标

| 类别 | 文件（省略 .cc/.h） | 建议 |
| --- | --- | --- |
| A 中性执行/观测/算术 | protection-runtime、protection-transfer-key、decision-path-snapshot、compute-usage-history、placement-load-ledger、checkpoint-recovery-estimate | 留 runtime；estimate 只接收调用者提供的合法时长，无 INPUT mode/选点/准入逻辑；公共快照不得带 P 求解副作用 |
| B CompFRR 适配 | frequency-protection-controller、frequency-n5c-adapter、frequency-storage-estimator | 分别归 CompFRR controller / placement adapter / estimator |
| A+B 混合 | recovery-controller | 共享 executor 留 runtime；checkpoint 选择/许可归 policy，relocation 执行 primitive 可独立 |
| A+B+C 混合 | n5c-placement-tracker | 观测留 runtime，quota 归 placement，研发输出 DTO 交 metrics adapter；无整文件删除结论 |
| 其他 scheme adapter | fixed-protection-controller、one-plus-one-controller、recompute-controller | 归所属 policy 的 adapter；不能误标为 CompFRR B 类 |

没有识别出可直接丢弃的独立 C 类 runtime 文件。研究阶段命名不证明它只服务历史分析。

## 7. Recommended Tree

以下均为未来位置。省略未变的外部 task/traffic/routing/fault/metrics；现有测试位置保持不变。

```text
protection/
├── common/
│   ├── protection-types.*          # 兼容入口，逐步薄化
│   ├── input-contract.h            # 中性描述 DTO；无 solver
│   └── task-state-adapter.*
├── mechanism/
│   ├── checkpoint/                 # capture/batch/apply 与生命周期
│   ├── replication/                # 现有一次性副本与 takeover
│   └── relocation/                 # 仅抽已有可复用执行，不新增策略
├── policy/
│   ├── compfrr/
│   │   ├── compfrr-controller.*
│   │   ├── compfrr-placement-adapter.*
│   │   ├── storage-estimator.*
│   │   ├── frequency/              # 唯一 solver + decision gate
│   │   ├── input/                  # Eager / Deferred 纯描述实现
│   │   └── placement/
│   │       ├── compfrr-placement-policy.*
│   │       └── compute-pressure/   # cumulative / idle-aware；兼容 recent
│   ├── fixed/                      # policy + adapter
│   ├── baseline/                   # FFP/LRL/FA、Recompute、1+1 policy/adapter
│   ├── recovery/                   # 可复用 checkpoint 决策；能力由各 scheme 授权
│   ├── placement-policy.h
│   └── recovery-policy.h           # 许可，不执行传输
├── baseline/checkbullet/           # 独立纵向包，不冒充纯 policy
├── runtime/
│   ├── protection-runtime.*
│   ├── protection-transfer-dispatcher.*
│   ├── recovery-controller.*       # 共享执行协调
│   ├── placement-resource-tracker.*
│   ├── compute-usage-history.*
│   └── ...                        # ledger / key / readonly path snapshot
├── storage/backup-storage-pool.*
└── README.md
```

目录拆分只在有明确 owner/interface 的提交中进行；不要求每项能力都立即形成单独类。CB 纵向包的整体搬迁可以排在最后，先做好路径兼容。

## 8. Alternative Tree

更保守的备选同样采用 INPUT B，只降低物理移动规模，不回退为两份 solver：

```text
protection/
├── common/                        # neutral INPUT contract
├── mechanism/{checkpoint,replication}/
├── policy/
│   ├── compfrr/{frequency,input,placement}/
│   ├── baseline/checkbullet/      # 原位置，明确标注纵向包例外
│   └── fixed/
├── runtime/
│   ├── compfrr/                   # 集中 scheme adapters，公共 runtime 不引用它
│   ├── recovery-controller.*      # 接受纯决策/能力依赖
│   ├── relocation-executor.*      # 不强制独立 mechanism 目录
│   └── ...                        # 中性 observer/transfer/storage 接口
└── storage/
```

推荐树职责可读性更好；备选树更少触碰导出头、CB 工具相对路径与历史入口。可以先完成接口拆分，通过 gate 后再决定 CB 整包是否移动，不能把搬迁本身当验收成果。

## 9. 测试与文档清理

测试 manifest 每项均列分类、原因、替代物、调用者和前置 gate。长期 unit / contract、smoke、小型 fixtures 保留原位置。历史矩阵入口可标 `ARCHIVE_N5_EVIDENCE`，但只代表最终用途；存在调用者时必须先抽 helper。当前没有 `delete_safe=true`。

优先将 accounting、frequency decision audit、placement/history、maintenance/recovery equivalence 等纯函数抽至 `tests/support/protection/`；`run-final-scenario.py` 的冻结 argv / 运行身份构造也被 CB 和多个工具使用。旧路径先薄 wrapper，不改默认参数和输出格式。独立数学 oracle 仍需独立，不能为“复用”而直接调用生产 solver 再比较自身。详见依赖图中的调用链。

后续 canonical 内容分工如下；本轮不创建这些正文，不增加新的历史说明页：

| 唯一入口 | 应承载内容 |
| --- | --- |
| protection/README.md | 模块介绍、目录/文件职责、公开参数入口；链接下列专题 |
| docs/protection/architecture.md | 公共底座、可选能力矩阵与依赖方向 |
| docs/protection/compfrr-f.md | Frequency + Eager/Deferred 模型、START/ON 合同 |
| docs/protection/compfrr-p.md | placement、U 命名/公式、ablation 和兼容参数 |
| docs/protection/recovery.md | readiness、INPUT/state/tail 并发、deadline/fallback 与 lifecycle |
| docs/protection/baselines.md | Recompute / 1+1 / CB-Sat 与 Fixed 能力隔离；链接 CB 参数来源 |
| docs/protection/reproducibility.md | 实验身份、实际/计划账本、small gate；长测试指令仍归 tests/README |

既有 `docs/n5/reviews/` 优先原位保留为 historical evidence，避免断链。新 canonical 只提取现行合同，不将历史结论改成新代码结果。MILESTONES 保持短小。`docs/README.md` 仍称 CB-Sat 未合入 n5，已与 PR #98 合并事实不符，应在实施期修正；本轮记录，不顺手修改。CB MTBF profile / parameter provenance、场景与 rho/H 来源、F2 图及对应数据均保留。

## 10. N5R 提交顺序与语义不变 gate

先按分支计划获得用户批准的 corrected baseline；不得从缺少恢复和 maintenance 修复的旧 n5 直接整理。建议逐步提交，每步检查 build、unit、contract、小型 regression；文档提交复核已有证据和链接即可，不无意义重跑性能测试。

1. `refactor(protection): freeze common substrate interfaces`：固定现状 small golden/兼容 API；抽 transfer ledger 的中性契约，保留 ID/queue owner。
2. `refactor(compfrr-f): separate frequency core and input staging`：方案 B 与单 solver；消除重复 ready/layout 描述，但不变公式或整数取整。
3. `refactor(compfrr-p): consolidate placement and compute-pressure policies`：正式内部名称、observer/quota/export 分离；保留所有外部入口与 ablation。
4. `refactor(recovery): isolate reusable recovery/relocation execution primitives`：从零与 checkpoint 执行分界，维持 operation-specific 候选与并发。
5. `refactor(baselines): enforce explicit mechanism capability boundaries`：CB / replica / recompute 接中性执行依赖，隔离 tests；是否搬 CB 包最后决定。
6. `test(protection): consolidate long-lived contract/regression suites`：纯 helper → 更新调用者 → 原入口 wrapper → 再审归档；不能反向删除依赖。
7. `docs(protection): establish canonical architecture entries`：提取现行内容、压缩原 README、修正文档状态，不重写历史证据。
8. `chore(n5): archive approved development entry points`：只有审批通过、调用扫描归零或兼容入口验证后才归档；分支/PR 清理另行执行。

基于现有 tests/unit、support、smoke 和 4-task / 16-node 小 fixture 设计 gate，不运行完整 1300 s 正式场景：

| Gate | 必须证明 |
| --- | --- |
| G0 公共底座 | 原始 deadline、LocalDelivery、真实已发/失败部分 bytes、actual WU、同纳秒完整 fault batch、quiescence 无变化 |
| G1 F / INPUT | Eager、Deferred + FA-FFP，Fixed，以及 audit 开/关不干预随机流；一次 reference solve、START/ON、失败准入/路径过滤一致 |
| G2 P | Cumulative、Idle-Aware 与 noR/noU/noM 单元 fixture；remaining horizon、exposure、quota replacement、不重复 actual/peak 和 tie-break 完全一致；RECENT_U 兼容测试保留 |
| G3 maintenance / recovery | ON 不依赖 ComputeService idle/availability；START 与 recovery compute 仍要求可用/空闲；policy PAUSE、capture hold、batch hold 分账；连续序列/cL/cR/F3/路径早退/deadline relocation 一致 |
| G4 baselines | CB 主版及显式 relocate 增强、1+1、Recompute 的现有 contract 与 smoke；无新增 tail、retry、takeover immunity 或隐藏兜底 |
| G5 artifacts | 决策、事件、flow/object ID、CSV/JSON schema、任务结果、actual/planned WU/等待、物理 bytes、quota/storage 轨迹与释放完全等价 |

已有入口包括 `tests/unit/run-cpp-tests.sh`、相关 Python unit、`run-frequency-smoke.py`、`run-protection-smoke.py`、`run-recovery-smoke.py`、`run-baseline-smoke.py`、`run-placement-baseline-smoke.py`、`run-cb-sat-smoke.py`。CB smoke 需显式纳入，不能以总入口名字推断它已运行。P 的两种 U 优先复用已有 placement/frequency unit；如需 end-to-end goldens，只在现有小 fixture 加两个代表性配置。

使用 corrected baseline 的同语义输入生成/保留 small goldens。仅允许规范化输出目录、墙钟耗时、源码 execution identity 等非语义字段；不忽略时间 ns、决策顺序、stream/对象 ID、浮点求和顺序导致的决策差异、WU 或字节差异。旧正式结果是否继续有效，要验证 **maintenance trajectory + 资源账本等价**，不能只看 completion 或 pause 后是否故障。本轮不宣称任何旧矩阵已通过新一轮等价验证。

## 11. Migration Risks 与待审批事项

- 抽 transfer queue 可能改变全局 ID 分配及同纳秒 callback 顺序；先委托同一 owner，再搬实现，不能双队列并行注册。
- checkpoint / recovery / replica / CB 对 storage 对象的保留、终止和 F3 销毁时刻不同；接口不能替代 owner，不能移交裸指针后重复释放。
- 名称导出包含 ns3 头路径、CMake 目标、CLI/CSV/fixtures 与 Python `__file__` 相对路径；整体替换会破坏历史可复现入口。
- INPUT 布局、成本和 runtime 并发不等同；抽象中不能改变 source 选择、LocalDelivery、取整或 peak accounting。
- 检查到的 availability 条件不都属于 ON maintenance：START、恢复、预测依赖仍有合法用途，不得批量删除。
- helper 内历史 run 固定参数/身份检查不宜进入 production；反过来，共享 oracle 不能使测试失去独立性。
- 分支都是待整合链的一部分；先合入修复再清理分支，不能将未整合分支标为无用并删除。

用户只需在审计后确认是否进入 N5R，以及采用推荐树还是保守物理布局。RECENT_U 撤出 production、外部字段去兼容、JIT/新 INPUT 研究、正式矩阵刷新与历史分支删除均不包含在这次默认批准范围内。

## 12. 本轮产物验证

已验证三份 CSV 的列结构、分类、路径存在性、去重与既定 Git 跟踪范围的完整覆盖；六份产物的本地 Markdown 链接、空白和命名出现行覆盖通过检查。对清单内 84 个 Python 文件只作 AST 解析，没有加载执行。HEAD、分支、既有已跟踪文件均未改变，工作区仅新增这六份审计产物。此结果是文档/静态扫描验证，不是 N5R 的 build、机制等价或性能验收。
