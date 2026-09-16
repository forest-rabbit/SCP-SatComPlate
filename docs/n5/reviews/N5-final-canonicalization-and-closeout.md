# N5 canonical closeout：CompFRR-P

状态：**LOCAL_GATES_PASS / RELEASE_IN_PROGRESS**。2026-09-16 人工通过收口审阅，
授权删除 recent-U 运行实现、阶段 CI、经 n5 合入 main、创建 `n5-complete` 和安全清理分支。

## 执行身份与范围

分支 `fix/compfrr-policy-aware-input-admission`；工作树 `SCP-SatComPlate-policy-aware`。
原 `SCP-SatComPlate` 工作树及其中 Stage 2B 未提交内容未修改。

- 开始：`05e0aad446bcb690310fa90bc722559ad5ded0eb`。
- 独立 consistency fix：`4b558cbe1`。
- pre-rename 基线：上述 fix 后的工作树，先捕获小场景再修改命名。
- post-rename production：`e48d0eba9804f74fbb22b9e69ac3d67f98a080cd`。
- 本文及 canonical README 为后续纯文档提交；不修改性能证据身份。

无新的 1300 s / 5–10 Gbps / 多 run 矩阵，无 N6/N7；未混入 JIT/V7 或新算法。

## 最小 consistency fix（先提交）

仅在 existing peer forecast 构造处读取 `Resolve(task, remote).mode`：
READY / IN_FLIGHT 设置 `recoveryInputSeconds=0`；FETCH 不设置 override，沿用 legacy
`S/B_I`。LocalDelivery 保留 0 INPUT 语义。未把 `remainingNs` 代入 P model，
未修改 Resolve、runtime recovery、receiver completion 或 dependency DAG。

原有 5/10 Gbps 审计快照分别为 407/409 次决策、22,773/23,105 个候选。
类别口径复算后，两组 hard-feasible set 与 selected remote 均保持不变；
原 92 个证据文件不写入。精确 window/R 差值仍因历史 trajectory 缺失而不能全部重算，
不将 bounds 写成精确测量，不声称已完成新版本完整性能复跑。
详见 [peer 审计第 9 节](CompFRR-peer-INPUT-contract-audit.md)。

## 名称与文件映射

| 原 | 当前 |
|---|---|
| N5cVariant / ParseN5cVariant / N5cVariantName | CompFrrPlacementVariant / ParseCompFrrPlacementVariant / CompFrrPlacementVariantName |
| N5c() / m_n5c | PlacementTracker() / m_placementTracker |
| N5cPeers / SelectN5cRemote / RevalidateN5c | CompFrrPeers / SelectCompFrrRemote / RevalidateCompFrrPlacement |
| n5cTrace / n5cPeak | placementTrace / placementPeak |
| placement Name() = n5c | compfrr |
| N5C_POST_BATCH_* / N5C_NO_FEASIBLE_REMOTE | COMPFRR_P_POST_BATCH_* / COMPFRR_P_NO_FEASIBLE_REMOTE |
| metrics/core/n5c-placement-metrics.cc | metrics/core/compfrr-placement-metrics.cc |
| n5c-placement-decisions.csv | compfrr-placement-decisions.csv |
| n5c-{recent,rational}-u-history.csv | compfrr-{recent,rational}-u-history.csv（recent writer 后续已退役） |
| satcompute-n5c-placement-test | satcompute-compfrr-placement-test |
| tests/unit/n5c-placement-test.cc | tests/unit/compfrr-placement-test.cc |

当前 placement analyzer 入口为 `analyze-compfrr-placement.py`；旧 analyzer 保留只读转发。
生产不再导出两组旧 P alias header，两个不参与构建的空实现标记一并删除：
`policy/compfrr/placement/n5c-placement-policy.{h,cc}`、
`runtime/n5c-placement-tracker.{h,cc}`；均可从 Git 历史恢复。
原别名使用者已改用 canonical 类型/头；quota 使用公共 `PeakQuotaLedger`。

旧命令描述与对比函数移到 `historical_scenario.py`，
参数适配实现移到 `historical_config_arguments.py`（旧 import 仅转发）。
当前 `scenario.py` 直接生成 canonical argv；wrapper CLI 使用
`--placement-mode=compfrr`、`--pressure-model=cumulative|idle-aware`、
`--placement-ablation=none|noR|noU|noM`，不接受旧 placement 名或 variant 开关。
测试覆盖旧/新可运行组合的完整 identity 对应，不仅检查默认组合。
历史 reader 在 `historical_placement.py` 支持旧 CSV；同时存在旧/新同义文件则拒绝歧义。

