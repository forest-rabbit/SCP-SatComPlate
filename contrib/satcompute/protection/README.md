# Protection：真实保护、备份与恢复

本模块实现真实 checkpoint、INPUT/state/tail 传输与故障后执行。
CompFRR-F 负责是否保护和频率，CompFRR-P 负责固定配置后的 remote 选择；执行层共用网络、
计算服务和存储账本，Baseline 按自己的能力合同接入。Routing 不属于保护算法开关。

N5R 基于已修复的 n5（PR #102）整理架构，不改当前场景、故障随机流、数学公式、时序或外部指标。
正式 compute-pressure 仅 CUMULATIVE 与 IDLE_AWARE；noU 是消融，recent-U 仅保留历史依赖。
进展与逐阶段等价 gate 见 [N5R 执行记录](../../../docs/n5/reviews/N5R-implementation.md)。

## 阅读入口

| 专题 | 内容 |
| --- | --- |
| [架构与依赖](../../../docs/protection/architecture.md) | canonical 目录、共享与私有能力、旧接口兼容 |
| [CompFRR-F / INPUT](../../../docs/protection/compfrr-f.md) | 唯一 START / (δ,n) solver、Eager/Deferred/Selective、cL/cR、ON maintenance、状态大小 |
| [CompFRR-P](../../../docs/protection/compfrr-p.md) | R/U/M、两种正式 pressure、reference pair、READY、quota |
| [Recovery / Relocation](../../../docs/protection/recovery.md) | 严格故障截止、并行依赖、deadline、LocalDelivery、可选迁移 |
| [Baselines](../../../docs/protection/baselines.md) | Fixed、FFP/LRL/FA、Recompute、1+1 与 CB 边界 |
| [指标与复现](../../../docs/protection/reproducibility.md) | WU/eq-WU、bytes/storage、planned/actual、small gate、历史证据 |

## 代码文件

以下均相对本目录，`.*` 表示同名头文件/实现文件。

| 位置 | 主要职责 |
| --- | --- |
| `protection-para.h/.cc` | 分层 typed 配置与唯一默认赋值；中文分类注释，无 CLI/校验 |
| `protection-config.h/.cc` | CLI 注册、显式来源、capability 校验与既有 wiring 的类型适配 |
| `common/{protection-types,task-state-adapter}.*` | 中性类型、合法进度边界和实际状态字节 |
| `common/{input-contract,checkpoint-evidence,placement-resources,protection-forecast}.*` | INPUT、恢复证据及预测/资源 DTO；不依赖 Frequency |
| `storage/{backup-storage-pool,peak-quota-ledger}.*` | 真实 used/reserved 对象与不重复未来峰值承诺 |
| `runtime/protection-runtime.*` | attempt 与执行资格守卫 |
| `runtime/protection-transfer-{dispatcher,key}.*` | 统一队列、稳定编号/排序和流账本 |
| `runtime/{checkpoint-recovery-port,protection-evidence-view,transfer-only-recovery-ledger}.*` | 中性恢复接口、只读指标接口、无 checkpoint 的 baseline 账本 |
| `runtime/recovery-controller.*`、`checkpoint-recovery-estimate.h` | checkpoint/重算恢复编排与纯 deadline 估计 |
| `runtime/{placement-resource-tracker,placement-load-ledger,compute-usage-history}.*` | 实际资源、活动负载及因果计算历史 |
| `mechanism/checkpoint/{checkpoint-manager,checkpoint-progress}.*` | 捕获/批次、连续序列、对象生命周期 |
| `common/input-dependency.h`、`mechanism/input-staging/*` | 中性只读 INPUT resolver、独立预取对象/流；接受最终恢复节点后才能 handoff |
| `mechanism/relocation/checkpoint-relocation-executor.*` | 获准迁移的预留与发送；不自行选择算法 |
| `mechanism/replication/replica-manager.*` | 真实双 attempt、同批故障后的接管与 RESULT 裁决 |
| `policy/{placement-policy,recovery-policy}.h` | 候选角色与 scheme 授权能力 |
| `policy/fixed/*`、`policy/placement/*` | 固定频率与共享 FFP/LRL/FA placement |
| `policy/compfrr/{compfrr-controller,compfrr-placement-adapter,storage-estimator}.*` | F 接线、P 只读适配、新增存储峰值估计 |
| `policy/compfrr/{frequency,input,placement}/` | F solver、INPUT 成本描述、P 排名及 compute-pressure |
| `baseline/{checkbullet,recompute,one-plus-one}/*` | 完整对比方案；[CB-Sat](baseline/checkbullet/README.md) 的参数和工具也在此 |
| `baseline/multitree/*` | [Multi-tree Published FT Rule](baseline/multitree/README.md)：冻结特征映射、公开 FT 树和共享 RS/RP 编排 |
| `../traffic/local-delivery.*` | 同星逻辑交付，不创建 UDP、不计网络字节 |

