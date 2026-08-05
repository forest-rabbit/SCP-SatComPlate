# 实施计划：SatCompute ns-3.48 legacy-parity 迁移

状态：已批准，阶段 0 至阶段 4 已完成；阶段 4 唯一一次 GitHub CI run
`30974934528` 已通过，下一步进入阶段 5 的 routing、traffic 和 task 对应迁移。

依据：[平台 v0.3 规格](../specs/platform-v0.3.md)。

## 总体方法

迁移同时使用两条只读基线：

- `legacy/ns-3.33`：目录、公共接口、输入、指标、工具和行为合同；
- 当前 `main`：已经通过 ns-3.48 编译的路由、任务、流量、原生轨道、在线拓扑、
  回放和导出实现。

不把两个 Git 历史 merge。每次只读取相应文件，将目标结构和必要实现通过小分支
迁入 `main`。优先采用兼容 facade 和文件移动，避免重新实现已经验证的算法。

## 分支与验证策略

每个任务使用一个短期 `docs/*`、`refactor/*`、`migration/*` 或 `feature/*`
分支。流程固定为：

```text
main -> 小分支 -> 聚焦本地验证 -> PR -> merge -> prune/delete -> main
```

阶段内部不运行 GitHub CI。每个大阶段完成、所有相关 PR 合并后，只在 `main`
手动触发一次 `SatCompute CI`。本地聚焦测试不受该限制。

## 阶段 0：规格与基线

### 任务 0.1：发布 v0.3 规格

验收：

- v0.3 明确 legacy-parity、配置边界、目录、行为和延期范围；
- v0.2 被标记为已废止；
- `AGENTS.md` 不再要求完整 scenario JSON；
- CI 改为仅手动触发。

验证：

```bash
git diff --check
git status --short --branch
```

### 任务 0.2：发布迁移矩阵

验收：

- legacy 的每个主要目录和公共入口都有最终动作；
- 当前新增的 online/replay/export 文件有明确归属；
- 删除项只限被 v0.3 明确替代的完整 scenario 配置链路和 Hypatia 后端。

### 阶段 0 检查点

- 规格、计划、矩阵和规则已合并；
- 在 `main` 手动运行一次 `SatCompute CI`；
- CI 不包含 ns-3 examples、全局 tests 或 `test.py`。

## 阶段 1：目录、入口和默认参数外形

### 任务 1.1：恢复根目录入口（已完成）

动作：

- `app/satcompute.cc` 移到 `satcompute.cc`；
- 更新 `CMakeLists.txt` 的 executable source；
- 保持当前运行行为不变。

验收：模块构建且 `./ns3 run "satcompute --help"` 成功。

### 任务 1.2：恢复 `para.h/para.cc`（已完成）

动作：

- 恢复 legacy `SatComputeConfig` 和 `GetDefaultSatComputeConfig()` 形态；
- 增加 v0.3 所需字段，但暂不切换生产入口；
- 所有默认值在 `para.cc` 使用中文注释；
- 增加集中 CLI 注册和基础参数校验测试。

验收：默认值、CLI 覆盖和秒到 ns 转换可独立测试。

### 任务 1.3：恢复中文 README 基线（已完成）

动作：以 legacy README 的章节和语义为底稿，先更新构建方式与“迁移中”边界，
后续阶段随合同落地更新具体命令。

### 阶段 1 检查点

- 入口和 para 路径与 legacy 对应；
- 当前 scenario 路径仍是唯一活动路径，新增 para 尚未形成第二活动配置源；
- 完整本地项目验证通过；
- `main` 手动运行一次 SatCompute CI。

实现结果（2026-08-05）：

- PR #27 将平台入口恢复到模块根目录；
- PR #28 和 #29 恢复 typed defaults、集中 CLI 注册、组合校验及秒到纳秒转换；
- PR #30 以 ns-3.33 中文 README 为主体恢复运行说明；
- 本地 configure/build、26 项 Python 合同测试、17 组 C++ 测试、smoke 和
  regression 全部通过；
- production 入口仍只读取 scenario 0.2，`para` 尚未接入，不存在双活动配置源；
- 本阶段唯一一次 GitHub CI 已从 `main` 手动触发并通过。

