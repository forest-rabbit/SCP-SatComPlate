# SatCompute 测试

本目录只维护 SatCompute 自有测试。配置、脚本和 GitHub Actions 都不会启用或运行
ns-3 上游 examples、全局 tests 或根目录 `test.py`。

## 当前配置层重构：小型语义等价门禁

配置仍是 `xx = xx;`，默认值位于 `protection/protection-para.cc`；ordinary CLI 使用
`protectionScheme` 和 scheme 私有参数。`unit/protection-config-test.cc` 验证 capability matrix，
`unit/test_protection_config_mapping.py` 验证 old→new 实验身份、默认值与拒绝合同。
`satcompute-protection-config-driver` 与平台编译同一入口，只额外开放三个测试注入参数：
`testBaselinePlacement`、`testCbSatBusyPolicy`、`testLrlRecoveryWeight`；不复制算法或 controller。
普通 `satcompute` 不接受这些参数，也不接受旧的 `protectionMode/placementMode/n5cVariant/inputPolicy`。

已有历史 runner 的参数描述接口保持兼容，真正 launch 前统一通过
`support/protection/config_arguments.py` 转换。其只读记录保留原 argv、实验身份与 inactive 字段；
历史命令对比用 `canonical_experiment_arguments()`，不修改已有 `execution.json`，
不放宽 source/commit guard。旧 CB 省略 busy 的 relocate 与新 CB canonical recompute 不会归为同一身份。
RECENT_U 只允许读取历史证据，执行适配器拒绝；JIT 亦未恢复。

55 组 4-task/15s 配置门禁覆盖全部 baseline placement、Fixed、三种 INPUT、五种 P 配置、
无保护/shadow、零存储、自定义 cadence 和受控 F3。对照必须来自修改前二进制：

```bash
python contrib/satcompute/tests/integration/regression/run-protection-config-equivalence.py \
  --output-root output/protection-config-hierarchy/new-check \
  --reference output/protection-config-hierarchy/config-before --jobs 2
```

`--legacy` 仅用于旧二进制捕获；不能在新代码上创建参考再声称跨版本等价。
本次实际证据和人工审阅停止点见[配置层收口](../../../docs/n5/reviews/Protection-config-hierarchy.md)。

### INPUT 三模式与维护测试

`compfrrInputPolicy=eager|deferred|selective` 是唯一平台 INPUT 开关，正式默认 selective；Selective 固定 SER。
平台正式默认 CompFRR-F + CompFRR-P（cumulative、无消融）+ Selective + Relocate。
历史描述 API 和普通运行默认严格分开：前者补齐旧 off/FA-FFP/Eager，当前正式 runner 显式序列化完整组合。
无保护、网络、轨道及 shadow 测试显式使用 off；Fixed 测试显式指定 Eager/公共 placement。
`unit/test_input_admission_policy.py` 调用纯 C++ 选择器，使用 tracked
`fixtures/protection/selective-input-ser-anchor.json`：405 个网络候选逐任务匹配，
68 个 SEND，另 4 个 LocalDelivery。fixture 是因果模型测试，不是未来实验流数目标；缺失时必须失败。

- `input-staging-runtime-test.cc`：真实接收/续传、同星交付、pending、两种合法 refetch、
  F3 与同纳秒边界、实际 compute-start USED、字节守恒和零泄漏。
- `frequency-runtime-test.cc --onlyInputAdmission=1`：SER 在线 generate 接线，核对实际节点对、
  pre-START_CHECKPOINT 采集时序、成功准入与 canonical prediction 采样窗口。
- `test_input_admission_runtime_audit.py`：复用 `support/protection/input_admission_runtime_audit.py`，
  用真实 START/Frequency committed actual pair 与生产 CSV/JSON 验证全生命周期账目，不需要开发大快照。
- `test_final_scenario.py`：三模式参数映射及历史证据名称规范化；旧公开 CLI 必须被拒绝。

### 小场景等价验证

不重复正式 800 任务 / 1300 s 矩阵，不重新标定故障或 MTBF。从项目根目录运行：

