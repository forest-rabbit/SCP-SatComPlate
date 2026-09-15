# Protection 配置分层实施与等价门禁

第 1–5 节是已推送的纯重构 `5cf0ac56e` 审阅快照（第 7 节仅澄清其 schema 措辞）；
后续正式默认配置调整 `44d1b6a53` 单列第 6 节，
不反向修改纯重构的默认值保留结论，也不把旧实验解释成新组合。

基线：corrected `n5@fd45c5144`；分支：`refactor/protection-config-hierarchy`。
范围：已批准 revised proposal 的 config/validation/wiring 与小场景验证。
不改数学模型、资源语义、故障随机流、正式输入和 production output schema；
public protection CLI 按本轮设计有意重命名和分层。纯重构保留原默认实验身份。
快照当时未开展 CI/PR/合并；最终审计已授权第 7 节的收尾集成，仍不开展 Multi-tree 或正式长矩阵。

## 1. 唯一 owner 与依赖边界

```text
para.cc -> protection/protection-para.cc       typed defaults，只有赋值/注释
satcompute.cc -> protection/protection-config.cc  CLI、显式来源、capability 校验
             -> 已有 scheme-specific controller/placement wiring

ProtectionConfig
├─ scheme / common / commonPlacement / diagnostics
├─ compfrr: checkpoint / placement / INPUT / recovery / pressure / ablation / fixed
├─ recompute: private placement
├─ onePlusOne: private placement
└─ cbSat: private placement / busy
```

`protection-para.h` 只声明 enum/struct，不保存仿真对象；`protection-para.cc` 保持 `xx = xx;`。
`protection-config.*` 不创建 flow/storage/event，不消费 RNG，不引入求解器。
`satcompute.cc` 仍接线原 Fixed/Frequency/CB/Recompute/1+1 controller；不是新增框架或复制算法。
普通二进制不提供旧别名；专用 `satcompute-protection-config-driver` 编译同一份 `satcompute.cc`，
仅启用 test-private 参数，保持同一 runtime。共享 Checkpoint/Recovery 未增加对 Frequency 的依赖。

## 2. 完整 old → new 映射

以下字段相对 `SatComputeConfig.protection`。新默认值不覆盖显式历史配置。

| 旧字段/CLI | 新字段 owner | 新普通 CLI / 复现方式 |
|---|---|---|
| `protectionMode=off` | `scheme=OFF` | `protectionScheme=off`，全局默认 |
| `protectionMode=fixed` | `scheme=COMPFRR; compfrr.checkpointPolicy=FIXED` | `protectionScheme=compfrr compfrrCheckpointPolicy=fixed` |
| `protectionMode=compfrr` | `scheme=COMPFRR; compfrr.checkpointPolicy=ADAPTIVE` | `protectionScheme=compfrr`，子策略默认 adaptive |
| `protectionMode=recompute/one-plus-one/checkbullet` | `scheme=RECOMPUTE/ONE_PLUS_ONE/CB_SAT` | `protectionScheme=recompute/one-plus-one/cb-sat` |
| Fixed/CompFRR 的 `placementMode` | `compfrr.placementPolicy` | `compfrrPlacementPolicy=ffp/fa-ffp/lrl/fa-lrl`；旧 n5c → compfrr，仅 adaptive |
| Recompute 的 `placementMode` | `recompute.placementPolicy` | 私有默认 FA-FFP；测试 driver `testBaselinePlacement` 保留四选一 |
| 1+1 的 `placementMode` | `onePlusOne.placementPolicy` | 同上，独立 owner |
| CB 的 `placementMode` | `cbSat.placementPolicy` | 同上，独立 owner |
| `n5cVariant=full` | `compfrr.pressureModel=CUMULATIVE; placementAblation=NONE` | `compfrrPressureModel=cumulative compfrrPlacementAblation=none` |
| `n5cVariant=noR/noU/noM` | 同一 CUMULATIVE + 指定 ablation | `compfrrPlacementAblation=noR/noU/noM` |
| `n5cVariant=rational-U` | `IDLE_AWARE + NONE` | `compfrrPressureModel=idle-aware` |
| `n5cVariant=recent-U` | 历史证据 | 只读归一化保留明确 historical-only 标识，执行适配器拒绝 |
| Fixed/CompFRR 的 `remoteBusyRecoveryPolicy` | `compfrr.recoveryPolicy` | `compfrrRecoveryPolicy=relocate/recompute`，默认 relocate |
| CB 的同名 busy | `cbSat.busyPolicy` | canonical 私有 recompute；历史 relocate 用 `testCbSatBusyPolicy=relocate` |
| off/Recompute/1+1 的同名 busy | 无活跃 owner | 旧值记录为 inactive，不向这些方案注入 CompFRR recovery |
| `inputPolicy` | `compfrr.inputPolicy` | `compfrrInputPolicy=eager/deferred/selective`；默认 eager，Fixed 仅 eager |
| `lrlRecoveryWeight` | `commonPlacement.lrlRecoveryWeight` | 私有默认整数 1；必要历史值用 `testLrlRecoveryWeight`，无新 sweep |
| `backupStorageBytesPerNode` | `common.backupStorageBytesPerNode` | CLI 名不变，默认十进制 10 GB，仅 pool 消费者 |
| `fixedProtectionDelta` | `compfrr.fixed.delta` | `compfrrFixedDelta`，默认 0.05，千分位精度；只在接线边界乘 1000 |
| `fixedProtectionBatchN` | `compfrr.fixed.batchN` | `compfrrFixedBatchN`，默认 4，n>0 且 n×delta≤1 |
| `compfrrShadow` / `compfrr-shadow` | `diagnostics.compfrrShadow` | CLI 名不变，默认 false |
| `compfrrShadowOutput` / `compfrr-shadow-output` | `diagnostics.compfrrShadowOutput` | CLI 名不变，空串时使用 outputDir/shadow |