## 阶段 2：配置合同切换

### 任务 2.1：增加星座结构合同

动作：

- 新增只包含星座物理结构的 JSON schema、读取器和 `synthetic-66.json`；
- 拒绝仿真时长、切片、距离门控、时延、路由和 workload 字段；
- 路径从 `para.cc` 提供。

### 任务 2.2：建立 resolved config

动作：

- 将 `SatComputeConfig + ConstellationDefinition` 一次解析为内部纳秒配置；
- 为现有 online、replay、export、metrics 提供只读 resolved view；
- 输出 effective config 和输入 hash。

### 任务 2.3：切换入口并删除完整 scenario 输入

动作：

- `satcompute.cc` 改用 `para.cc + CLI`；
- 切换所有生产消费者；
- 删除 `--scenarioConfig`、scenario schema/loader、Python 完整配置验证器和
  `input/examples/synthetic-66-{fixed,distance}.json`；
- 同步测试和文档。

验收：生产代码中不存在 `ScenarioConfig` 或 `scenarioConfig` 引用。

### 阶段 2 检查点

- fixed、distance、online 和 replay 均通过 para/CLI 运行；
- 无双配置源；
- 大阶段本地回归通过；
- `main` 手动运行一次 SatCompute CI。

实现结果（2026-08-05）：

- 星座 JSON 已缩减为只含物理结构的 closed-world 合同，默认 66 星输入已迁移；
- `SatComputeConfig + ConstellationDefinition` 统一解析为只读 resolved config，
  online、replay、export、metrics 和有效配置均使用该视图；
- 根入口已切换为 `para.cc + CLI`，完整 scenario schema、加载器、示例和 fixture
  已删除，生产代码不再存在双配置源；
- fixed、distance、online、replay、正常导出和 export-only 已由本地 smoke 与
  regression 覆盖；本阶段唯一一次 GitHub CI run `30971563245` 已通过。

## 阶段 3：拓扑外观与共享核心

### 任务 3.1：恢复 `SatelliteTopology` facade（已完成）

动作：恢复 legacy 公共入口和调用层次，并在内部委托 current online/replay
controller、地址管理和 link state。

### 任务 3.2：统一 online、export 和 replay（已完成）

动作：

- 三条路径共享轨道、candidate、distance gate 和 delay；
- 统一稳定卫星 ID 与拓扑 epoch；
- 保持 network cadence 与 export cadence 独立；
- 链路集合不变时不重算路由。

### 任务 3.3：恢复拓扑 wrapper 相关测试（已完成）

验收：legacy replay、66 星 online、1/2 秒与 20 秒共同时间点等价。

### 阶段 3 检查点

- 拓扑公共接口与 legacy 对应；
- 完整本地 topology/routing smoke 通过；
- `main` 手动运行一次 SatCompute CI。

实现结果（2026-08-05）：

- PR #40 恢复 `topology/satellite-topology.h/.cc`，平台入口、流量和任务只面向
  facade；online/replay controller、稳定 ID、IPv4 地址与 link state 均由其委托；
- online controller 与 exporter 共用 `OnlineOrbitConstellation` 和
  `CircularOrbitTopologyPolicy`，replay 通过权威 manifest 消费相同 JSON 状态；
- network cadence 与 export cadence 保持独立，controller 只在有效边集合变化时
  重算路由，distance 时延或带宽单独变化不会推进 route epoch；
- facade、online、replay、切片导出和回放测试覆盖稳定 ID、固定候选、链路恢复、
  66 星 online，以及 1 秒/2 秒导出和 20 秒网络更新在 0/20/40 秒的等价性；
- 本阶段完整本地验证通过；唯一一次 GitHub CI run `30972203958` 已从 `main`
  手动触发并通过。

## 阶段 4：input 与 tools

### 任务 4.1：恢复 legacy input（已完成）

恢复 topology examples、compute profiles、traffic workloads 和 README；保持提交的
JSON 内容和稳定 ID，不把运行结果提交到仓库。

实现结果（2026-08-05）：

- 33 个 legacy input 文件按原 blob 恢复，包括 66 星静态/10 秒 replay 快照、
  22/66 节点 ComputeProfile、5000 条 varied transfer 与 10 条本地大流输入；
