# Pre-N2 代码清理审计

## 1. 审计基线与边界

```text
Branch: refactor/pre-n2-code-cleanup
Base main SHA: fd393bda4d93c94fe711bc971cdf796c28bd2200
Routing milestone: pre-n2-routing-final
Main CI: success
Audit classification: APPROVED
Implementation allowed: YES
Unknown delete candidates: NONE
```

本审计只覆盖 SatCompute 自己增加的代码、输入、工具、文档和输出组织。
任何 ns-3 上游文件均不进入删除候选。分类含义：

| 分类 | 含义 |
|---|---|
| `KEEP` | SatCompute 当前仍需要，路径也合理 |
| `MIGRATE` | 功能和内容保留，只调整职责或路径 |
| `DELETE` | 已确认无引用、无唯一保留价值，且删除已获范围批准 |
| `UNKNOWN` | 当前证据不足，必须保留 |
| `KEEP_UPSTREAM` | SatCompute 当前依赖的 ns-3 上游内容，必须原样保留 |
| `KEEP_UPSTREAM_UNUSED` | SatCompute 不使用，但属于 ns-3 上游，必须原样保留 |

硬性行为边界：

- 不修改任务、传输、路由、队列、分包、完成条件或 registry 语义；
- Legacy CSV/TCP offered-load 模式属于已批准的功能范围收缩，不迁移其
  TCP OnOff 或 `offeredLoad` 能力；
- CSV 指标输出全部保留；
- failure 输出路径和所有 checker/CI 路径必须在同一提交迁移；
- Legacy 文档清理只删除退出范围的章节、命令和参数说明，不删除 README 文件；
- 每个实现提交都必须可构建、可运行、可验证。

## 2. ns-3 上游内容

| 路径 | 当前引用/来源 | 分类 | 处理 |
|---|---|---|---|
| SatCompute 当前构建依赖的 `src/**` 模块 | core、network、internet、applications、point-to-point、traffic-control、flow-monitor 等 | `KEEP_UPSTREAM` | 全部保留；不得修改核心模块 |
| 其他 `src/**` | ns-3 上游模块 | `KEEP_UPSTREAM_UNUSED` | 全部保留 |
| `src/openflow/**` | ns-3 上游可选模块，SatCompute 构建未启用 | `KEEP_UPSTREAM_UNUSED` | 保留；继续由 `--enable-modules=satcompute` 排除 |
| `src/core/helper/csv-reader.cc` | ns-3 core 通用 CSV reader；SatCompute legacy loader 未调用它 | `KEEP_UPSTREAM` | 属于 core 上游内容，原样保留 |
| `src/core/helper/csv-reader.h` | ns-3 core 通用 CSV reader；SatCompute legacy loader 未调用它 | `KEEP_UPSTREAM` | 属于 core 上游内容，原样保留 |
| `bindings/**` | ns-3 上游绑定生成内容 | `KEEP_UPSTREAM_UNUSED` | 全部保留 |
| `examples/**` | ns-3 上游示例 | `KEEP_UPSTREAM_UNUSED` | 全部保留 |
| `utils/**` | ns-3 上游工具 | `KEEP_UPSTREAM_UNUSED` | 全部保留 |
| `build-support/**` | ns-3 上游构建基础设施 | `KEEP_UPSTREAM` | 全部保留 |
| `test.py`、`waf`、根 `wscript` | ns-3 上游测试和构建入口 | `KEEP_UPSTREAM` | 全部保留 |
| `scratch/` | ns-3 上游 scratch 入口 | `KEEP_UPSTREAM_UNUSED` | 目录必须保留 |
| `scratch/subdir/scratch-simulator-subdir.cc` | ns-3 上游 scratch 示例；自仓库首个导入提交即存在 | `KEEP_UPSTREAM_UNUSED` | 原样保留，不作为 SatCompute 清理对象 |

本轮预计修改的 ns-3 上游文件数为零。

## 3. Archive 与 Scratch

