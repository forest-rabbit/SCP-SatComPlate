# INPUT 仓库收口

## 范围与起点

本轮只统一参数、保留生产测试并清理研究工具，不改 SER、Frequency、Placement、
Recovery、故障随机流或正式场景；不重跑 1300 s 性能矩阵。默认 `inputPolicy=eager`。

起点：`feature/compfrr-input-admission-runtime@461d76713`，工作区干净。
`n5@aa7a49a1c`；worthiness=`34177d0cf` 是 runtime 的祖先。
CB-Sat=`9a2f029cf` 已经由 #98 整合进入 n5；JIT=#99（base CB-Sat）尚未合并，
head=`2ff3c5c98` 有 5 个独有提交，关闭前须制作并验证 Git bundle。
main=`009788ca9`、legacy/ns-3.33=`f4c7bff66` 保持不变。

## 分阶段验收

1. 固定仓库内 SER fixture：405 个网络候选逐任务结果，68 SEND；另 4 个 LocalDelivery。
2. 单一 INPUT 参数与最小生产因果快照，验证三模式旧新语义等价。
3. 按 import/CMake/调用关系删除离线工具，保留通用 lifecycle/accounting validator。
4. Canonical 文档收口，完整 build/Python/C++/smoke/regression。
5. 推送并创建到 n5 的 PR；最终提交一次阶段 CI，通过后 merge commit 整合。
6. 验证归档/祖先后关闭 #99、依次删除 JIT、CB-Sat、worthiness、runtime 分支。

主动 INPUT 的真实流生命周期、receiver join、USED、合法 refetch 与实际字节账本必须
完全保持。只移除旧 CLI 和开发用 `input-start-snapshots.json`，保留生产 CSV schema。
大快照的数据采集位置仍为 actual pair 固定且 post-batch revalidation 成功之后、
START_CHECKPOINT 执行之前。清理不能删除 SER 实际依赖的因果输入。

## 删除清单与保留边界

删除前已扫描全库 import/runpy/CMake/shell 引用：离线栈外只剩 CMake probe target、
架构测试 forwarding manifest 和文档引用；维护账本已先脱离旧 runner/快照依赖。
以下 31 个文件是一次性 INPUT/JIT 分析闭包，不涉及其他正式矩阵 runner：

```text
contrib/satcompute/tests/integration/regression/analyze-input-break-even-timebase.py
contrib/satcompute/tests/integration/regression/analyze-input-causal-bound.py
contrib/satcompute/tests/integration/regression/analyze-input-coinitialization.py
contrib/satcompute/tests/integration/regression/analyze-input-criticalpath.py
contrib/satcompute/tests/integration/regression/analyze-input-latency-resource.py
contrib/satcompute/tests/integration/regression/analyze-input-partial-predictability.py
contrib/satcompute/tests/integration/regression/analyze-input-start-trace.py
contrib/satcompute/tests/integration/regression/analyze-selective-input-staging.py
contrib/satcompute/tests/integration/regression/analyze-v7-offline.py
contrib/satcompute/tests/integration/regression/run-input-admission-development.py
contrib/satcompute/tests/integration/regression/run-input-start-calibration.py
contrib/satcompute/tests/support/protection/checkpoint-bound-witness.h
contrib/satcompute/tests/support/protection/input-transfer-estimate.cc
contrib/satcompute/tests/support/protection/input_break_even_timebase_audit.py
contrib/satcompute/tests/support/protection/input_causal_bound_audit.py
contrib/satcompute/tests/support/protection/input_coinitialization_audit.py
contrib/satcompute/tests/support/protection/input_criticalpath_audit.py
contrib/satcompute/tests/support/protection/input_latency_resource_audit.py
contrib/satcompute/tests/support/protection/input_partial_predictability_audit.py
contrib/satcompute/tests/support/protection/input_start_trace_audit.py
contrib/satcompute/tests/support/protection/jit_offline_audit.py
contrib/satcompute/tests/support/protection/selective_input_offline_audit.py
contrib/satcompute/tests/unit/test_input_break_even_timebase_audit.py
contrib/satcompute/tests/unit/test_input_causal_bound_audit.py
contrib/satcompute/tests/unit/test_input_coinitialization_audit.py
contrib/satcompute/tests/unit/test_input_criticalpath_audit.py
contrib/satcompute/tests/unit/test_input_latency_resource_audit.py
contrib/satcompute/tests/unit/test_input_partial_predictability_audit.py
contrib/satcompute/tests/unit/test_input_start_trace_audit.py
contrib/satcompute/tests/unit/test_jit_offline_audit.py
contrib/satcompute/tests/unit/test_selective_input_offline_audit.py
```