## 冻结清单

- 唯一 START / (δ,n) solver、SER `U_ser_pot > 1-P_F`、candidate coverage、fixed-local、
  anchor 后固定配置/P ranking、START/ON 分工未改。
- CUMULATIVE / IDLE_AWARE 并列正式 policy；noR/noU/noM 为消融，保留方案公式及默认值不变。
  原 rename 保留的 RECENT_U 已按后续人工决定物理移除，见下方专项 gate。
- `max(R,U,M)`、传播时延/稳定 ID tie-break、资源/路径硬约束、所有 baseline 能力不变。
- INPUT runtime、维护、恢复/relocation、同纳秒 fault batch、Storage/WU/bytes 记账不变。
- 场景和 para 默认未改：800 tasks、194,119,753,287 input bytes、352,513,119 WU。
- task 120 的 bandwidth-normalized arrival 公式未改；5 Gbps 为 1024042825747 ns，
  10 Gbps 为 1024682825747 ns；F3 固定 node 62、1027055770726 ns、horizon 1300 s。
- 无 workload/fault/RNG/routing 参数变更；历史仿真产物不改名、不覆盖、不重标执行提交。

## 验证

| Gate | 结果 |
|---|---|
| 模块及项目自有测试 build | PASS（上游 examples / 全局 tests 保持关闭） |
| C++ 完整维护测试链 | PASS；含 baseline、同纳秒、compute、fault、routing、INPUT、recovery |
| peer + Frequency runtime | 12,352 checks PASS |
| INPUT staging runtime | 374 checks PASS |
| P 公式 / 约束 | 8,574 checks PASS |
| 小型 placement CSV 审计 | 10 组、14 次 proposal PASS |
| Python unit | 287 tests，OK；1 项需外部原生轨道切片的可选测试跳过 |
| pre/post rename semantic equivalence | 11 个组合、2,096 文件（1,835 CSV）PASS |
| canonical source / runner 与历史身份 mapping | PASS |
| git diff --check | PASS |

等价门禁对照：
`output/compfrr/n5-closeout-gates/{pre-rename,post-rename}/`。
显式 `--allow-placement-rename` 只允许列出的文件/fixture 路径及 placement identity/reason
逐项映射；CSV 列结构、顺序、数值全部比较，JSON 仅沿用原 output path/wall-clock 豁免。
新增负面测试确保 bytes、event time、schema、缺失文件仍触发失败；没有更新 golden 消除差异。

离线原快照复核：
`output/compfrr/n5-closeout-gates/peer-consistency-audit/`。
consistency 修复前的 smoke 捕获与开发探针保留在同一 gate 目录，失败探针不作为验收结果。
曾遇到 ccache 临时目录权限限制，以 `CCACHE_DISABLE=1` 绕过；最终 build/test 已完成。
运行中的 binary 重新链接曾使并行 CLI 检查短暂不可执行；最终 Python 门禁在 build 完成后重跑通过。

### recent-U 退役 gate（2026-09-16）

删除 RECENT_U enum / 解析 / 评分、专用窗口资源字段与采样、CSV writer、Online fixture 和
`run-n5c-recent-u.py`。依赖扫描确认 Rational-U 历史审计仍使用身份/等价工具，先抽入
`tests/support/protection/historical_recent_evidence.py` 并迁移调用者；此 helper 无执行入口。
共享 compute history 仍用于 Cumulative/Idle-Aware 的服务核对和空闲时长，不删除。
旧结果与只读分析器未修改；所有删除内容可从 Git 历史恢复。

build、完整维护 C++ 链、290 项 Python unit（1 项可选轨道切片跳过）通过；
P 公式/约束 8,568 checks、小型 placement 审计 9 组 / 12 次 proposal 通过。
与 `post-rename` 原证据相比，`recent-retired` 小门禁比较 **2,078 文件 / 1,819 CSV** 全部相等，
只允许原 `frequency/online-compfrr-placement-recent-U/` 的 18 份文件整体消失。
新增负面测试拒绝其余文件缺失、字节/时刻变化、空 reference 或部分保留的废弃 fixture。