非保护字段（simulation/fault/seed/run/constellation/task/compute/link/routing/output）不改名、不改默认。
旧 CSV 文件名、schema、枚举标签（例如 n5c/full/rational-U）、C++ 兼容头、tracked fixture 均保持。
本轮不做全库字符串替换，历史报告也不反向改写。

## 3. Capability matrix

“四种公共 placement”指 FFP、FA-FFP、LRL、FA-LRL；baseline 下是私有选择，不是能力删除。

| scheme / checkpoint | Placement | INPUT | Recovery / pool | Fault mode |
|---|---|---|---|---|
| off | 无 | 原业务 | 无真实保护池/流；独立 shadow 仍可用 | 原 none/generate/validation-replay |
| CompFRR / Fixed | 原四种 | 仅 Eager | 原 checkpoint/local-tail/optional relocation；两种 fallback；common pool | none/generate/validation-replay |
| CompFRR / Adaptive | 原四种 + CompFRR-P | Eager/Deferred/Selective | 原 Frequency 与恢复合同；common pool | generate 且 F1/F2 至少一个开启 |
| Recompute | 私有四种 | 故障后完整 INPUT | 从零重算，无常态 checkpoint；原 transfer-only 零池账本 | none/generate/validation-replay |
| 1+1 | 私有四种 | 一次真实副本 INPUT | 无第三副本/隐藏重算；原 transfer-only 零池账本 | none/generate/validation-replay |
| CB-Sat | 私有四种 | 完整 INPUT + 单节点日志 | 独立 H/X/恢复；busy 两值；common pool；无 CompFRR tail | none/generate/validation-replay |
| Multi-tree | 未实现 | 未实现 | 不创建机制；配置即拒绝 | 不可运行 |

所有真实保护要求 network tasks 与 shadow 关闭；validation-replay 仍需显式冻结 trace、关闭 audit/shadow。
CompFRR-P 仅 adaptive；production pressure 仅 CUMULATIVE、IDLE_AWARE。
noR/noU/noM 仅 cumulative 消融；Fixed+P、Fixed+Deferred/Selective、idle-aware+消融均拒绝。
显式跨方案 `compfrr*` 参数即使等于默认值也拒绝；未选择的 typed 子结构不参与运行。
显式 fixed cadence 只允许 Fixed，显式 pressure/ablation 只允许 P，显式 pool 只允许真正的 pool 消费者。

`compfrrRecoveryPolicy` 仍仅切换原 REMOTE_BUSY / DIRECT_DEADLINE_INFEASIBLE 分支，
不把 INPUT_PATH_UNAVAILABLE 等既有回退全部改成另一策略；CB busy 仍只作用于其原忙时分支。
Fixed 的一次性 START、ON maintenance、计算空闲合同、并行 INPUT/state/tail 屏障、同星 LocalDelivery
与真实 receiver completion 均未修改。

## 4. Baseline canonical private defaults 与历史身份