保留：SER pure selector、causal DTO、InputDependency、InputStagingManager、真实传输和恢复；
`input-admission-test.cc`、`input-staging-runtime-test.cc`、frequency/recovery tests、tracked
SER fixture、`input_admission_runtime_audit.py` 及其测试。旧 U/recovery/baseline 分析工具不按名称删除。
ignored output 本轮不物理删除，避免破坏审计可复现性；最终 D/S 与 CB/N5C 正式结果均保留。
历史工具可由前一提交 `d0ad381e5` 恢复。
另删除 863 行过程报告 `CompFRR-selective-input-staging-offline-audit.md`，核心取舍与
source/run identity 已并入最终 INPUT 报告的 Historical INPUT Design Decisions。
删除 G1 大快照 exporter/旧 DTO 的三文件已由最小 `selective-input-snapshot.*` 替代。

## 状态

G0 已重新核验：7 个远端分支与任务书一致，唯一 open PR 为 #99。
G1a：已提取 tracked causal fixture（409 行），6 项纯 SER 测试通过；405 个网络候选
逐任务完全匹配（68 SEND），4 个 LocalDelivery。测试不再因 ignored output 缺失而跳过。
旧三模式小场景输出已保留在 `output/input-repo-closeout/before-mapping/`。
G1b：完整构建通过；17 项 INPUT Python tests、12 项场景测试（1 项既有切片缺失跳过）、
22 个 INPUT lifecycle 场景（374 checks）通过。Eager/Deferred/Selective 对应旧组合的
33/34/37 份小场景输出完全一致；SELECTIVE 只删除开发大快照。
完整 corrected-chain 小型门禁：1,984 份 CSV/JSON（1,723 CSV）相同，唯一授权删除为
旧 SER fixture 的 `input-start-snapshots.json`；生产 CSV schema 与值均不变。
因果 snapshot 现位于 `policy/compfrr/input/selective-input-snapshot.*`，只含生产实际使用量。
维护账本 validator 已脱离大快照与旧 runner，改用真实 checkpoint START 和 committed
Frequency actual pair/time 交叉验证；物理准入与概率窗口继续由 C++ 接线测试检查。
G2/G3：历史闭包清理、三模式 canonical 文档和历史决策摘要已完成。
维护 Python 为 217 项（1 项既有 native slices 缺失跳过）；删除的是离线模型实验测试，
SER anchor 不再跳过。完整 C++、frequency smoke、16 组 placement smoke 均通过。
对历史 15 组 U source invocation 的只读核对通过（未运行矩阵）；通用 reader 只将确定等价的
旧 INPUT 参数映射为新模式，冲突/重复/未知旧 selector 必须拒绝，原 source guards 不放宽。
22 场景 lifecycle 的 220 份输出（154 CSV）也与冻结 pending-fix 参考一致。

JIT bundle：`/home/emsky/project/archives/SCP-SatComPlate-input-closeout-20260915/jit-history.bundle`
（另有 `output/input-repo-closeout/archive/` 副本）。已在只导入 n5 的独立 bare 仓库内 verify、
fetch、fsck，恢复 head 为 `2ff3c5c98d58ed68c192f97cf47dff7a7e45cefd`，独有提交数=5。
其两个 prerequisite 都属于永久 n5 历史，不依赖待删除的分支引用。

最终 PR、CI、merge 与分支清理完成后补充下方收据；当前不得提前声称已合并。
NEXT PLANNED WORK：Multi-tree baseline integration（需要单独批准）。

## 最终本地门禁

全部通过：full build、217 Python（1 项既有跳过）、全部 C++、SER、optional INPUT/recovery、
frequency、smoke（含 16 placement）、maintained regression。
Regression 含既有 N4B 100-task 验收，不是新的正式 800-task/1300s INPUT 性能矩阵。
证据：`output/input-repo-closeout/{final-build,final-python,final-cpp,final-smoke,final-regression}.log`，
三模式输出、G1 equivalence、lifecycle 输出和依赖扫描也在该目录。
最终默认、SER 严格比较、概率随机流、场景、Frequency/Placement/Recovery 语义未改。
