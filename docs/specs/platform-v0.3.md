# 规格：SCP-SatComPlate ns-3.48 兼容迁移 v0.3

状态：已批准，作为后续迁移的唯一有效平台规格。

v0.2 以完整 scenario JSON 为唯一运行输入，并对 SatCompute 进行了较大幅度的
目录与接口重构。该方向已经被否决。v0.3 以 ns-3.33 版本的项目形态和行为合同
为迁移基线，仅在 ns-3.48 构建、原生轨道计算和必要适配处引入差异。

## 目标

在官方 ns-3.48 上交付可长期开发的 SatCompute 平台，同时满足：

1. `legacy/ns-3.33` 与 `main` 中的 `contrib/satcompute` 可以按目录、入口、
   接口、输入、指标和测试逐项对应；
2. 保留 ns-3.33 已有的 IPv4、逐流 ECMP、HRW、size-aware、capacity-aware、
   NetworkTransfer、任务计算和指标语义；
3. 使用 ns-3.48 原生圆轨道模型实时计算稳定卫星 ID 对应的 ECEF 坐标；
4. 实时拓扑和离线拓扑切片调用同一套轨道、候选链路、距离门控和时延实现；
5. 为未来的位置相关故障建模和前端状态输出保留确定性状态边界，但不在本阶段
   实现故障执行、前后端传输、IPv6 或 SRv6。

## 技术栈与版本

- 仿真器：官方 ns-3.48；
- 项目位置：`contrib/satcompute/`；
- 构建系统：ns-3.48 CMake，通过 `./ns3` 包装器调用；
- 主要实现：C++；
- 输入、切片和结构化输出：JSON/CSV；
- JSON 解析：仓库内固定版本的 nlohmann JSON 单头文件；
- 外部生成、检查和可视化工具：Python，仅位于 `tools/`；
- 版本控制：`main` 为 ns-3.48 主线，`legacy/ns-3.33` 为永久只读参考分支。

## 构建、运行和验证命令

配置与构建不启用 ns-3 全局 examples 或 tests：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
```

平台入口：

```bash
./ns3 run "satcompute"
./ns3 run "satcompute --help"
```

项目自有验证保持在既有目录：

```bash
python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

GitHub Actions 只允许手动触发 SatCompute CI。阶段内的小 PR 使用本地聚焦验证；
每个大阶段全部合并后，在 `main` 上手动运行一次 CI。CI 不运行 `test.py`、
ns-3 examples 或上游全局测试。

## 目标目录

```text
contrib/satcompute/
├── CMakeLists.txt
├── README.md
├── para.h
├── para.cc
├── satcompute.cc
├── metrics/
│   ├── metrics.cc
│   ├── metrics.h
│   ├── core/
│   ├── diagnostics/
│   └── routing/
├── routing/
├── task/
├── topology/
│   ├── satellite-topology.cc
│   ├── satellite-topology.h
│   ├── snapshot/
│   ├── link/
│   ├── orbit/
│   ├── online/
│   ├── replay/
│   └── export/
├── traffic/
├── tools/
│   ├── generation/topology/
│   ├── generation/scenario/
│   ├── analysis/
│   ├── validation/
│   └── visualization/
├── input/
│   ├── topology/constellations/
│   ├── topology/examples/
│   ├── topology/resources/
│   └── traffic/
├── tests/
└── third-party/
```

与 ns-3.33 相比允许的结构差异仅限：

- `CMakeLists.txt` 替代 `wscript`；
- `topology/orbit`、`topology/online`、`topology/replay` 和
  `topology/export` 保存 ns-3.48 原生在线计算所需实现；
- Hypatia/TLE 生成后端不进入 v0.3 主线；
- 为 JSON 合同增加的 schema 与共享 manifest 可以与所属数据类型同目录保存。

`app/` 不保留。平台入口固定为 `contrib/satcompute/satcompute.cc`。迁移组件不以
通用 `model/` 目录重新分类；新增文件应按原有职责放入 topology、traffic、task、
routing 或 metrics。

## 配置与输入合同

### 平台参数：`para.h` 和 `para.cc`

`para.h` 定义有类型的 `SatComputeConfig`；`para.cc` 用中文注释集中提供默认值；
平台入口使用 ns-3 `CommandLine` 提供同名覆盖。以下参数只属于该层：