| baseline | canonical private default | 不复用/不新增 |
|---|---|---|
| Recompute | FA-FFP；正式 LRL weight=1；首次故障后完整重算 | 不消费 CompFRR INPUT/频率/pressure/busy；不新增 checkpoint 池 |
| 1+1 | FA-FFP；首次 TASK_RUNNING 一次资源约束申请 | 原免疫/同批故障/takeover/deadline 合同不变；不消费 CompFRR 私有项 |
| CB-Sat | FA-FFP + busy=RECOMPUTE；10 GB common pool | 保留单节点完整 INPUT/日志；无 tail、SER 或 Frequency |

CB 的 MTBF 唯一数据源仍为 `baseline/checkbullet/calibration/frozen-mtbf-profile.json`：
41.47642679900744 s（33430 s exposure / 806 次联合故障，原 pilot runs101–110）。
profile、provenance、H/X 与成本数据源未复制，也未重标定。

新 canonical CB 对应旧**显式** `checkbullet + fa-ffp + recompute`；旧 CLI 省略 busy 的真实值是
relocate，适配器明确切至专用 driver 并注入 relocate。不能把这种旧运行重命名成 canonical CB。
默认全局仍 off；只打开 CompFRR 时仍 adaptive + FA-FFP + Eager + relocate，未自动选 P 或 Selective。

历史 R0–R7 × 四种 placement、CB 四种 placement × 两种 busy，以及 P 两 pressure/三消融/三 INPUT
逐组做旧 argv→新 argv→归一化实验身份核对。旧参数描述 API 保留，实际 launch 才改名。
转换记录返回 `original / argv / identity / inactive / inactive_reasons`；baseline 不消费的 eager、
fixed cadence、busy、pool 等冗余参数按原实现来源标记，不伪造新资源或新策略。
`canonical_experiment_arguments()` 同时核对旧新命令，保留所有非保护参数；不放宽历史 source guard。
CB 工具的 `execution.json` 继续用原元数据 schema，command 记录实际新启动参数，两者经单测一致性验证。
旧 A/B/C、R0–R7 与 CB 跨方案对照使用只读 `historical_comparison_arguments()`，
把当前活跃控制还原成原表格的字段形状；非活跃 legacy 描述使用旧默认占位，仅供旧表格比较，
不代表真实资源配置，严禁回送 runtime。实际实验身份仍以严格的 `canonical_experiment_arguments()` 为准。

## 5. 验证证据

独立参考在改动任何 production 源码前由 `fd45c5144` 二进制捕获：

- `output/protection-config-hierarchy/before`：既有 11 组机制 gate。
- `output/protection-config-hierarchy/config-before`：新增 55 组配置覆盖，只使用 4 任务/16 节点/15s。

中间 config-after、after-rebuilt 均已严格等价；最终复核见同目录 `config-final`、`mechanism-final`。
比较 CSV 全部 schema、行序与数值（逐字节），JSON 仅规范化输出路径与 wall-clock。
不使用 CB profile 迁移豁免或旧 INPUT snapshot 删除豁免，不刷新 golden，不用摘要哈希替代比较。

| 门禁 | 结果 |
|---|---|
| build | 平台、专用 driver 与全部项目测试目标完成构建 |
| focused config | 123 项 capability/default/private isolation/invalid-combination 检查通过 |
| C++ unit | `tests/unit/run-cpp-tests.sh` 通过，含 Frequency、P、checkpoint、recovery、baseline、fault、routing |
| Python unit | 229 项：228 通过、1 项按原条件 skip；`python-unit-closeout.log` 含完整身份与 CLI 拒绝测试 |
| smoke | `tests/integration/smoke/run-all.sh` 通过，含 16 组 placement smoke |
| 55 组配置旧新对照 | 1,685 份文件，其中 1,337 CSV，严格等价 |
| 11 组机制旧新对照 | 1,984 份文件，其中 1,723 CSV，严格等价 |

命令从仓库根目录、项目 uv 环境执行：

```bash
source .venv/bin/activate
cmake --build cmake-cache -j 2
contrib/satcompute/tests/unit/run-cpp-tests.sh
python -m unittest discover -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/integration/smoke/run-all.sh
python contrib/satcompute/tests/integration/regression/run-protection-config-equivalence.py \
  --output-root output/protection-config-hierarchy/config-review \
  --reference output/protection-config-hierarchy/config-before --jobs 2
python contrib/satcompute/tests/integration/regression/run-protection-equivalence.py \
  --output-root output/protection-config-hierarchy/mechanism-review \
  --reference output/protection-config-hierarchy/before --jobs 2
```