## 9. Policy-aware / P consistency：CONSISTENT

candidate 的 Selective dry-run 与 existing peer 的有效 categorical contract 对称。
P 只用 0 或 legacy serialization；真实恢复仍用当前因果 dependency resolver，并等待真实接收。
LocalDelivery、FETCH/refetch、IN_FLIGHT unknown remaining 均由 focused test 覆盖。
本结论不是对未来 R 或端到端性能的额外保证。

## 剩余文本逐文件分类

检查 `git grep -ni n5c`（大小写两种写法结果相同）。
平台 owned production C++/CMake、public CLI 与当前 canonical runner 的算法身份命中为 **0**。
下面保留位置不能被误当作当前生产别名；新测试/本文中的旧名仅用于映射与拒绝检查。

### 历史指令与阶段证据：保留当时名称、结论、链接，不追改 provenance。

- `AGENTS.md`
- `MILESTONES.md`
- `docs/n5/reviews/Checkpoint-maintenance-semantics-audit.md`
- `docs/n5/reviews/CompFRR-1G-candidate-coverage-stage2a.md`
- `docs/n5/reviews/CompFRR-input-binary-admission-runtime.md`
- `docs/n5/reviews/INPUT-final-repository-closeout.md`
- `docs/n5/reviews/N5A-G3-recovery-loop.md`
- `docs/n5/reviews/N5A-G4-integration-accounting.md`
- `docs/n5/reviews/N5B-G1-frequency-policy.md`
- `docs/n5/reviews/N5B-G2-dynamic-frequency-runtime.md`
- `docs/n5/reviews/N5B-G3-frequency-evaluation.md`
- `docs/n5/reviews/N5B-G3R-semantic-corrections.md`
- `docs/n5/reviews/N5B-G3R2-feasible-pair-capacity-retry.md`
- `docs/n5/reviews/N5B-closeout-N5C-kickoff.md`
- `docs/n5/reviews/N5B-final-800-task-scene.md`
- `docs/n5/reviews/N5B-final-architecture-cleanup.md`
- `docs/n5/reviews/N5C-U-multirun-audit.md`
- `docs/n5/reviews/N5C-rational-U-main-scenario.md`
- `docs/n5/reviews/N5C-rational-U-multirun.md`
- `docs/n5/reviews/N5C-recent-U-evaluation.md`
- `docs/n5/reviews/N5R-implementation.md`
- `docs/n5/reviews/Pre-N5C-baselines-recompute-oneplusone.md`
- `docs/n5/reviews/Pre-N5C-cb-sat-v2.md`
- `docs/n5/reviews/Pre-N5C-compfrr-input-deferred.md`
- `docs/n5/reviews/Pre-N5C-compfrr-riskweighted-start-llm4x.md`
- `docs/n5/reviews/Pre-N5C-on-capacity-resume.md`
- `docs/n5/reviews/Pre-N5C-placement-baselines-final.md`
- `docs/n5/reviews/Pre-N5C-v7-cbsat-joint-audit.md`
- `docs/n5/reviews/Protection-architecture-consolidation-audit.md`
- `docs/n5/reviews/Protection-config-hierarchy.md`
- `docs/n5/reviews/Recovery-direct-deadline-feasibility.md`
- `docs/n5/reviews/branch-closeout-plan.md`
- `docs/n5/reviews/docs-cleanup-manifest.csv`
- `docs/n5/reviews/protection-dependency-map.md`
- `docs/n5/reviews/protection-file-manifest.csv`
- `docs/n5/reviews/tests-cleanup-manifest.csv`

### 当前文档中的历史链接或旧名拒绝说明：不代表可运行的旧参数。

- `contrib/satcompute/protection/baseline/checkbullet/README.md`
- `contrib/satcompute/protection/baseline/checkbullet/preflight.md`
- `contrib/satcompute/tests/README.md`
- `docs/README.md`
- `docs/protection/architecture.md`
- `docs/protection/baselines.md`
- `docs/protection/compfrr-p.md`
- `docs/protection/reproducibility.md`

### 隔离的历史适配：只读旧身份/CSV，current scenario 不经旧命令生成器执行。

- `contrib/satcompute/tests/support/protection/historical_config_arguments.py`
- `contrib/satcompute/tests/support/protection/historical_placement.py`
- `contrib/satcompute/tests/support/protection/historical_scenario.py`
- `contrib/satcompute/tests/support/protection/historical_recent_evidence.py`