| 路径 | 当前引用 | 唯一能力/后续价值 | 分类 | 建议处理 |
|---|---|---|---|---|
| `archive/legacy-code/ospf.py` | 无构建、代码、CI 或文档引用；调用仓库中已不存在的 `ospf` 程序 | 仅旧 60/108 星 OSPF 批处理，无 JSON/Hypatia 转换能力 | `DELETE` | 独立清理提交中删除 |
| `archive/legacy-code/runSim1.py` | 无构建、代码、CI 或文档引用；调用已不存在的 `link-test` | 仅旧 cluster/link-change 参数扫描 | `DELETE` | 独立清理提交中删除 |
| `archive/legacy-code/runSim2.py` | 无构建、代码、CI 或文档引用；调用已不存在的 `link-test` | 仅旧 cluster/link-change 参数扫描 | `DELETE` | 独立清理提交中删除 |
| `scratch/subdir/scratch-simulator-subdir.cc` | ns-3 scratch 构建示例 | 上游示例 | `KEEP_UPSTREAM_UNUSED` | 保留 |

`scratch/` 中没有 SatCompute 自定义文件可删除。

## 4. TOPOLOGY_ONLY 模式

Legacy background traffic 删除后，未提供 `TaskTrace` 和 `transferTrace` 的运行
继续作为正式 `TOPOLOGY_ONLY` 模式保留。它必须：

- 加载并应用全部成对的 `nodes_<time>s.json` 与
  `topology_<time>s.json`；
- 按快照启停 ISL、重算 stock `Ipv4GlobalRouting` 并推进 route epoch；
- 不安装 PacketSink、NetworkTransfer 或 TaskCoordinator；
- 正常生成基础网络指标和 `run-summary.json`；
- 保持 `sinkApplications=0`、`sentBytes=0`、`receivedBytes=0`。

66 星 / 132 ISL / 12 snapshot smoke、动态候选回归及后续 Hypatia 快照接入
均依赖该模式。`LEGACY_OFFERED_LOAD` 删除与 `TOPOLOGY_ONLY` 保留是两个独立
合同，不得混淆。

## 5. Legacy CSV/TCP offered-load 输入体系

### 5.1 能力差异与范围决策

仓库中不存在 SatCompute CSV topology、node、link、task 或 compute-profile
loader。唯一运行时 CSV 输入是 66 星 legacy traffic matrix。

JSON transfer/task 输入没有覆盖下列 legacy 能力：

- traffic matrix × `offeredLoad` 的倍率缩放；
- TCP `OnOff` background traffic；
- legacy UDP 的 100 秒聚合发送规则。

这些能力已被作者明确移出项目范围，因此不迁移到 JSON。删除依据是批准的
范围收缩，而不是“JSON 完全等价”。支持范围冻结为 JSON
NetworkTransfer/TaskTrace 的 UDP 传输。

### 5.2 文件、代码和参数审计

