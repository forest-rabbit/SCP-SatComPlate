# N5R Protection Architecture Consolidation

执行依据：[已批准只读审计](Protection-architecture-consolidation-audit.md)。此页仅记录分步提交、边界与 gate，不重写历史结果。

## 最终审阅补充：目录收口与历史 V7 离线诊断

用户批准 `N5R_Final_Architecture_Closeout_and_V7_JIT_Offline_Audit.md` 及四项统计补充。
当前起点 `fcdfe2db8` / Draft #103，沿用本分支，不改模型/参数/随机流/INPUT timing。

| 阶段 | 目标及门禁 | 状态 |
| --- | --- | --- |
| A 目录最终收口 | 完整 baseline 统一到外层；公共 placement 单独归位；CB 参数/工具唯一 owner；Multi-tree 仅 README；build/unit/contract/依赖与唯一性/小型等价/16 placement/CB 8+repeat | build、C++、Python/owner/include 检查、16 placement 通过；小型等价见下方路径审计；CB 8+repeat 在干净提交后执行 |
| B V7 离线诊断 | A 全通过后，复用已有 run11 与同批 Deferred；逐 lifecycle、守恒与故障表、严格配对及报告；缺失值不伪造；不运行仿真 | 待执行 |

字节分类优先级：实际使用 → 无需恢复的故障未发生 → 目标不一致 → 其余失败/取消。
失败等原因另保留独立标签；未建立流的请求不伪造字节。
非关键路径只表示实际恢复时未额外等待 INPUT，不等于预取无收益；已发比例不代表接收 READY。
旧 V7 结果仍属于其执行提交，不冒充 corrected N5R 的性能结果。原文保留在项目外任务书。

A 的单独差异审计：原严格 gate 拒绝两个 CB CLI 的 `cb-sat-parameters.json.profile_path`，
这是任务书明确要求迁移 frozen profile 后的真实文件位置变化。1965 文件全量核对未发现其他差异，
1707 份 CSV 全部逐字节相同；profile 和 provenance 均与 `fcdfe2db8` 逐字节相同。
未刷新 golden、未修改任何运行输出。新增默认关闭的精确路径迁移开关，只有指定两个 JSON 的
指定 old→new profile_path 允许改变，MTBF/其他字段仍严格拒绝；损坏和越界例外测试通过。
对照目录为 `output/n5r-equivalence/final-owner-closeout/`，reference 为目录调整前的 `stage8-archive/`。
ns-3 跳过已有 generated include 的行为会留下旧路径；项目 CMake 只刷新 14 个已知旧 stub，
未改上游代码。旧 policy/baseline 的余下 5 个 Python cache 已移至 `/tmp/n5r-obsolete-baseline.vZRtmB/`。

下方八阶段记录为此次最终目录审阅之前的过程快照，其中路径保留决定由本节最终收口更新。

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
| 6 tests | 七个长期 helper（accounting/frequency/baseline/placement/risk-start/INPUT/scenario）抽到 tests/support/protection；六组 unit 和 CB 场景调用者迁移；旧入口全部转发，独立 oracle 不调用 production solver；正式场景 CLI 也撤出 recent-U，历史 argv 描述保留 | build no-op、Python 195（skip 1）、C++ 合同 PASS；新增 include 边界、旧入口单实现、recent-U 不创建输出测试；small equivalence 1965 文件（1707 CSV）完全一致 |
| 7 canonical docs | 模块 README 压缩至 65 行；六个专题分别拥有 architecture/F/P/recovery/baselines/reproducibility；CB 新源码与原 profile/工具路径分开；旧报告保留历史语境；里程碑仅补已合入的 #102 事实 | 12 份文档/85 个本地链接无缺失；build no-op、Python 195（skip 1）、C++ 合同 PASS；small equivalence 1965 文件（1707 CSV）完全一致 |
| 8 历史归档 | 依赖扫描完成；见下表。recent-U 原位历史归档；旧头/七个转发入口/三处 source marker 保留；不删除证据、不放宽旧 source guard；include 边界测试扩至 common 与执行 .cc | build no-op、Python 195（skip 1）、C++ 合同 PASS；small equivalence 1965 文件（1707 CSV）完全一致；正式矩阵不运行 |

## 小型等价证据

步骤 1–7 的提交依次为 `fdc144ba6`、`19f470c01`、`76d0083fc`、`55ffca53a`、
`b8ecd2c8a`、`a32470433`、`4248f5f67`，步骤 8 为 `939acfb6f`；均在对应 gate 通过后单独提交。

`tests/integration/regression/run-protection-equivalence.py` 只运行已有 C++ fixture 和 4-task/16-node/15s CLI 场景。覆盖 Frequency、recovery（含 failure/path/deadline/maintenance）、Recompute、1+1、CB、自有 F Eager/Deferred 与 P Cumulative/Idle-Aware。

