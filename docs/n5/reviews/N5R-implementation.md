# N5R Protection Architecture Consolidation

执行依据：[已批准只读审计](Protection-architecture-consolidation-audit.md)。此页仅记录分步提交、边界与 gate，不重写历史结果。

## 冻结范围

- 唯一基线：PR #102 的 merge commit `d26f7af907731c9a219d973add0f18e0b87f8778`，tree 与 corrected head `17c629a1d` 完全一致。包含 #100/#101、deadline/INPUT-path recovery、checkpoint-maintenance 修复。
- 单一实施分支：`refactor/protection-architecture-consolidation`。main、legacy、JIT/V7 和既有正式实验不变。
- 顺序：公共底座 → F/INPUT B → P → recovery/relocation → baselines → tests → canonical docs → 历史归档。
- 每步 build、unit、contract、小型 deterministic equivalence 后提交。遇到非授权语义变化立即停步单独审计，禁止刷新 golden 掩盖差异。
- 用户补充批准：production compute-pressure 仅 CUMULATIVE / IDLE_AWARE；noU 仅 ablation；recent-U 撤出正式 CLI，历史和被依赖实现先保留。此唯一显式入口变化单列测试，不视作纯重构等价例外之外的普遍放宽。
- 不跑正式 1300s 矩阵，不改 Frequency/U 数学、RNG、routing、INPUT timing、默认场景或外部 CSV。CI 沿用阶段门禁，不逐提交触发。

## 阶段记录

| 步骤 | 状态 / 依赖边界 | 验证 |
| --- | --- | --- |
| corrected-chain 整合 | #102 已 merge 入 n5；完整祖先和 tree 一致；#100 自动 merged，#101 superseded closed；4 个旧本地/3 个远端分支已清理，历史在 n5 可恢复 | build PASS；Python 185（skip 1）PASS；C++ 合同入口 PASS；smoke 总入口及 CB 8 个小组合+repeat PASS |
| 1 公共底座 | `fdc144ba6`：中性 dispatcher owns 单一 canonical queue / ID / flow ledger；checkpoint 保留 storage/hold/生命周期和原回调；兼容 Flows/QueueRecovery | build、Python 190（skip 1）、C++ 合同、16 组 placement smoke PASS；small equivalence 1965 文件（1707 CSV）完全一致 |
| 2 F/INPUT B | 中性 InputContract 描述初始化/布局/恢复 INPUT（含 LocalDelivery）；F/input 只提供成本描述、一套 solver；事件适配器与 storage estimator 归入 F；旧头/type alias 兼容 | build、Python 190（skip 1）、C++ 合同 PASS（新增 56 项 INPUT contract 断言）；small equivalence 1965 文件（1707 CSV）完全一致；公共 checkpoint/recovery/Fixed 无 Frequency include |
| 3 P | CompFrrPlacementPolicy 与两种正式 pressure；中性 forecast 不携带 solver/storage callback；PlacementResourceTracker、PeakQuotaLedger 与 P decision journal/export 分离；旧类型/CLI/CSV 保持兼容；recent-U 仅历史 API | build、Python 191（skip 1）、C++ 合同 PASS；P 8574 断言；recent-U 正式 CLI 拒绝测试 PASS；small equivalence 1965 文件（1707 CSV）完全一致 |
| 4 recovery/relocation | RecoveryController 依赖中性 CheckpointRecoveryPort，metrics 只依赖 EvidenceView；原主任务快照/事件构造共用；relocation 预留和发送是可选公共机制，tail 必须由调用者明确提供 | build、Python 191（skip 1）、C++ 合同 PASS；新增 7 项 reservation/owner cleanup/发送顺序合同；small equivalence 1965 文件（1707 CSV）完全一致，含 direct/deadline/INPUT path/parallel/LocalDelivery/same-ns |
| 5 baselines | Recompute/1+1 使用 TransferOnlyRecoveryLedger，无 checkpoint executor；scheme 明确授予 recovery 能力；Fixed/Recompute/1+1 adapters 归各 policy；CB C++ 独立 baseline/checkbullet，MTBF/工具/导出头原路径兼容；历史 source guard 同时保护新位置 | build、Python 191（skip 1）、C++ 合同 PASS；small equivalence 1965 文件（1707 CSV）完全一致；CB 主版/relocate、1+1 同 ns fault batch、Recompute 从零与零容量账本均保持 |
| 6 tests | 待执行：helper→调用者→兼容入口 | 单元与历史入口 import/fixture gate |
| 7 canonical docs | 待执行：唯一内容归属，不覆盖历史证据 | 链接/目录/CLI 合同 |
| 8 历史归档 | 待执行：依赖扫描后决定，不按阶段名删除 | compatibility/coverage 清单 |

## 小型等价证据

`tests/integration/regression/run-protection-equivalence.py` 只运行已有 C++ fixture 和 4-task/16-node/15s CLI 场景。覆盖 Frequency、recovery（含 failure/path/deadline/maintenance）、Recompute、1+1、CB、自有 F Eager/Deferred 与 P Cumulative/Idle-Aware。

基线目录 `output/n5r-equivalence/corrected-baseline/`，各步使用新的输出目录，禁止覆盖。全部 CSV 的 schema/顺序/值逐字节相等；JSON 仅规范化输出位置及 wall_clock_ns/wall_clock_s，保留所有语义字段。实际 faults、WU、bytes、storage 和路径差异均失败。日志与退出状态另行检查，不使用 SHA-256。

这里的 gate 不宣称刷新或替代历史正式性能矩阵；N6/N7 再做正式实验。
