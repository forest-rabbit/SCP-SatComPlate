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
| [CompFRR-F / INPUT](../../../docs/protection/compfrr-f.md) | 唯一 START / (δ,n) solver、Eager/Deferred、cL/cR、ON maintenance、状态大小 |
| [CompFRR-P](../../../docs/protection/compfrr-p.md) | R/U/M、两种正式 pressure、reference pair、READY、quota |
| [Recovery / Relocation](../../../docs/protection/recovery.md) | 严格故障截止、并行依赖、deadline、LocalDelivery、可选迁移 |
| [Baselines](../../../docs/protection/baselines.md) | Fixed、FFP/LRL/FA、Recompute、1+1 与 CB 边界 |
| [指标与复现](../../../docs/protection/reproducibility.md) | WU/eq-WU、bytes/storage、planned/actual、small gate、历史证据 |

## 代码文件

以下均相对本目录，`.*` 表示同名头文件/实现文件。

| 位置 | 主要职责 |
| --- | --- |
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
| `baseline/multitree/README.md` | 未来方案占位，当前未实现，无运行入口 |
| `../traffic/local-delivery.*` | 同星逻辑交付，不创建 UDP、不计网络字节 |

旧导出头和历史 CLI/分析入口仅兼容转发；不允许公共 Fixed/Checkpoint/Recovery 反向依赖 Frequency。
不实现 Multi-tree、JIT、网络 ACK 或第二套网络；生产实现不调用 shadow validator。

## 参数与当前可运行范围

参数在外层 `para.h/.cc`，CLI 注册/校验在 `satcompute.cc`，不增加完整配置 JSON。

| 参数 | 默认 | 说明 |
|---|---:|---|
| `protectionMode` | `off` | `recompute` 完整从零重算；`one-plus-one` 真实热副本；`fixed` 固定检查点；`compfrr` 动态频率；`checkbullet` 单备份星完整 INPUT/日志；仅 compfrr 要求 generate 且启用 F1/F2 至少一个来源；保护模式均要求网络任务、shadow 关闭 |
| `backupStorageBytesPerNode` | `10000000000` B | 十进制 10 GB；仅为实验容量，可覆盖，0 可用于存储不足测试 |
| `fixedProtectionDelta` | `0.05` | 5% 增量；千分之一精度，转换后传入纯策略 |
| `fixedProtectionBatchN` | `4` | 4 个连续有效 L1 一批，要求 n>0 且 n×delta≤1 |
| `placementMode` | `fa-ffp` | `ffp/lrl` 最小筛选；`fa-ffp/fa-lrl` 可行性感知筛选，五种保护模式均可注入；`n5c` 仅用于 CompFRR |
| `n5cVariant` | `full` | `full`=CUMULATIVE、`rational-U`=IDLE_AWARE；`noR/noU/noM` 仅消融；recent-U 正式入口拒绝 |
| `remoteBusyRecoveryPolicy` | `relocate` | fixed/compfrr 的 REMOTE_BUSY、DIRECT_DEADLINE_INFEASIBLE 分支：迁移 checkpoint 或从零重算；CB 保持既有忙时合同；off/recompute/one-plus-one 不使用此开关 |
| `inputStagingPolicy` | `eager` | `eager` 保持旧预置行为；显式 `deferred` 仅支持 compfrr，常态只保护状态、故障后获取一次完整原始 INPUT |
| `inputAdmissionPolicy` | `none` | `none` 保持原合同；唯一 selective 规则 `ser-break-even` 仅用于 CompFRR + deferred，一次性选择独立 INPUT 预置，不改 checkpoint 布局；见 [F/INPUT](../../../docs/protection/compfrr-f.md#独立-input-binary-admission) |
| `lrlRecoveryWeight` | `1` | G3 正式运行前冻结，不扫描或事后选择；不影响 FFP |

测试位置和命令统一见 [tests](../tests/README.md)；平台构建与 uv 环境见[总 README](../../../README.md)。