| 路径/符号 | 类型 | 当前引用 | 分类 | 建议处理 |
|---|---|---|---|---|
| `contrib/satcompute/input/traffic/csv/traffic_matrix(66).csv` | 唯一 CSV runtime fixture | 默认 `trafficMatrix`，仅 `offeredLoad>0` 时读取 | `DELETE` | legacy 范围收缩提交中删除 |
| `contrib/satcompute/traffic/background-traffic.cc` | CSV loader、UDP client、TCP OnOff、legacy sinks | wscript 编译；no-workload/legacy 分支调用 | `DELETE` | legacy 范围收缩提交中删除 |
| `contrib/satcompute/traffic/background-traffic.h` | legacy 应用接口 | `satcompute.cc` include/call | `DELETE` | 与实现一起删除 |
| `ReadTrafficMatrix` | SatCompute 私有手写 loader | 仅 `background-traffic.cc` 内部调用 | `DELETE` | 随文件删除；不影响上游 csv-reader |
| `SatComputeConfig::trafficMatrix` | legacy 配置 | `para.*`、CLI、background traffic | `DELETE` | 删除字段和默认值 |
| `SatComputeConfig::offeredLoad` | legacy 配置 | mode 选择、互斥校验、CLI；CI 大量显式传 0 | `DELETE` | 删除字段、校验和全部 `--offeredLoad=0` |
| `SatComputeConfig::transport` | legacy UDP/TCP 选择 | legacy app 和 NetworkTransfer UDP 校验 | `DELETE` | 删除字段与 CLI；受支持传输隐式固定为 UDP |
| `--trafficMatrix` | legacy CLI | 根 README、模块 README | `DELETE` | 删除参数和文档 |
| `--offeredLoad` | legacy CLI | 根 README、模块 README、JSON README、CI 命令 | `DELETE` | 独立提交中原子更新全部调用 |
| `--transport` | legacy CLI | 根 README、模块 README | `DELETE` | 删除参数和文档 |
| `ApplicationState`、legacy `InstallApplications`、legacy `CollectApplicationMetrics` | legacy runtime | 仅 no-workload/legacy 路径 | `DELETE` | no-workload 不再安装无业务 PacketSink |
| `contrib/satcompute/wscript` 的 `background-traffic.cc` 条目 | 构建引用 | 当前必编译 | `MIGRATE` | 在范围收缩提交中移除该源文件 |
| `README.md` 的 legacy 运行说明 | 文档 | 正 `offeredLoad` 示例和三项 CLI | `MIGRATE` | 删除退出范围的说明 |
| `contrib/satcompute/README.md` 的 legacy 章节 | 文档 | 说明矩阵、UDP/TCP、倍率 | `MIGRATE` | 删除并同步正式 JSON 输入边界 |
| `contrib/satcompute/input/traffic/README.md` | 输入目录文档 | 记录 CSV 来源和机械截取方法 | `MIGRATE` | 删除 CSV 章节与目录说明 |
| `contrib/satcompute/input/traffic/json/README.md` | JSON 输入文档 | 描述与 `offeredLoad` 互斥 | `MIGRATE` | 移除过期互斥说明 |
| `.github/workflows/satcompute-smoke.yml` | CI | 多个命令显式传 `--offeredLoad=0` | `MIGRATE` | 同一范围收缩提交中全部移除并运行 CI |

`ApplicationMetrics` 仍被 NetworkTransfer engine、task engine 和
`run-summary.json` 使用，不随 legacy background traffic 删除。

README 处理仅删除 legacy 专用章节、命令和 CLI 参数说明；根 README、模块
README、traffic README 和 JSON README 文件本身全部保留。

## 6. Metrics 源代码

当前 `metrics.cc` 已是 222 行的 orchestration；不进行整体重写。目录迁移和
职责拆分如下：

| 当前路径 | 当前职责 | 分类 | 目标/处理 |
|---|---|---|---|
| `metrics/metrics.h` | 公共聚合结构、RunMetadata、MetricsRecorder | `KEEP` | 保持 metrics 根目录 |
| `metrics/metrics.cc` | 编排所有 writer、决定诊断、打印输出 | `KEEP` | 保持根目录并精简输出 |
| `metrics/run-summary.h` | run-summary 接口 | `MIGRATE` | `metrics/core/run-summary.h` |
| `metrics/run-summary.cc` | run-summary writer | `MIGRATE` | `metrics/core/run-summary.cc` |
| `metrics/task-metrics.h` | task 聚合和 writer 接口 | `MIGRATE` | `metrics/core/task-metrics.h` |
| `metrics/task-metrics.cc` | task/event/compute CSV writer | `MIGRATE` | `metrics/core/task-metrics.cc` |
| `metrics/flow-metrics.h` | flow 聚合、基础 writer、DropReason writer | `MIGRATE` | 基础部分移到 `core/flow-metrics.*`；DropReason writer 移到 diagnostics |
| `metrics/flow-metrics.cc` | network metrics/details 及 DropReason CSV | `MIGRATE` | `WriteFlowDropReasons` 拆至 `diagnostics/flow-drop-reason-diagnostics.*` |
| `metrics/transfer-metrics.h` | transfer 与 ECMP writer | `MIGRATE` | transfer writer 移到 core；ECMP writer 移到 routing |
| `metrics/transfer-metrics.cc` | `transfer-summary.csv` 与 `ecmp-route-events.csv` | `MIGRATE` | 拆为 `core/transfer-metrics.*` 和 `routing/ecmp-metrics.*` |
| `metrics/ecmp-route-recorder.h` | runtime ECMP event recorder | `MIGRATE` | `metrics/routing/ecmp-route-recorder.h` |
| `metrics/ecmp-route-recorder.cc` | runtime ECMP event recorder | `MIGRATE` | `metrics/routing/ecmp-route-recorder.cc` |
| `metrics/size-aware-metrics.h` | reservation metrics 接口 | `MIGRATE` | `metrics/routing/size-aware-metrics.h` |
| `metrics/size-aware-metrics.cc` | reservation CSV/JSON writer | `MIGRATE` | `metrics/routing/size-aware-metrics.cc` |
| `metrics/failure-diagnostics.h` | 完整 task failure diagnostics 接口 | `MIGRATE` | `metrics/diagnostics/failure-diagnostics.h` |
| `metrics/failure-diagnostics.cc` | incomplete、queue、UDP、flow-link、summary | `MIGRATE` | `metrics/diagnostics/failure-diagnostics.cc` |