每次 output-root 必须不存在，原始参考和失败记录均保留。首次机制重跑曾调用尚未同步重编译的
旧 C++ fixture，因 `SatComputeConfig` 布局变化而失败；重编译测试目标后原样通过，未修改模型或预期值。
系统 clang-format 14 不支持仓库 `BreakTemplateDeclarations` 配置，因此格式工具未执行成功，
未改仓库样式配置；`git diff --check` 用于补充空白检查，不冒称 formatter 通过。
Python 的 native 0..1050s 拓扑切片重生成测试需要显式 `SATCOMPUTE_POSITION_SLICES`，本轮未设置，
按原测试合同 skip；没有为配置重构新生成正式拓扑。

结论范围仅为已覆盖输入下的语义等价，不等于所有未来场景的形式证明或正式性能刷新。
用户随后授权提交并推送本分支供审阅；CI/PR/合并和后续 Multi-tree 等工作仍等待单独指令。

## 6. 后续授权：正式 CompFRR 默认组合

用户随后明确正式方案不是 off，指定 Adaptive CompFRR-F + CompFRR-P + Selective INPUT + Relocate。
`protection-para.cc` 的 scheme/placement/INPUT 默认相应改为 COMPFRR/COMPFRR/SELECTIVE；
CUMULATIVE、无消融、relocate 与全部 baseline private defaults 保持不变。
off 仅作为显式诊断/历史复现能力，不是正式对比方案；算法、输入、seed/run、恢复合同未修改。

普通平台不带保护参数即使用这套组合。正式 runner 同时写出完整显式 argv，避免默认值再次变化导致漂移。
历史 `arguments()` API 保留原意；执行/读证据适配器对省略旧 scheme 的调用显式补 off，
对截至 `5cf0ac56e` 的旧 scoped CompFRR 命令显式补原 FA-FFP/Eager，不能按新默认解释。
所有新正式 runner 命令已完整序列化，不依赖这套历史补全。
网络/拓扑/故障诊断显式 off；Fixed 显式 Eager + 公共 placement，未扩大其能力。

这不是对纯重构默认值的“等价改名”，也不是新的性能结论。仅执行 small gate：
默认启动与显式正式组合的 4-task/15s 全输出比较，以及原 55 组配置/11 组机制对照；
不重跑正式 1300s 矩阵，不自动提交/推送。

验证：目标构建、123 项配置检查、para/protection contract 测试通过；Python 232 项中
231 通过、1 项沿用原 native-slices 条件 skip。默认与显式正式组合全输出一致，
55 组配置对照（1,685 文件）及 11 组机制对照（1,984 文件）与原参考严格等价。
完整 smoke 通过（含 16 组 placement）；修改的诊断回归 shell 入口语法检查通过。
证据目录为 `output/protection-formal-default/`；历史参考未覆盖。

## 7. 最终审计收尾

报告中的 schema 保持仅指 production CSV/JSON 输出，不包含有意重命名的 public CLI。
纯重构 `5cf0ac56e` 与正式默认调整 `44d1b6a53` 分开提交，不能把后者描述为默认行为不变。

扫描当前 `output/`、`docs/`、`tests/` 中的 JSON/Markdown/Python/text 历史记录：
`output/n5c-recent-u/run-{11..15}/recent-U/` 的 5 份 `execution.json` 与 5 份 `time.txt`
全部使用 `--n5cVariant=recent-U`；未发现 two-token 形式。按最终任务书 9.1 不修改 adapter。
测试锁定历史描述 API 的等号形式、只读归一化不改原 argv、recent-U 不可执行；
未记录的 `--n5cVariant recent-U` 仍明确拒绝，不假称通用解析器已支持该历史边界。
production 的 recent-U 拒绝测试继续保留；本轮未开放任何新 capability。

本次只新增上述测试和审计澄清。复用第 6 节 smoke、55/11 strict-equivalence 证据；
合并前补跑维护中的 Python、C++ unit，且只调度一次既有阶段 CI（含原 regression）。
CI 不新增 ns-3 examples/tests 或正式 800-task/1300s 性能矩阵。
通过后按授权 PR -> CI -> merge n5 -> ancestry 检查 -> 清理本分支；不进入 Multi-tree 实施。

本地结果：目标构建通过（无待重编译源码），historical argument tests 14/14；
维护中的 Python 233 项（232 通过、1 项原 native-slices skip），完整 C++ unit 通过，
`git diff --check` 通过。两个收尾项均关闭：`PROTECTION_CONFIG_HIERARCHY_FINAL_APPROVED`。
该结论不替代随后一次阶段 CI 的集成门禁，也不宣称重新验证了正式性能矩阵。
