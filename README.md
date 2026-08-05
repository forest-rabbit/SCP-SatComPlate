# SCP-SatComPlate

[![SatCompute CI](https://github.com/forest-rabbit/SCP-SatComPlate/actions/workflows/phase_gate.yml/badge.svg)](https://github.com/forest-rabbit/SCP-SatComPlate/actions/workflows/phase_gate.yml)

SCP-SatComPlate 是基于官方 ns-3.48 的纯星上动态网络与计算仿真平台。`main` 是
后续开发主线；`legacy/ns-3.33` 永久保留原 SatCompute，作为目录、输入、行为和
指标的只读对照。

当前平台使用 ns-3.48 原生 LEO 圆轨道组件实时计算卫星 ECEF `x/y/z`，使用固定
plus-grid 候选 ISL 和距离门控，不会在每个时刻改选“最近的异轨卫星”。正式仿真
在线生成拓扑；topology-only 模式可以预先输出节点和候选链路切片，供后续故障
建模与前端可视化使用。

## 目录

```text
SCP-SatComPlate/
├── contrib/satcompute/      SatCompute 平台模块、入口、输入、工具和测试
├── third-party/             仓库级第三方依赖
├── docs/                    v0.4 规格、实施计划和历史迁移记录
├── src/                     官方 ns-3.48 模块，不放项目代码
├── ns3                      ns-3.48 构建入口
└── .github/workflows/       项目阶段门禁
```

平台代码只位于 `contrib/satcompute/`。入口是
`contrib/satcompute/satcompute.cc`；`para.h/.cc` 只保存参数结构、默认值和中文
解释；nlohmann JSON 位于仓库根目录 `third-party/`。

## 构建

在仓库根目录执行：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
./ns3 run "satcompute --simulationDuration=2"
./ns3 run "satcompute --help"
```

配置时不启用 ns-3 上游 examples 或全局 tests，也不运行 `test.py`。SatCompute
自己的 C++ 检查仍作为普通 executable 构建。

## 配置与输入

平台不读取完整运行配置 JSON。仿真时间、网络/切片间隔、距离门限、时延、带宽、
路由、随机数和输出目录只来自 `para.cc` 默认值与同名 CLI。人工设置的时间参数以
秒输入，平台内部统一转换为 ns-3 `Time` 和整数纳秒。

独立数据输入按职责分开：

- `input/topology/constellations/*.csv`：ns-3.48 原生星座结构；
- `input/topology/resources/*.json`：卫星静态算力；
- `input/traffic/workload/*.json`：任务到达、输入大小、计算量和输出大小；
- `outputDir/topology/`：topology-only 生成的节点/链路切片，不是正常仿真输入。

ComputeProfile 与 TaskTrace 不包含 schema/version/hash 字段。独立
NetworkTransfer workload 已删除；每个任务仍在内部执行输入传输、FCFS 计算和
结果传输，结果大小严格使用任务显式给出的 `output_bytes`。

## 时间与拓扑

- `fixed` 时延实验可以把网络更新时间设为 20 秒；
- `distance` 时延实验通常设为 1 秒或 2 秒；
- topology-only 的切片间隔独立设置，例如 1 秒；
- distance 模式每个网络 tick 更新链路时延；
- 只有有效链路集合发生变化时才重算当前 hop-based IPv4 路由。

生成拓扑切片：

```bash
./ns3 run "satcompute \
  --simulationDuration=20 \
  --topologyOnly=1 \
  --topologySliceInterval=1 \
  --outputDir=/tmp/satcompute-topology"
```

相同星座、参数、seed/run 和采样时间会得到相同的卫星坐标与候选链路状态。未来
故障流程将先基于这些切片生成故障 JSON，再让正式平台在线计算同一拓扑并在精确
事件时刻应用故障；故障执行本身尚未纳入当前版本。

## 路由与后续范围

当前保留五种 IPv4 模式：`global-first`、固定逐流 hash、HRW、size-aware HRW
和 capacity-aware HRW。固定任务输入、星座、seed/run 与同时事件顺序时，五种
模式均可复现。

IPv6 和 SRv6、卫星故障/修复执行、后端到前端的实时状态接口、地面站与馈电链路
留到后续阶段。本阶段不为尚未确定的接口预建框架。

## 本地验证与 CI

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

每个大阶段只在全部分支合并并清理后，于 `main` 手动触发一次 GitHub CI。中间
小步骤只执行本地验证。

详细参数、JSON 合同、拓扑切片、路由和输出说明见
[SatCompute 运行说明](contrib/satcompute/README.md)，当前长期基线见
[平台 v0.4 规格](docs/specs/platform-v0.4.md)。
