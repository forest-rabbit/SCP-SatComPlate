# Protection 架构与职责

本页定义现行实现的依赖边界；参数入口见[模块 README](../../contrib/satcompute/protection/README.md)。
N5R 从 corrected n5（PR #102）纯重构，不代表新的算法或性能实验。

```text
Protection Scheme -> Placement Policy -> Recovery Policy -> Shared Runtime
                                                           |
                                                     Routing / Traffic
```

Routing 始终是公共基础设施，不是任何保护算法的开关。方案选择决策与能力，执行层维护真实事件、对象和账本。
共用候选生成/可行性查询不要求不同操作拥有相同的最终候选集。

## 目录与依赖

```text
protection/
├── common/                       中性状态、INPUT、预测 DTO 与 evidence
├── storage/                      BackupStoragePool / PeakQuotaLedger
├── runtime/                      dispatch、attempt、恢复接口、资源观察
├── mechanism/
│   ├── checkpoint/               CheckpointManager / 连续进度
│   ├── relocation/               可选 checkpoint 迁移执行器
│   └── replication/              1+1 真实双 attempt 执行
├── policy/
│   ├── fixed/                    固定 delta/n 与接线
│   ├── compfrr/
│   │   ├── frequency/            唯一 START / (delta,n) solver
│   │   ├── input/                INPUT 成本适配（方案 B）
│   │   └── placement/            CompFRR-P / 两种 compute-pressure
│   └── placement/                共享 FFP/LRL/FA 候选与排序策略
└── baseline/                     完整独立对比方案
    ├── checkbullet/              CB-Sat 代码、参数、标定来源与工具
    ├── recompute/                无常态保护的从零重算
    ├── one-plus-one/             一次副本申请与双 attempt
    └── multitree/                仅未实现说明，无 CLI/源码/结果
```

- **CompFRR-F / INPUT B**：Frequency Core 与 Input Staging 平行；两种 INPUT 策略只共用一套 solver。
  `common/input-contract.*` 定义初始化、布局、恢复依赖；`policy/compfrr/input/` 将其映射为成本。
  Checkpoint、Recovery、Fixed 不反向依赖 Frequency；F 的 controller 负责接线。
- **CompFRR-P**：消费中性的 `PlacementForecastInput` / `PlacementResourceSnapshot`，
  不接收 solver 或 storage callback，不为候选重新求解 Frequency。
  `PlacementResourceTracker` 只观察实际服务/资源；P tracker 保存提案，`PeakQuotaLedger` 管未来承诺。
  资源指标与 P 评分指标分开输出，不能用提案替代实际账本。
- **公共执行**：`ProtectionTransferDispatcher` 独占 canonical queue、单调 flow ID 和流账本。
  `CheckpointRecoveryPort` 提供 checkpoint/恢复访问，`ProtectionEvidenceView` 仅供指标读取。
  `CheckpointManager` 保留对象生命周期；relocation executor 只执行获准的预留/传输。
- **Baseline 边界**：Recompute/1+1 使用 `TransferOnlyRecoveryLedger`，不实例化空 checkpoint executor。
  CB-Sat 有独立 MTBF/H/X、单节点状态和恢复合同，不继承 CompFRR tail/Frequency。

| 方案 | 正常保护执行 | 恢复能力的归属 |
| --- | --- | --- |
| Fixed / CompFRR | 双层 checkpoint | policy 显式授予 checkpoint / local-tail / relocation 能力；共享执行器 |
| Recompute | 无 | 原始 INPUT + 从零执行；不授予 checkpoint/tail/relocation |
| 1+1 | 一次资源约束副本申请 | replica 自身接管；无隐藏 Recompute |
| CB-Sat 主 baseline | 单节点完整 INPUT + root/log | 独立恢复，忙时 Recompute；不继承双层 tail |
| CB-Sat + relocate | 同上 | CB 显式扩展迁移完整 INPUT/root/log，不改变主 baseline |

Relocation 是可复用的 optional mechanism，不是代码层 CompFRR-P 私有能力；
完整方案可以将 deadline-aware relocation 描述为 CompFRR-P 的 safeguard。
START、ON maintenance、recovery compute 各自的 availability/idle 合同见 [F](compfrr-f.md) 与[恢复](recovery.md)。

## 兼容边界

旧 `frequency-protection-controller.h`、`n5c-placement-*.h`、baseline 导出头保留转发/类型别名；
旧 CLI `placementMode=n5c`、`n5cVariant=full/rational-U` 和 CSV 名字不是第二套实现。
production U 仅 CUMULATIVE / IDLE_AWARE，noR/noU/noM 是消融，recent-U 正式 CLI 已拒绝；
历史 API/fixtures/分析证据按真实依赖保留，见[复现与归档](reproducibility.md)。

CB 的 C++、参数 profile、来源与工具统一由 `baseline/checkbullet/` 拥有，旧整套目录撤销。
公开 `ns3/cb-sat-*.h` 名字通过 CMake 的 canonical header 导出保持兼容；不另造一套转发头目录。
冻结 profile 与 provenance 逐字节保留，不重新标定 MTBF。
生产代码不依赖 shadow validator，单元测试可以单向引用其旧布局 oracle；
不增加完整插件框架，Multi-tree 只有说明占位，未实现。