- 仿真开始时间和总时长；
- 网络更新时间、拓扑导出切片间隔；
- 星座结构文件路径、online/replay 模式和 replay 目录；
- 候选 ISL 策略、seam、最大 ISL 距离；
- fixed/distance 时延模式与 fixed 时延；
- 链路带宽、MTU、队列和接收缓冲区；
- 路由模式、重算策略和 ECMP hash seed；
- transfer、compute profile、task trace 路径与任务/分包策略；
- ns-3 seed、run、预留 stream 起点；
- 输出目录、日志和诊断模式。

人类直接设置的仿真时间和间隔以秒表示，启动时统一转换为 ns-3 `Time` 或精确
整数纳秒。工作负载数据已有的纳秒事件字段保持 ns-3.33 合同，不在本迁移中
擅自改写。

### 星座结构 JSON

星座文件只回答“这是什么星座”，位于
`input/topology/constellations/`，至少包含：

- schema version 和稳定星座名称；
- Walker Star/Delta 结构；
- 轨道面数和每面卫星数；
- 高度、倾角和相位规则；
- 轨道 epoch 或初始相位。

星座文件不得包含仿真时长、切片间隔、更新间隔、距离阈值、时延模式、路由、
带宽、随机种子、任务路径或输出目录。出现这些字段必须校验失败，不能由
`para.cc` 静默覆盖。

### 独立数据 JSON

以下内容继续作为独立数据输入，不写入 `para.cc`：

- `nodes_<time>s.json` 与 `topology_<time>s.json` 拓扑切片；
- ComputeProfile 算力资源；
- TaskTrace 任务输入；
- NetworkTransfer 流量输入；
- 未来的故障/修复事件输入。

生成器专用配置只能放在 `tools/generation/*/config/`，只描述生成策略，不作为
平台完整运行配置。原 `input/examples/synthetic-66-fixed.json`、
`synthetic-66-distance.json`、scenario schema、`--scenarioConfig` 和对应完整配置
加载器已随阶段 2 配置切换删除，不保留双输入路径。

### 有效配置和可复现性

每次运行仍输出只读的 `effective-config.json` 和输入 manifest。它们记录：

- 解析后的全部 `para/CLI` 值；
- 星座结构文件的规范路径和 SHA-256；
- 实际读取的 topology、transfer、compute、task 输入路径与 SHA-256；
- seed、run、stream 起点；
- 软件版本和运行模式。

该文件是运行证据，不是下一次运行的配置入口。

## 轨道、拓扑和时间语义

1. ns-3.48 原生圆轨道 mobility 是 online 模式的唯一位置计算核心；
2. 外部卫星 ID 使用稳定的 plane-major 映射，不依赖 `Node::GetId()`；
3. ISL 使用固定、可审计的候选卫星身份，不在每次更新时改选最近异轨卫星；
4. 候选链路只有超过最大距离或受未来故障覆盖时才中断；
5. fixed 和 distance 是同一星座上的两种网络实验，不复制星座文件；
6. `networkUpdateIntervalSeconds` 决定在线网络状态刷新频率；
7. `topologyExportIntervalSeconds` 独立决定 JSON 切片精度；
8. 每个网络 tick 可以更新 distance 时延，但只有有效链路集合变化时才重算
   当前 hop-based IPv4 路由；
9. 未来故障事件在其精确纳秒时刻立即修改有效拓扑并重算路由，不等待周期 tick。

online 运行和拓扑生成工具必须共享同一个 C++ 轨道、候选链路、距离门控和时延
实现。Python 工具不得复制轨道传播公式。

## 路由、流量、任务和算力

v0.3 必须保留以下 IPv4 模式及其确定性语义：

- `global-first`；
- `global-hash-per-flow`；
- `global-hrw-per-flow`；
- `global-size-aware-hrw`；
- `global-capacity-aware-hrw`。

固定五元组、候选集合、输入和 seed 时，hash/HRW 结果必须与 ns-3.33 黄金结果
一致。size-aware 与 capacity-aware 虽然依赖活动传输状态，但固定任务输入和
canonical 同时事件顺序时必须可复现。

ComputeProfile 和 TaskTrace 继续分离。节点算力作为 JSON 数据保存在 input 中，
而其路径、任务完成策略和日志模式属于 `para.cc`。任务闭环继续使用输入传输、
FCFS 非抢占计算、结果传输和最终完成状态。

## 指标与诊断

`metrics/` 的目录和职责恢复为 ns-3.33 形态：