`GetIpv4DropReasonCount/Name` 同时被 core `run-summary` 使用，因此保留在
core flow metrics；只移动写出 `flow-drop-reasons.csv` 的函数。

## 7. Tools

| 当前路径 | 当前用途/引用 | 分类 | 目标 |
|---|---|---|---|
| `tools/check-task-output.py` | task、failure、stress、CI checker | `MIGRATE` | `tools/validation/check-task-output.py` |
| `tools/check-ecmp-output.py` | Hash/HRW/transfer checker，CI 使用 | `MIGRATE` | `tools/validation/check-ecmp-output.py` |
| `tools/check-size-aware-output.py` | lifecycle、中型、75% checker | `MIGRATE` | `tools/validation/check-size-aware-output.py` |
| `tools/check-size-aware-replay.py` | N1 replay checker | `MIGRATE` | `tools/validation/check-size-aware-replay.py` |
| `tools/check-flow-drop-reasons.py` | FqCoDel/FlowMonitor checker，CI 使用 | `MIGRATE` | `tools/validation/check-flow-drop-reasons.py` |
| `tools/preflight-task-workload.py` | 输入与派生包/packet-hop 预检，CI 使用 | `MIGRATE` | `tools/validation/preflight-task-workload.py` |
| `tools/generate-stress-topology.py` | 生成 JSON 压力 topology | `MIGRATE` | `tools/generation/generate-stress-topology.py` |
| `tools/generate-task-workload.py` | 生成确定性 TaskTrace JSON | `MIGRATE` | `tools/generation/generate-task-workload.py` |
| `tools/generate-transfer-workload.py` | 生成确定性 transfer JSON | `MIGRATE` | `tools/generation/generate-transfer-workload.py` |

当前没有可迁移的 conversion 或 maintenance 脚本。不创建无法由 Git 跟踪的
空目录；Hypatia 转换工具出现后再建立 `tools/conversion/`。

## 8. Docs

| 当前路径 | 当前用途 | 分类 | 目标/处理 |
|---|---|---|---|
| `docs/n1-6-stress-validation-review.md` | N1.6 正式压力审查 | `MIGRATE` | `docs/reviews/n1-6-stress-validation-review.md` |
| `docs/pre-n2-size-aware-hrw-validation.md` | Pre-N2 路由验证 | `MIGRATE` | `docs/reviews/pre-n2-size-aware-hrw-validation.md` |
| `docs/dev-setup.md` | 开发环境说明 | `KEEP` | 保持 `docs/` 根目录 |
| `docs/reviews/pre-n2-code-cleanup-audit.md` | 本审计表 | `KEEP` | 作为删除与迁移依据 |
| 根 `README.md` | 项目入口 | `KEEP` | 更新 legacy、tools 和 review 路径 |
| `contrib/satcompute/README.md` | 模块运行说明 | `KEEP` | 更新输入、输出和工具路径 |

## 9. 输出合同

### 9.1 保持 outputDir 根目录

以下常用或路由输出全部 `KEEP`，文件名和 schema 不变：

```text
run-summary.json
network-flow-metrics.csv
network-flow-details.csv
ecmp-route-events.csv
transfer-summary.csv
task-summary.csv
task-events.csv
compute-node-summary.csv
size-aware-reservation-events.csv
size-aware-summary.json
```