旧导出头/分析入口保留兼容；旧参数只在测试层显式转换，普通平台 CLI 不保留别名。
不允许公共 Fixed/Checkpoint/Recovery 反向依赖 Frequency。
不实现完整 MTGP 训练、JIT、网络 ACK 或第二套网络；生产实现不调用 shadow validator。

## 参数与当前可运行范围

外层 `para.cc` 只调用 `GetDefaultProtectionConfig()`。默认值由本目录 `protection-para.cc`
以 `xx = xx;` 集中赋值，CLI/validation 在 `protection-config.cc`，`satcompute.cc` 只接线既有 controller。
层级为 **完整 Scheme → 私有策略 → Variant/Ablation**，不增加完整配置 JSON 或第二套算法。

| 参数 | 默认 | 说明 |
|---|---:|---|
| `protectionScheme` | `compfrr` | 正式默认 CompFRR；另有 recompute / one-plus-one / cb-sat / multitree；off 仅诊断/历史复现 |
| `compfrrCheckpointPolicy` | `adaptive` | `fixed / adaptive`；原 Fixed 下沉为策略，不改变一次性 START 与维护合同 |
| `compfrrPlacementPolicy` | `compfrr` | `ffp / fa-ffp / lrl / fa-lrl / compfrr`；默认使用自己的 CompFRR-P，仅 adaptive 可用 |
| `compfrrInputPolicy` | `selective` | 唯一 INPUT 开关：`eager / deferred / selective`；Selective 固定 SER，Fixed 仅 Eager |
| `compfrrRecoveryPolicy` | `relocate` | `relocate / recompute`；仅切换既有 REMOTE_BUSY、DIRECT_DEADLINE_INFEASIBLE 分支，不重写其他回退 |
| `compfrrPressureModel` | `cumulative` | CompFRR-P 私有：`cumulative / idle-aware`；旧 full / rational-U 的明确映射 |
| `compfrrPlacementAblation` | `none` | `none / noR / noU / noM`；消融仅 cumulative，不是新增 U policy |
| `compfrrFixedDelta` | `0.05` | 仅 Fixed，5% 增量、千分之一精度 |
| `compfrrFixedBatchN` | `4` | 仅 Fixed，n>0 且 n×delta≤1 |
| `backupStorageBytesPerNode` | `10000000000` B | common pool 容量；仅 CompFRR（含 Fixed）/CB 使用，0 允许测试不足；不为 Recompute/1+1 新建池 |
| `compfrr-shadow` / `compfrr-shadow-output` | `false` / 空 | 独立 off+generate 诊断；不启用真实保护 |

上述 `compfrr*` 私有 CLI 仅属于 `protectionScheme=compfrr`；跨方案显式传参即拒绝，即便等于默认值。
未选中的子结构不会创建运行时对象。Adaptive 仍要求 generate 且 F1/F2 至少一个开启；
Fixed 和三个完整 baseline 保留 none/generate/validation-replay 执行能力，不因此开启在线 Frequency。
正式默认变化由用户在纯重构完成后单独确认，不修改算法。Fixed 必须显式选择合法组合，例如
`--compfrrCheckpointPolicy=fixed --compfrrPlacementPolicy=fa-ffp --compfrrInputPolicy=eager`；
不能让 Adaptive 的 P/Selective 默认值扩展 Fixed 能力。历史 runner 显式恢复原默认，不重解释旧实验。

| 完整 baseline | canonical private placement | INPUT/恢复私有合同 |
|---|---|---|
| Recompute | FA-FFP | 故障后获取完整 INPUT，从零重算；无常态 checkpoint |
| 1+1 | FA-FFP | 首次 TASK_RUNNING 一次副本请求；不自动重算或追加副本 |
| CB-Sat | FA-FFP | 完整 INPUT/单节点日志，busy 默认 recompute；无 CompFRR tail |

三种 baseline **仍支持原四种 placement**，它们下沉为各自 typed private config；LRL weight=1
下沉为 commonPlacement 私有值。测试 executable `satcompute-protection-config-driver` 才接受
`testBaselinePlacement`、`testCbSatBusyPolicy`、`testLrlRecoveryWeight`。普通平台不提供这些实验注入开关。
特别注意：**旧 CB 命令省略 busy 时实际为 relocate**，历史转换会显式恢复此身份，不能套用新的
canonical recompute。完整 old→new 对照与小场景证据见[配置层收口](../../../docs/n5/reviews/Protection-config-hierarchy.md)。

测试位置和命令统一见 [tests](../tests/README.md)；平台构建与 uv 环境见[总 README](../../../README.md)。