```bash
source .venv/bin/activate
./ns3 build -j 8
contrib/satcompute/tests/unit/run-cpp-tests.sh
python -m unittest discover -s contrib/satcompute/tests/unit -p 'test_*.py'
python contrib/satcompute/tests/integration/regression/run-protection-equivalence.py \
  --output-root output/equivalence/new-check \
  --reference output/input-repo-closeout/equivalence --jobs 3
```

比较要求已有独立参考，output-root 必须是新目录。缺少旧基线时不能用修改后的代码
自建 golden 声称等价。11 个组合包含 C++ fixture 与 4-task/16-node/15s CLI；CSV 整表
逐字节比较，JSON 只规范化输出路径与 wall-clock。
对 INPUT 收口前的参考，可显式使用 `--allow-retired-input-snapshot`，只允许旧开发 JSON
消失，其他生产 schema、数据和生命周期完全一致。旧 CB owner 迁移另有严格的
`--allow-cb-profile-relocation`，只接受两个指定路径值变化，不忽略其他参数。

一次性 INPUT/JIT offline 栈与开发矩阵 runner 已删除；设计取舍和原始证据身份见
[最终 INPUT 报告](../../../docs/n5/reviews/CompFRR-input-binary-admission-runtime.md)，
完整删除清单见[仓库收口](../../../docs/n5/reviews/INPUT-final-repository-closeout.md)。
通用 accounting、baseline、placement、risk/scenario helpers 保留，正常运行不自动做离线统计。
recent-U 不在 production CLI；历史分析/fixture 保留。日常 CI 只运行本目录维护测试。

## 历史专项与目录索引

以下 N5C/U/recovery 等阶段命令及“等待/保持 PR 未合并”等描述是当时合同的历史记录，
不是当前执行计划；#100/#101 及修复链已由 #102 整合。旧 runner 的冻结 source guard 保留，
不要为在 N5R 重跑历史矩阵而放宽；当前只执行上面 small gate 和维护中的 unit/smoke。

恢复 deadline 专项：`unit/recovery-runtime-test.cc` 覆盖 direct/迁移的完整完成预算，
含 Eager/Deferred、空闲但超时、无可行目标、开关与 LocalDelivery。
历史审计 `integration/regression/audit-direct-recovery-deadline.py` 不运行仿真；
`run-recovery-deadline-reruns.py --phase all --jobs 3` 仅手动运行 run11 FULL/noU/Rational-U，
拒绝覆盖旧目录，要求干净提交，不进入 CI。完成后使用
`analyze-recovery-deadline-reruns.py` 核对实际账本与三方配对。
审计范围不等于补跑范围，详见[专项报告](../../../docs/n5/reviews/Recovery-direct-deadline-feasibility.md)。

后续五轮 U 补齐使用 `run-recovery-u-revalidation.py --phase all --jobs 8`，
只新增 FULL run12/14、noU 与 Rational-U run12/14/15，复用其余 7 组；
包含 INPUT 路径早退影响。`analyze-recovery-u-revalidation.py` 审计 15 组及正/负单任务长尾排除。
二者均位于 `integration/regression/`，通过项目 `.venv` 的 Python 手动运行，不进入 CI，拒绝覆盖原始输出。

```text
tests/
├── unit/                    Python 与 C++ 聚焦测试
├── integration/
│   ├── smoke/               单能力快速闭环
│   └── regression/          五种路由与完整任务回归
├── fixtures/
│   ├── constellation/       4/16 星轻量星座
│   ├── topology/            最小节点切片
│   └── task/                合法与非法算力/任务输入
└── support/                 C++/Python 测试公共构造
```

日常单元、smoke 和回归输出写入临时目录并在退出时清理。手动正式场景的原始指标
保存在 gitignore 排除的本地 `output/`，完整正式场景运行不接入 `run-all.sh` 或 GitHub CI。

N5C V4 使用 `integration/regression/run-n5c-placement.py`，保持冻结的 800 任务 / 1300 s / seed 1 / run 11。
激活项目 uv 环境并完成构建、保持干净提交后，依次运行：