- 新增快速合同测试只解析大型 workload，不在 CI 执行完整压力仿真；
- 真实 66 星 replay 平台运行应用 0/10/20 秒三个切片，链路集合不变时只进行
  一次初始路由计算。

### 任务 4.2：恢复 analysis、validation 和 visualization

优先恢复仍适用于当前输出合同的工具；对路径变化做最小适配，并恢复其单元测试。

实现结果（2026-08-05）：

- PR #43 恢复 7 个 legacy validation CLI；它们当前可读和可显示帮助，阶段 6
  在分层 metrics 恢复后接回黄金输出检查；
- PR #46 恢复 topology interval 的边状态、连通性和无权 ECMP 候选分析，将输入
  从完整 scenario 改为 v0.3 topology trace，并以逐字节复制方式降采样；
- PR #47 恢复三维查看器、交互播放和可选 GIF 结构，位置只读取 manifest 列出的
  ECEF XYZ；默认关闭路径不读取 trace 或导入可选图形依赖；
- 旧 C++ route audit 随阶段 5 的 routing 公共接口接回；依赖 Hypatia 和旧
  fixed-delay 结论的冻结报告、PNG 不迁移。

### 任务 4.3：重建 topology generator

保留 `tools/generation/topology/` 边界，但删除 Hypatia vendor/adapter。拓扑生成器
作为链接 `libsatcompute` 的 C++ executable 调用共享 ns-3.48 轨道核心；Python
只做命令编排和结果检查。

实现结果（2026-08-05）：

- PR #45 新增链接 `libsatcompute` 的 `satcompute-topology-generator`，直接复用
  `OnlineOrbitConstellation`、拓扑 policy 与 `CircularOrbitTraceExporter`；
- Python checker 只检查 closed-world manifest、稳定 ID、配对切片和 SHA-256，
  不含轨道公式；
- 平台 `--exportOnly=true` 与独立生成器在相同参数下的全部输出逐字节相同。

### 任务 4.4：恢复 scenario generator

`tools/generation/scenario/` 只组合 compute、task、traffic 和未来 fault 输入，
通过拓扑 manifest 获取卫星 ID，不重新计算轨道。

实现结果（2026-08-05）：

- PR #48 将旧入口重建为独立 input bundle 组合器，不恢复完整 scenario 配置；
- task 模式严格组合 ComputeProfile + TaskTrace，transfer 模式只组合
  NetworkTransfer，两种模式互斥并与 para 校验一致；
- 数据文件逐字节复制，bundle manifest 记录 topology/constellation 哈希、稳定
  satellite ID、条目数和输入哈希；future `fault_trace` 明确保留为 `null`；
- 真实 C++ topology trace 生成 task/transfer bundle 后，两种 bundle 均被平台
  直接读取并完成仿真。

### 阶段 4 检查点

- legacy input 和工具目录恢复；
- online 与离线 generator 输出等价；
- 大阶段本地工具测试通过；
- `main` 手动运行一次 SatCompute CI。

实现结果（2026-08-05）：

- PR #42 恢复 33 个 legacy input 文件，PR #44 恢复确定性 task/transfer workload
  生成器，PR #43、#45、#46、#47、#48 分别完成 validation、共享拓扑生成、
  interval analysis、trace visualization 和 input bundle；
- SatCompute-only 完整构建通过，配置明确显示 `Examples: OFF`、`Tests: OFF`；
- 61 个项目 Python 测试、18 组 C++ 可执行测试、smoke 和 regression 全部通过；
- 额外本地门禁覆盖真实 66 星 headless XYZ/ISL 渲染、真实 trace 降采样分析，以及
  C++ generator -> input bundle -> C++ 平台的 task/transfer 两条链路；
- 阶段 4 唯一一次 GitHub CI run `30974934528` 从 `main` 手动触发并通过，工作流
  未启用或运行 ns-3 examples、全局 tests 或 `test.py`。

## 阶段 5：routing、traffic 和 task 对应

### 任务 5.1：恢复 routing factory 与 legacy include 边界