基线目录 `output/n5r-equivalence/corrected-baseline/`，各步使用新的输出目录，禁止覆盖。全部 CSV 的 schema/顺序/值逐字节相等；JSON 仅规范化输出位置及 wall_clock_ns/wall_clock_s，保留所有语义字段。实际 faults、WU、bytes、storage 和路径差异均失败。日志与退出状态另行检查，不使用 SHA-256。

这里的 gate 不宣称刷新或替代历史正式性能矩阵；N6/N7 再做正式实验。

## 依赖扫描与原位归档决定

以 `rg` 扫描 `contrib/satcompute/{protection,tests}`、CMake、satcompute.cc、文档及 CI 入口，
区分直接 include/runpy、脚本命令、字面 source guard 与纯历史引用。第 6 步先抽出七个长期 helper、
迁移维护 unit/CB 场景调用者；仍有依赖的兼容入口不按名字删除。

| 对象 | 可核对的实际依赖 | 本轮决定 |
| --- | --- | --- |
| RECENT_U enum/score/历史 CSV | `unit/n5c-placement-test.cc`、`frequency-runtime-test.cc` 及 `support/protection/placement_audit.py` | 保留历史实现与 fixture；production CLI 已拒绝；不是正式 compute-pressure enum 的成员 |
| `run-n5c-recent-u.py` | `run-n5c-rational-u.py` 复用 identity/equivalence；`test_n5c_u_audit.py` 验证历史命令 | 标记 archived-in-place；保留原 guard，不重新启动旧五组 |
| `analyze-n5c-recent-u.py` | `test_n5c_u_audit.py` 使用独立 window reconstruction | 保留只读历史审计；部分输出不能作为完成证据 |
| 三处未编译 `.cc` source marker | recent/rational、recovery、maintenance 历史 source-scope；Recompute guard 另覆盖实际新实现 | 保留路径，明确不是第二套代码；不靠空 marker 认证当前实现等价 |
| 旧 controller/P/CB 导出头 | CMake header export、satcompute.cc、C++ fixture 的 `ns3/*.h` include | 保留 forwarding/type alias；canonical `.cc` 已唯一编译 |
| 七个旧 Python 入口 | `run-cpp-tests.sh`、frequency/recovery smoke、CB `audit-cb-sat-run.py` 和历史矩阵仍调用 | 只转发到 `tests/support/protection/`，不复制算法；unit 验证单实现 |
| CB profile/provenance/tools | config 编译路径、MTBF 身份核验、smoke/历史工具链 | 原路径保留，C++ owner 已分离；MTBF 不变，不重新标定 |
| 只读审计六份产物与历史正式输出 | 原 PR、实验身份、旧分析链 | 保留原文/原输出；旧 manifest 为审计时快照，不追改为当前路径 |
| JIT/V7、CB 依赖分支、main/legacy | #99 仍依赖原 CB base；不在 corrected chain | 原样保留，不合入、不删除 |

没有发现可在不破坏上述依赖/证据的前提下直接物理删除的本轮候选；因此不做批量删除。
历史 runner 的 frozen source guard 不因重构放宽；现行等价性由独立 corrected-tree small gate 证明。
N5R 分支须待后续人工审阅/合并后才可清理；本轮不合入 n5/main、不打 tag。

## 最终本地收口

八步均已完成，最终执行提交为 `939acfb6f`；此后仅补本段证据，不更改可执行代码。

- build、维护 C++ 合同、Python 195 项（1 项因未提供 `SATCOMPUTE_POSITION_SLICES` 外部切片跳过）通过。
- 第 1–8 步各自相对同一 corrected baseline：1965 文件 / 1707 CSV 全量等价；最终目录 `output/n5r-equivalence/stage8-archive/`。
- smoke 总入口通过，含保护、恢复、Frequency、baseline 及 16 个 placement 小组合。
- CB 八个 4-task/15s 组合及 repeat 全部通过，目录 `output/cb-sat-v2/20260914T074052541358Z-smoke/`；
  repeat 核对 26 个 CSV/JSON 文件。八组加 repeat 的 189 份 CSV 与步骤 0 的 corrected-chain CB smoke 逐字节一致。
- `para.cc`、正式 input、fault、routing、上游 `src/` 及 CI/uv 配置相对 corrected n5 无差异。
- N5R 等待后续人工审阅与批准合并；未重复正式 1300s 矩阵、未触发 GitHub CI、未动 main/发布 tag。
  阶段 CI 仍按项目规则留在 N5 集成到 main 的最终门禁，不逐重构提交触发。