```bash
python contrib/satcompute/tests/integration/regression/run-n5c-placement.py --root output/n5c-v4/formal --phase gate
python contrib/satcompute/tests/integration/regression/run-n5c-placement.py --root output/n5c-v4/formal --phase main
python contrib/satcompute/tests/integration/regression/analyze-n5c-placement.py --root output/n5c-v4/formal
python contrib/satcompute/tests/integration/regression/run-n5c-placement.py --root output/n5c-v4/formal --phase ablation
python contrib/satcompute/tests/integration/regression/analyze-n5c-placement.py --root output/n5c-v4/formal
```

gate 只重跑 R5/R7 FA-FFP，与旧输出逐 CSV/JSON 核验；主实验只新增 Eager/Deferred 两组，
消融为 Deferred 的 noR/noU/noM，复用 full。拒绝覆盖输出或混用运行 commit。
新增 `satcompute-n5c-placement-test` 和既有 frequency runtime 中的 N5C 边界 fixture；
离线审计核查 min-max、固定配置、存活 exposure、assignment 积分和实际资源守恒。
`comparison.json/csv` 中 WU 与 eq-WU 分开、路径比例分母为 recovery_attempted，busy 分母为故障时具有 designated backup 的任务。
这是单一固定场景的描述性比较，不预设 N5C 优于 FA-FFP/FA-LRL。

U 专项 Gate A 只增加 seed 1 / run 12–15 的 Deferred FA-FFP/full/noU（十二组），
复用通过身份、命令和源代码等价核验的 run 11 三组。干净提交、构建后运行：

```bash
python contrib/satcompute/tests/integration/regression/run-n5c-u-audit.py --root output/n5c-u-audit --phase prepare
python contrib/satcompute/tests/integration/regression/run-n5c-u-audit.py --root output/n5c-u-audit --phase run --jobs 8
python contrib/satcompute/tests/integration/regression/analyze-n5c-u-audit.py --root output/n5c-u-audit
```

prepare 还重复执行既有 frequency runtime fixture，核验输出确定性（不是新增正式仿真）。
运行阶段禁止改变 HEAD/工作区；`--resume` 只复用已成功且身份完全相符的组，不覆盖部分输出。
离线输出 `summary.csv`、`full-vs-noU-task-diff.csv`、`u-decision-audit.csv`、
`paired-comparison.json` 和各 run 的 `task140-u-diagnostic.json`。
`test_n5c_u_audit.py` 检查冻结矩阵、历史截断、反事实 tie-break、缺失样本与元数据拒绝。
同快照反事实与完整 full/noU 轨迹差异分列；真实生成故障不强制相等。
完成后只进入[共同审阅](../../../docs/n5/reviews/N5C-U-multirun-audit.md)，不自动实现 recent-U。

后续 recent-U 实验已停止：小测试及 FULL/noU 两组完整等价门禁通过，五组 recent-U
正式进程在暂停后结束，部分输出只读保留、不能当性能证据。见
[历史状态](../../../docs/n5/reviews/N5C-recent-U-evaluation.md)。不要重新启动旧五组入口。

Rational-U 的 B0 快照与 B1 一组主场景已完成：seed 1 / run 11、Deferred/relocate、
800 任务/1300 s。复用已完成的两组旧方案正式等价门禁，重新验证旧小场景（含 recent-U）
输出等价；不改默认参数、不扩展 run 12–15、不自动进入 CI/合并。

```bash
python contrib/satcompute/tests/integration/regression/analyze-n5c-rational-u.py --snapshot output/n5c-v4/formal/R7-n5c --output output/n5c-u-freshness-snapshot
python contrib/satcompute/tests/integration/regression/run-n5c-rational-u.py --phase all
python contrib/satcompute/tests/integration/regression/analyze-n5c-rational-u.py --compare output/n5c-rational-u
```