保留当前 ns-3.48 算法实现，恢复 legacy `routing-policy-factory.*` 和公共调用路径。

### 任务 5.2：恢复 `traffic/network-transfer.*`

用 wrapper 重新提供 legacy 安装/收集入口，内部继续使用当前 engine、application
和 receiver。

### 任务 5.3：审计 task/traffic 数据与事件顺序

恢复 legacy fixtures 和黄金结果，验证 canonical 输入顺序、五元组、分包、FCFS、
异构算力、strict/report 和动态拓扑行为。

阶段 5 行为门禁映射：

- `legacy-workload-manifest.json` 固定 54 个 legacy fixture 的路径与 SHA-256；
- `legacy-workload-parity-test.cc` 直接读取旧 ComputeProfile、TaskTrace、
  NetworkTransfer 和静态拓扑，验证 canonical 顺序、派生 transfer ID、IPv4/UDP
  五元组、分包、精确服务时长、FCFS 与异构算力；
- 同一 parity test 将旧 12-transfer 输入运行在规则 0/2/4s 动态切片上，验证仅链路
  集合变化时发生三次路由计算；
- 当前 `task-*`、`network-transfer-*`、五种 routing C++ 测试继续作为 ns-3.48
  行为门禁；统一 regression 负责平台 strict/report 与 online/replay 重复性。

### 阶段 5 检查点

- 五种 IPv4 模式和任务闭环通过 legacy 黄金回归；
- 大阶段本地完整业务回归通过；
- `main` 手动运行一次 SatCompute CI。

## 阶段 6：metrics 与诊断

### 任务 6.1：恢复 core metrics

恢复 FlowMonitor、网络汇总、transfer、task、compute node 和 run summary 文件。

### 任务 6.2：恢复 routing metrics

恢复 route recorder、ECMP、size-aware 和 capacity-aware 独立输出。

### 任务 6.3：恢复 diagnostics

恢复 incomplete object、drop reason、ISL queue 和 UDP socket 诊断。

### 任务 6.4：移除聚合 writer

所有等价输出迁入分层实现后删除 `run-output-writer.*`，由 `MetricsRecorder` 统一
编排。

### 阶段 6 检查点

- legacy 指标文件和字段通过检查器；
- 新 topology/effective-config 证据仍保留；
- 大阶段本地完整指标回归通过；
- `main` 手动运行一次 SatCompute CI。

## 阶段 7：完整回归与收尾

### 任务 7.1：恢复完整测试层次

合并 legacy fixtures、support、unit、smoke、regression 与当前 ns-3.48 新测试，
消除重复但不删除行为覆盖。

### 任务 7.2：完成中文 README

以 legacy README 为主体，更新所有 ns-3.48 命令、para 参数、星座结构、在线/
回放/导出、指标和验证说明。

### 任务 7.3：完成矩阵审计

逐项确认 legacy 文件的最终动作，更新 v0.3 状态与延期边界。

### 最终检查点

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
git diff --check
git status --short --branch
```

随后在 `main` 手动运行一次 SatCompute CI，确认成功后标记 v0.3 完成并清理所有
非永久开发分支。

## 主要风险与控制

| 风险 | 影响 | 控制方式 |
|---|---|---|
| 直接复制 legacy 代码破坏 ns-3.48 API | 高 | 优先 wrapper/facade，每次先读 current 与 legacy 对应文件 |
| para 与星座 JSON 重复字段 | 高 | closed-world schema；重复字段直接失败 |
| metrics 恢复时丢失当前新增证据 | 高 | 先建立输出文件/字段矩阵，再拆 writer |
| current 路由重构改变 legacy hash | 高 | 使用 legacy 黄金 fixture 逐模式比较 |
| 工具复制轨道公式 | 高 | 生成器链接共享 C++ core，Python 只编排 |
| 阶段内 PR 频繁消耗 CI | 中 | workflow 仅 `workflow_dispatch`，每阶段手动一次 |
| 大范围目录恢复造成不可审阅 diff | 中 | 文件移动、wrapper、数据恢复分开提交和 PR |

## 开放问题

当前没有阻塞实施的开放问题。故障、前端、IPv6/SRv6 和非圆轨道需要未来独立
规格，不属于本计划。