### 9.2 迁移到 diagnostics/failure

以下文件全部 `MIGRATE`，不删除：

```text
incomplete-tasks.csv
incomplete-transfers.csv
flow-drop-reasons.csv
isl-queue-drops.csv
isl-queue-drop-summary.csv
udp-socket-drops.csv
udp-socket-drop-summary.csv
flow-link-concentration.csv
diagnostic-summary.json
```

目标统一为：

```text
<outputDir>/diagnostics/failure/<filename>
```

路径迁移、旧文件清理、所有 Python checker、CI 和 README 命令必须在同一
原子提交完成。清理只删除已知诊断文件，并仅在空目录时移除目录，不递归
删除未知用户文件。

诊断行为冻结为：

| 模式 | 结果 | failure 目录 |
|---|---|---|
| 任意运行，`diagnosticMode=off` | 任意 | 清理旧目录，不生成 |
| task + `diagnosticMode=failure` | COMPLETE | 清理旧目录，不生成 |
| task + `diagnosticMode=failure` | PARTIAL/strict incomplete | 生成完整失败诊断 |
| transfer-only + `diagnosticMode=failure` | 无 TaskCoordinator | 生成 `flow-drop-reasons.csv`；供 replay/FqCoDel fixture 使用 |

transfer-only 写出前应清理同目录内可能残留的其他 task failure 文件，避免
复用 outputDir 时混入旧结果。

### 9.3 outputDir 默认策略

- 默认开发输出改为 `/tmp/satcompute-output`；
- CI 和本地回归继续为每个场景显式使用独立的
  `/tmp/satcompute-<case>`；
- 需要跨重启保留的正式实验结果必须通过 `--outputDir` 显式写入 Git 仓库外
  的持久目录；
- `contrib/satcompute/output/` 不再作为默认目录，避免 ignored 旧结果与
  新运行混合。

`/tmp` 内容可能在重启或系统清理后消失，因此不承担正式结果归档。

## 10. 引用更新与原子提交约束

必须更新但不删除的入口：

- `contrib/satcompute/wscript`：metrics 移动和 legacy source 删除；
- `contrib/satcompute/satcompute.cc`：include、legacy CLI/mode 删除；
- `.github/workflows/satcompute-smoke.yml`：tools、diagnostics 和
  `offeredLoad` 路径；
- 根 README、模块 README、input README；
- 所有 checker 内部的 failure 路径常量。

建议提交顺序：

1. `chore: audit legacy SatCompute files and input pipelines`
2. `refactor: organize core routing and diagnostic metrics`
3. `refactor: organize validation tools and review documents`
4. `refactor: place failure diagnostics under output subdirectory`
   （runtime、checker、CI、文档原子更新）
5. `chore: remove confirmed obsolete archive code`
6. `chore: retire legacy CSV offered-load mode`
   （独立记录已批准的功能范围收缩）
7. `refactor: simplify default SatCompute runtime output`
8. `test: preserve SatCompute behavior after code cleanup`

每个提交后至少执行与该切片相关的构建和小型检查；不重新运行完整 75% 或
109 GB。

## 11. 审计汇总

```text
KEEP:
  metrics orchestration
  core/routing/task/transfer output contracts
  ApplicationMetrics for JSON NetworkTransfer/task
  TOPOLOGY_ONLY
  development documentation

KEEP_UPSTREAM:
  SatCompute 构建依赖的 ns-3 模块和构建基础设施

KEEP_UPSTREAM_UNUSED:
  SatCompute 未启用的 ns-3 modules、examples、bindings 和 scratch 示例

MIGRATE:
  metrics source layout
  validation/generation tools
  formal review documents
  failure diagnostic output paths
  references in wscript, CI and documentation

DELETE:
  archive/legacy-code/{ospf.py,runSim1.py,runSim2.py}
  SatCompute legacy traffic CSV
  background-traffic runtime
  trafficMatrix/offeredLoad/transport legacy configuration and CLI
  legacy-only tests and documentation

UNKNOWN:
  none in the audited candidate scope
```

任何未列入本表的新删除候选必须先补充证据并重新取得确认。