入口拒绝覆盖已有证据；以上是复现顺序，不应对已有完成目录再次执行。
正式运行要求同一干净 HEAD。`test_n5c_rational_u.py` 验证因果 H/I、边界、评分和元数据；
离线核对每个候选的实际服务记录与 H/I，分别报告同快照反事实、完整轨迹及配对恢复。
精确零、`abs(U)<1e-12`、`abs(U)<1e-9` 分别统计；后两项只是诊断，不能进入评分。
结果写入 [Rational-U 审计](../../../docs/n5/reviews/N5C-rational-U-main-scenario.md) 后停止等待共同审阅。

2026-09-14 追加授权仅补跑 Rational-U run 12–15，复用 run 11 及十组 FULL/noU，
生产代码/参数保持 run 11 原样。五轮三方审计完成后停止，保持 #100/#101 未合并。

```bash
python contrib/satcompute/tests/integration/regression/run-n5c-rational-multirun.py --phase all --jobs 4
python contrib/satcompute/tests/integration/regression/analyze-n5c-rational-multirun.py
```

输出默认位于 `output/n5c-rational-multirun/`，拒绝覆盖；新增执行期间保持同一干净 HEAD。
`summary.csv`/`aggregate.json` 是分轮/汇总，`paired-comparison.json` 是同故障配对；
`leave-one-out.json` 按每轮和五轮合并分别去除恢复时间/总浪费的最大收益贡献及最大绝对贡献。
排除单位是同一个 `(run,task)`，两侧对称排除、不重跑；HHI/链路指标保留完整轨迹口径。
`test_n5c_rational_multirun.py` 检查执行边界、输入冻结、流去重、配对及长尾排除。
四组补跑和十五组审计已完成，见[五轮三方报告](../../../docs/n5/reviews/N5C-rational-U-multirun.md)；
三组均完成 3993/4000，证据不支持替换 FULL，停止等待审阅，勿重复启动。

Pre-N5C placement 消融入口为 `integration/regression/run-pre-n5c-placement-matrix.py`：
`--stage gates` 先运行 R5/R7-FA-FFP 并与最新 capacity-resume 原始文件比较；
`--stage remaining --jobs 8` 再运行其余 30 组，合计 32 组，每组 800 任务/1300 s，需显式授权。
两阶段必须同一干净 HEAD，拒绝覆盖已有组目录。`analyze-pre-n5c-placement-matrix.py --root 输出目录`
核对实际 WU/物理流/清理及频率公式，生成 master-summary CSV/JSON；不回写历史 CSV/JSON。
`run-placement-baseline-smoke.py` 仅为 16 组四任务接线检查，纳入维护 smoke，不运行正式场景。
原 FA fixture 保留；`n5b-policy-test.cc` 增加 minimal 布尔穷举和历史可行集 oracle，
`frequency-runtime-test.cc` 验证真实链路拒绝与不搜索第二候选，R0/R1 测试验证真实 deadline 准入差异。
`test_placement_matrix.py` 验证矩阵组数、零计数分布及 rename 比较不忽略业务差异。
这 32 组已验收冻结，见[最终报告](../../../docs/n5/reviews/Pre-N5C-placement-baselines-final.md)；
收尾及日常测试不重复正式矩阵。

N5A-G4 的冻结故障验收仍复用 `integration/regression/run-final-scenario.py`，
只在明确授权后手动运行；原始 N4 输出不可覆盖。例：

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --output-dir output/n5a-g4/off-replay --protection-mode off --fault-mode validation-replay \
  --validation-trace output/n4-release-validation/fault-trace.json
# FIXED 使用另一个新目录，把 --protection-mode 改为 fixed。
.venv/bin/python contrib/satcompute/tests/integration/regression/analyze-protection-accounting.py \
  --run output/n5a-g4/off-replay --reference output/n4-release-validation