### 历史审计/runner：保留既有调用者、source guard 和旧报告结构；不是当前正式入口。

- `contrib/satcompute/protection/baseline/checkbullet/tools/analyze-cb-sat-matrix.py`
- `contrib/satcompute/tests/integration/regression/analyze-n5c-rational-multirun.py`
- `contrib/satcompute/tests/integration/regression/analyze-n5c-rational-u.py`
- `contrib/satcompute/tests/integration/regression/analyze-n5c-recent-u.py`
- `contrib/satcompute/tests/integration/regression/analyze-n5c-u-audit.py`
- `contrib/satcompute/tests/integration/regression/analyze-pre-n5c-placement-matrix.py`
- `contrib/satcompute/tests/integration/regression/analyze-recovery-deadline-reruns.py`
- `contrib/satcompute/tests/integration/regression/analyze-recovery-u-revalidation.py`
- `contrib/satcompute/tests/integration/regression/audit-checkpoint-maintenance.py`
- `contrib/satcompute/tests/integration/regression/audit-direct-recovery-deadline.py`
- `contrib/satcompute/tests/integration/regression/run-checkpoint-maintenance.py`
- `contrib/satcompute/tests/integration/regression/run-n5c-placement.py`
- `contrib/satcompute/tests/integration/regression/run-n5c-rational-multirun.py`
- `contrib/satcompute/tests/integration/regression/run-n5c-rational-u.py`
- `contrib/satcompute/tests/integration/regression/run-n5c-u-audit.py`
- `contrib/satcompute/tests/integration/regression/run-pre-n5c-placement-matrix.py`
- `contrib/satcompute/tests/integration/regression/run-recovery-deadline-reruns.py`
- `contrib/satcompute/tests/integration/regression/run-recovery-u-revalidation.py`
- `contrib/satcompute/tests/support/protection/baseline_audit.py`
- `contrib/satcompute/tests/support/protection/frequency_audit.py`
- `contrib/satcompute/tests/support/protection/input_staging_audit.py`
- `contrib/satcompute/tests/support/protection/peer_input_contract_audit.py`
- `contrib/satcompute/tests/support/protection/risk_start_audit.py`

### 历史 fixture、负面 CLI 和语义等价测试：刻意覆盖旧名读取或拒绝，不导出旧 production identity。

- `contrib/satcompute/tests/integration/regression/run-protection-config-equivalence.py`
- `contrib/satcompute/tests/integration/regression/run-protection-equivalence.py`
- `contrib/satcompute/tests/unit/test_compfrr_canonicalization.py`
- `contrib/satcompute/tests/unit/test_n5c_placement.py`
- `contrib/satcompute/tests/unit/test_n5c_rational_multirun.py`
- `contrib/satcompute/tests/unit/test_n5c_rational_u.py`
- `contrib/satcompute/tests/unit/test_n5c_u_audit.py`
- `contrib/satcompute/tests/unit/test_placement_matrix.py`
- `contrib/satcompute/tests/unit/test_protection_architecture.py`
- `contrib/satcompute/tests/unit/test_protection_config_mapping.py`
- `contrib/satcompute/tests/unit/test_protection_contract.py`
- `contrib/satcompute/tests/unit/test_protection_equivalence.py`

### 上游非平台内容：文本子串 n5Client 或二进制偶合，与本算法无关，不修改。

- `doc/manual/figures/perf-chunk-processor.png`
- `src/lte/doc/source/figures/fading_pedestrian.pdf`
- `src/lte/doc/source/figures/lena-dual-stripe.eps`
- `src/lte/doc/source/figures/lte-strongest-cell-handover-algorithm.eps`
- `src/lte/doc/source/figures/mac-random-access-contention.pdf`
- `src/traffic-control/examples/fqcodel-l4s-example.cc`

## 发布 gate

人工 STOP 审阅已通过。按当前链 → n5 → main 发布，阶段 CI 通过后创建 annotated
`n5-complete`；不重跑完整 5/10 Gbps，不进入 N6/N7。
工作树清理必须先保全 ignored 证据；原 `SCP-SatComPlate` 的 Stage 2B 未提交内容另行保留，
不能因提交祖先已经合入而一并删除。PR、CI、tag 凭据在实际完成后补记。