- `core/flow-metrics`：FlowMonitor 安装、流级和聚合网络指标；
- `core/transfer-metrics`：NetworkTransfer 结果；
- `core/task-metrics`：任务和计算节点结果；
- `core/run-summary`：运行摘要和实际策略；
- `routing/`：ECMP、HRW、size-aware、capacity-aware 路由证据；
- `diagnostics/`：失败对象、drop reason、队列和 socket 诊断；
- `metrics.*`：统一编排，不把所有实现重新合并成一个 writer。

新增的轨道坐标、拓扑切片和 manifest 输出可以保留，但不得替代旧指标合同。

## 代码风格

- 遵循 ns-3.48 `.clang-format` 和项目既有 GNU C++ 风格；
- C++ public 类型和接口使用 Doxygen；
- C++ 注释和 Doxygen 不使用 Unicode 数学符号或箭头；
- 迁移优先保留 ns-3.33 文件名、类名和调用层次；
- 只在 ns-3.48 API 或原生轨道能力确实需要时增加适配层；
- 不以“顺手清理”为由重构未涉及模块。

示例：

```cpp
SatComputeConfig config = GetDefaultSatComputeConfig();
AddSatComputeCommandLineOptions(commandLine, config);
commandLine.Parse(argc, argv);
const ResolvedSatComputeConfig resolved = ResolveSatComputeConfig(config);
```

## 测试策略

每个小增量在本地运行其聚焦测试，并保持模块可编译。大阶段检查点运行完整的：

1. Python 合同测试；
2. C++ 项目单元测试；
3. smoke；
4. regression；
5. 一次手动 GitHub `SatCompute CI`。

必须恢复并维护：

- legacy snapshot、traffic、task、compute fixtures；
- 4-node diamond 静态/动态路由黄金结果；
- Hash、HRW、size-aware、capacity-aware 确定性；
- FCFS、异构算力和任务完成策略；
- 66 星 online 构造；
- 1/2 秒导出与 20 秒应用时的共同时间点等价性；
- online 导出、JSON replay 与 manifest 完整性。

## 边界

### 始终执行

- 迁移前读取相应 legacy 文件、当前实现和测试；
- 每个改动保持 `main` 可构建；
- 所有 ID、候选和同时间事件使用 canonical 顺序；
- 每个 PR 只处理一个可验证责任；
- PR 合并后确认其 head 可达 `main`，再删除本地和远端分支；
- 每个大阶段只运行一次 GitHub CI。

### 必须先确认

- 修改独立数据 JSON 的字段合同；
- 修改已有 IPv4 hash 字节布局或黄金结果；
- 引入新第三方依赖；
- 修改上游 `src/`；
- 扩大到故障执行、前端协议、IPv6 或 SRv6。

### 禁止

- 将 `legacy/ns-3.33` 合并进 `main`；
- 同时保留完整 scenario JSON 与 `para.cc` 两个运行配置源；
- 在 Python 中复制 ns-3.48 轨道计算；
- 将入口放入 `app/` 或把迁移组件重新聚合到通用 `model/`；
- 在 GitHub CI 中启用 ns-3 examples、全局 tests 或运行 `test.py`；
- 为通过测试而删除或弱化 legacy 行为检查。

## 完成标准

v0.3 只有同时满足以下条件才完成：

1. 最终目录通过迁移矩阵审计，所有 legacy 文件都有保留、适配、替代或明确删除
   结论；
2. 平台使用根目录 `satcompute.cc` 和 `para.cc + CLI`；
3. 完整 scenario JSON 输入和加载链路完全移除；
4. 中文 README 以 ns-3.33 版本为主体并准确描述 ns-3.48 差异；
5. legacy 输入、工具、指标和测试合同恢复；
6. 五种 IPv4 路由与任务闭环通过确定性回归；
7. online、export 和 replay 在共同时间点输出等价状态；
8. 每个大阶段的 SatCompute CI 成功且没有运行上游 examples/tests；
9. 工作树干净，只保留 `main` 和永久 `legacy/ns-3.33` 开发基线分支。

## 已延期范围

- 卫星或链路故障的生成与执行；
- 后端到前端的实时状态传输协议；
- IPv6 和 SRv6；
- 地面站、馈电链路；
- SGP4/TLE 和非圆轨道 provider。

这些范围必须以独立规格和后续 PR 开始，不能在 v0.3 迁移过程中预实现。