# FIXED 分析只传 --run；不要求其业务输出等同 OFF。
```

缺少冻结 trace 必须停止，不重新 generate 替代。OFF 比较13份业务 CSV 与5份 JSON，
仅忽略 run-summary 的 wall_clock_ns/s；在线模型概率审计和旧 shadow 不属于回放输出。
会产生离线 `protection-accounting.json` 与逐任务 CSV，正常平台运行不自动执行该分析。
`recovery-runtime-test.cc` 覆盖 planned/actual、失败前缀、预留等待和三个人工核算锚点；
recovery smoke 另外比较 generate/验收回放、验证重复结果及 fixed 截断清理。

## Unit

`unit/run-cpp-tests.sh` 按固定顺序运行以下普通 executable：

| 文件 | 主要覆盖 |
|---|---|
| `para-test.cc` | `para.cc` 默认值、分组和关键压力测试默认项 |
| `protection-contract-test.cc` | N5A-G1 独立架构、存储守恒、状态大小、L1/RemoteCommit 时序、attempt 隔离与恢复选择；不发真实备份流 |
| `n5b-policy-test.cc` | FFP 原规则穷举对照、LRL 合成诊断、频率纯求解与 shadow 锚点、当前概率/同轮提交/前向配置合同；不接入真实频率运行时 |
| `protection-path-test.cc` | N5A-G2 真实 UDP 动态注册/乱序接收、ID、存储不足、非零初始化、取消与同纳秒计算结束 |
| `frequency-runtime-test.cc` | N5B 实际故障 epoch、提案/提交、四类状态、动态频率、PAUSE/恢复、LRL 实时负载与重复运行一致性 |
| `recovery-runtime-test.cc` | G3 受控 FaultController→备份/网络/计算/任务闭环，LocalDelivery、服务锁、F1/F2 免疫、F3、deadline、同纳秒实体快照和旧回调 |
| `recompute-baseline-test.cc` | 无常态保护、FFP 全量重传、从零执行、planned/actual 与 deadline/F3 截断 |
| `one-plus-one-baseline-test.cc` | 一次性副本准入、真实并行、正常故障暴露、完整 batch 后接管、首个 RESULT 与 loser 账本 |
| `link-window-test.cc` | 10 Gbps、空闲、双向独立、跨窗/尾窗、可用性、队列与预留时间积分 |
| `constellation-definition-test.cc` | 原生 shell CSV、字段约束和稳定卫星数量 |
| `routing-policy-factory-test.cc` | 五种路由名到 next-hop/path policy 的映射 |
| `task-input-test.cc` | ComputeProfile/TaskTrace closed-world 校验、canonical 排序和派生传输 ID |
| `compute-service-test.cc` | 整数服务时间、非抢占 FCFS、同刻 task ID tie-break 和因果运行任务快照 |
| `fault-lifecycle-test.cc` | FAILED/CANCELLED 幂等终止、迟到包隔离和 reservation 归零 |
| `fault-trace-test.cc` | 生成 trace 的字段、算术/区间校验和 canonical writer |
| `fault-risk-query-test.cc` | 1 秒节点风险、空闲/故障状态、动态恢复、双来源时长、F3 抢占、只读性、时间边界及 query/audit 不改变 RNG 与事件 |
| `fault-model-test.cc` | 内置参数、F1/F2 状态、独立抽样、`q_comp`，共享模型状态滚动预测、原生未来位置、无状态/RNG 副作用，以及 F3 fixed-K/Poisson 数量、范围、无放回和确定性 |
| `compute-fault-execution-test.cc` | 计算故障批处理、任务各阶段、恢复、通信不变、START 当刻抽样前预测和 生成事件与测试注入的一致性 |
| `satellite-fault-execution-test.cc` | 整星端点语义、即时重路由、capacity 重准入和按实时距离恢复 |
| `online-orbit-foundation-test.cc` | 原生 mobility、连续坐标、固定 plus-grid 候选和 canonical 顺序 |
| `online-topology-controller-test.cc` | 距离门控、fixed/distance 时延、周期更新和按边集合重算路由 |

长期 Python 测试收敛为：

| 文件 | 覆盖 |
|---|---|
| `test_task_workload_model.py` | 精确S/W/K/RESULT、rho/sigma/H、LLM、合法边界及5/10/20%守恒 |
| `test_final_scenario.py` | 冻结输入、两类种子、固定锚点、缺失切片/非法CLI、正式runner；可选原生切片逐字节复现 |
| `test_compfrr_shadow.py` | 全部正式任务跨语言布局、虚拟账本、资源分账、证据差异与离线统计 |
| `test_fault_probability_comparison.py` | 概率对概率一致性及缺失记录拒绝 |
| `test_fault_workload_fixtures.py` | 保留F1/F2/N4B固定fixture的角色与分布，不再依赖旧生成profile |
| `test_link_metrics_report.py` | 通用链路统计分位数 |
| `test_protection_contract.py` | 保护入口拒绝非法值与 shadow 混跑、generate 通过模式校验，以及生产模块无 shadow 依赖 |
| `test_baseline_evaluation.py` | R5 严格比较、物理流去重、loser RESULT 保留、planned/actual 与故障身份配对 |

C++另保留 `task-deadline-test.cc`（当前正式输入和deadline边界）以及
`compfrr-shadow-model-test.cc`（成本分档、严格START、初始化不双计、频率枚举、
OFF/ON追赶与状态边界）。通用task/routing/fault/network测试未删除。

测试内部的 `support/fault-injection.h` 只安排直接 ns 事件，保留同刻排序、任务中断和
拓扑恢复边界覆盖；不读取生产故障文件，也不提供用户 replay 模式。

## Smoke

N5A 定向验证（需要先构建；使用项目 uv 环境）：

```bash
./ns3 run --no-build "satcompute-protection-contract-test"
./ns3 run --no-build "satcompute-protection-contract-test --traceInput=contrib/satcompute/input/experiments/leo-66/workload/task-trace.json"
./ns3 run --no-build "satcompute-protection-path-test"
./ns3 run --no-build "satcompute-recovery-runtime-test --outputDir=output/n5a-g3/controlled"
.venv/bin/python contrib/satcompute/tests/integration/smoke/run-protection-smoke.py --output-root output/n5a-g2/smoke
.venv/bin/python contrib/satcompute/tests/integration/smoke/run-recovery-smoke.py --output-root output/n5a-g3/smoke
```

第二条只核对全部正式任务的字节/WU/合法边界，与 G4 oracle 单向比较，不运行正式网络仿真。
可加 `--sizingOutput=<新输出文件>` 保存四类代表性状态表；普通平台运行不输出这些验证数据。
G2 smoke 为 16 星/4 类任务/15 s 的 off、fixed、重复、容量不足四次运行，输出可留在上述目录；
不提供 `--output-root` 时自动使用临时目录。核对精确字节、cL/cR、l/r/x、存储、普通计算时间
及普通业务统计隔离；G3 新增受控恢复与平台入口 F3 对照，不是正式 800 任务/1300 s 实验。
详见 [protection](../protection/README.md)。

Pre-N5C 的 `run-baseline-smoke.py` 验证无故障 Recompute 与 off 的业务输出一致、
真实 replica INPUT/RESULT、重复运行确定性和输出目录切换清理，已纳入维护 smoke。
正式六组运行仍使用 `run-final-scenario.py`，先 R5 严格复现冻结 B，再 R0–R4；
`analyze-baseline-evaluation.py` 离线检查物理流/WU/资源守恒并输出统一比较，普通运行不自动分析。

| 脚本 | 主要覆盖 |
|---|---|
| `run-routing-smoke.sh` | 在线 IPv4、更新次数以及未变边集合不重复重算 |
| `run-capacity-aware-smoke.sh` | 完整路径准入、pacing 和 reservation 释放 |
| `run-task-smoke.sh` | 输入传输、FCFS 计算、结果传输的单任务闭环 |
| `run-diagnostics-smoke.sh` | strict 部分完成、队列丢包和失败诊断文件 |
| `run-topology-smoke.sh` | topology-only 切片、终点采样、XYZ 演化和逐字节确定性 |
| `run-compfrr-shadow-smoke.py` | 8任务off/on、重复、audit独立性、字节/队列账本及同纳秒F3/初始化顺序 |
| `run-protection-smoke.py` | G2 四类真实固定备份流、receiver/commit/存储守恒、off 计算对照和确定性 |
| `run-frequency-smoke.py` | N5B 四任务 generate CLI、概率逐值一致、FFP/LRL 统计、审计独立及 off 输出清理 |
| `run-recovery-smoke.py` | G3 16 星/4 任务/15 s 受控 F3，off/fixed/重复运行，同星 RESULT 的实际字节与零网络流 |
| `run-link-metrics-smoke.py` | 指标开关不改变业务、空闲/丢包/故障、窗口汇总和陈旧文件清理 |

## Regression

- `run-full-routing-regression.sh`：运行五种 IPv4 模式、distance 时延、重复
  size-aware 仿真和 66 星在线拓扑；
- `run-full-workload-regression.sh`：运行任务确定性、无任务模式、strict/report、
  失败诊断，并执行正式的 100 秒/66 星/20 任务示例；
- `run-fault-lifecycle-regression.sh`：F1 热/概率曲线、动态恢复、F2 原生空间风险与
  固定 8 秒恢复、小任务闭环、F3 永久断链；检查 RUNNING 中断、排队保留、通信不变、
  同 seed 重复一致。审计涵盖全部 RUNNING 任务的逐秒概率与剩余窗口，容差 1e-12，
  不依赖 NOTICE 或固定行数，并核对默认关闭/陈旧审计文件清理。
- `run-n4b-joint-acceptance.sh`：复用 66 星/1000 s/100 任务输入，检查四轮
  正常/审计/重复运行业务一致、联合来源、唯一 F3 重路由、零丢包、账本归零、
  所有任务/传输终态与实际 START 合同。旧 93/100、82 行属于旧 F1/NOTICE 结果，
  不再作为新模型固定 golden。

## 本地运行

首次运行先启用项目 uv 环境（`source .venv/bin/activate`），在仓库根目录配置并构建：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
```

随后依次执行完整本地门禁：

```bash
PYTHONDONTWRITEBYTECODE=1 .venv/bin/python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

定位失败时可直接运行对应具名 shell 脚本或 C++ executable。各 runner 默认使用
`./ns3 run --no-build`，因此修改 C++ 后必须先重新执行 `./ns3 build`。

## 最终生成器的原生切片复现

不启动网络仿真，复用已经导出的0..1050 s、间隔1 s原生节点切片：

```bash
SATCOMPUTE_POSITION_SLICES=output/n4c-g3-truncnormal-v3-20260909/orbit/topology \
.venv/bin/python -m unittest discover -s contrib/satcompute/tests/unit -p 'test_final_scenario.py' -v
```

该检查两次生成TaskTrace/摘要，比较双次字节一致及正式TaskTrace逐字节一致；
也核对全部原生放置记录。未提供该环境变量时仅跳过原生切片复现项，并明确显示skip；
其他固定输入、模型、合成位置和CLI测试仍执行。生成器不会读取正式TaskTrace作为生成源。
冻结验收必须提供已有切片并实际通过，不能用skip宣称复现完成。

## 手动正式运行与 G4 验证

`integration/regression/run-final-scenario.py`支持当前场景的none、generate、
generate+shadow，以及仅供 N5A 验收的显式 validation-replay。默认 generate + 正式 CompFRR 组合；
none/shadow/无保护 replay 必须显式指定 `--protection-mode=off`，概率 CSV 审计和 shadow 均默认关闭。
输出必须是新目录，当前1300 s、800任务、66星、10 Gbps、1 ms、seed1/run11。

N5B 最终场景移除前置任务 801，仅增大任务 120 到 800 MB；当时仅授权正式 B 组，
使用 `--protection-mode=compfrr --placement-mode=ffp`，不追加 A/C 或 CI。
`run-f3-protection-check.py --output-dir=新目录 --input-bytes 700000000 800000000`
是保留真实故障模型/轨道的 B 组单任务大小初筛；首次通过后停止，完整负载仍须单独验收。
`run-f3-protection-check.py --inspect-run=B目录` 只读检查：node62 在 F3 前无 F1/F2、
START 有收益、F3 前真实 ON、使用有效非零进度检查点且按期完成；仅完成或仅 START 不算通过。
`analyze-frequency-evaluation.py --runs B目录` 生成本次单组账本。
历史 G3R2 的 801 任务 B/C 结果保留，不能与本次或旧 A 严格配对；双组分析也要求同场景、同代码。
可行节点对、5 ms 释放、重复/部分释放、终态清理和无额外抽样测试位于既有 policy/runtime 单测。

历史 Pre-N5C v6/ON-resume 审计只运行 R4–R7：当时均为
`--protection-mode=compfrr --placement-mode=ffp`（该旧 FFP 现名为 `fa-ffp`），
R4/R5 使用 eager，R6/R7 加 `--input-policy=deferred`；R4/R6 加
`--remote-busy-recovery-policy=recompute`，R5/R7 为 relocate。
新 workload 为400 WU/token且总WU保持352513119；START使用初始化就绪后的风险加权进度，
ON评分不变；后续容量修订为路径阻塞的ON增加释放重试（原节点对、无额外抽样）。
历史输出在 `output/n5-on-capacity-resume/` 和 `output/n5-startscore-riskweighted-llm4x/`，
均不覆盖；当前四种 placement 的冻结矩阵以本页开头的入口及报告为准。
`integration/regression/analyze-riskweighted-start.py --r4 R4目录 --r5 R5目录 --r6 R6目录 --r7 R7目录
--output 新JSON路径` 只读核对同代码/同workload、评分、实际浪费、流量、恢复和399/596/574。
Python单测包含错误评分/INPUT/历史LLM状态口径及整数纳秒取整检查；生产不新增预测CSV总开关。
既有 `frequency-runtime-test.cc` 覆盖自身L1在途占用、eager/deferred释放后恢复、重复通知、
同纳秒故障/终止、存储阻塞及重复运行一致性；ON暂停与OFF等待分别记账。

历史 N5B-G3 的 800 任务在同一构建下手动各运行一次：A 使用 `--protection-mode=fixed`，B 使用
`--protection-mode=compfrr`，C 再加 `--placement-mode=lrl`。三者均为在线 generate，
不自动加入 smoke/regression/CI。LRL lambda 固定 1，无扫描。输出目录约定为
`output/n5b-g3/{A-ffp-fixed,B-ffp-compfrr-frequency,C-lrl-compfrr-frequency}`。
`analyze-frequency-evaluation.py --runs A目录 B目录 C目录` 只读这些原始输出，
核对配对运行参数、动态所有权和 N5A 实际账本，写入各目录的 `frequency-evaluation.json`
及父目录 `paired-evaluation.json`；不重跑、不修改原始 CSV。`test_frequency_evaluation.py`
验证统计口径，smoke 在四任务真实结果上检查同一个分析入口。

```bash
# 手动完整运行；不是日常短测试
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --output-dir=output/final-generate
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --fault-mode=none --output-dir=output/final-none
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --audit --shadow --output-dir=output/final-shadow

# 仅分析现有输出，不运行仿真
.venv/bin/python contrib/satcompute/tools/validation/compfrr-shadow/summarize.py \
  --run-dir=output/final-shadow --reference-dir=output/final-generate
```

比较双方必须采用相同概率审计开关（上例若用于逐文件比较，generate也加`--audit`）。
可选文件缺失不能静默忽略。none账本可用`tools/validation/summarize-n4c-baseline.py`分析。
通用CLI仍支持自定义实验参数，但正式runner不保留历史候选/调参兼容入口。

G4清理冻结只允许构建、unit、小型shadow/拓扑smoke与现有CSV离线分析；
**没有重新运行上述完整场景，也没有触发阶段CI。**
[最终验收](../../../docs/n4c/reviews/G4-final-freeze.md)与[解析结果](../../../docs/n4c/reviews/G4-shadow-decision-evaluation.md)
不代表真实备份、带宽/存储占用或故障任务被救回。
