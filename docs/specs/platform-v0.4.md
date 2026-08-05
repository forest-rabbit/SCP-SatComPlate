# 规格：SCP-SatComPlate 简化平台 v0.4

状态：已确认，作为 v0.3 迁移完成后的简化与长期开发基线。

## 目标

在官方 ns-3.48 上保留 SatCompute 的任务计算、IPv4 路由和指标能力，同时移除
迁移期引入但后续平台不需要的配置层、版本层、完整性哈希、独立拓扑生成程序和
冗余测试。平台需要支持两个共享同一轨道与拓扑实现的运行阶段：

1. 拓扑预处理：不安装网络协议栈和任务应用，只按输入切片间隔输出每颗卫星的
   ECEF 坐标和每条固定候选 ISL 的状态；
2. 正式仿真：根据同一个星座文件在线重算确定性拓扑，执行任务输入传输、计算和
   结果传输；未来只额外读取故障事件，不回放预处理切片。

预处理切片将供未来故障生成器使用。故障生成与执行、前端接口、IPv6 和 SRv6
仍不在 v0.4 实现范围内。

## 技术栈

- 仿真器：官方 ns-3.48；
- 项目模块：`contrib/satcompute/`；
- 构建：ns-3 CMake/Ninja；
- 平台实现：C++；
- 数据输入与输出：原生轨道 CSV、业务 JSON、指标 CSV/JSON；
- JSON：根目录 `third-party/nlohmann/json.hpp`；
- 长期分支：`main` 与只读参考 `legacy/ns-3.33`。

## 命令

日常配置和构建不启用 ns-3 全局 examples/tests：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
```

拓扑预处理和正式任务仿真都使用同一个入口：

```bash
./ns3 run "satcompute --topologyOnly=1"
./ns3 run "satcompute --topologyOnly=0 --computeProfile=... --taskTrace=..."
```

项目本地验证仅运行保留的 SatCompute 测试：

```bash
python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

每个实现增量运行聚焦本地验证。整个 v0.4 大阶段全部合并后只在 `main` 手动运行
一次 `SatCompute CI`；CI 不运行 `test.py` 或上游 examples/tests。

## 项目结构

```text
third-party/
└── nlohmann/json.hpp

contrib/satcompute/
├── CMakeLists.txt
├── README.md
├── para.h
├── para.cc
├── satcompute.cc
├── input/
│   ├── topology/
│   │   ├── constellations/       # ns-3.48 LeoOrbitalShell CSV
│   │   └── resources/workload/   # 卫星算力 JSON
│   ├── traffic/workload/         # 任务 JSON
│   └── fault/                    # 未来故障 JSON
├── topology/
│   ├── orbit/                    # 原生 LEO helper 的薄封装
│   ├── online/                   # 候选链路、距离门控和周期更新
│   ├── export/                   # topologyOnly 切片输出
│   ├── link/
│   └── ipv4/
├── routing/
├── traffic/                      # 任务内部的网络传输引擎
├── task/
├── metrics/
├── tools/
│   ├── generation/               # 仅保留任务生成器；未来增加故障生成器
│   ├── analysis/
│   ├── validation/
│   └── visualization/
└── tests/
    ├── unit/
    ├── integration/smoke/
    ├── integration/regression/
    └── fixtures/
```

不保留模块内 `third-party/`、独立 `tools/generation/scenario/`、
`tools/generation/topology/`、`satcompute-topology-generator` 或 `app/`。

## 配置合同

### `para.h` 与 `para.cc`

`para.h` 只定义 `SatComputeConfig` 和默认值函数；`para.cc` 只给每个参数赋默认值
并用中文解释。`CommandLine::AddValue`、大小写归一化、文件检查、跨字段校验和秒到
ns-3 `Time`/整数纳秒转换均位于入口或使用该值的组件，不放入 `para.cc`。

平台参数包括：

- 星座 CSV、算力 JSON、任务 JSON和输出目录路径；
- 仿真时长、网络更新时间、拓扑切片间隔和是否包含终点；
- `topologyOnly`；
- 固定 plus-grid 候选、seam 开关和最大 ISL 距离；
- `fixed`/`distance` 时延及 fixed 时延；
- ISL 带宽、MTU、队列和 UDP 接收缓冲；
- 五种 IPv4 路由模式、ECMP hash seed；
- 任务分包、完成策略、日志和诊断参数；
- ns-3 seed 与 run。

不保留 `runName`、`simulationStart`、`topologySource`、`topologyDir`、
`routingRecomputePolicy`、`transferTrace`、`topologyExportEnabled`、
`randomStreamStart`、`validateOnly` 或第二套 resolved/effective 配置对象。

### 星座 CSV

星座位于 `input/topology/constellations/`，使用 ns-3.48
`LeoOrbitNodeHelper`/`LeoOrbitalShell` 的六列格式：

```text
altitudeKm,inclinationDegrees,numberOfPlanes,numberOfSatellitesPerPlane,phasingFactor,raanSpanDeg
780.0,86.4,6,11,1,180
```

v0.4 支持一个 Walker shell。卫星外部 ID 按 native helper 的 plane-major 创建顺序
映射为 `plane * satellitesPerPlane + slot`，但不得直接使用 `Node::GetId()` 作为外部
ID。星座文件不包含仿真、网络、路由、随机数、任务或输出参数。

### 任务与算力

ComputeProfile 和 TaskTrace 仍是两个独立 JSON，但不再包含 `schema_version`。
TaskTrace 中每个任务继续明确给出 `input_bytes`、`compute_work_units` 和
`output_bytes`。平台不执行真实算法，因此计算完成后的结果传输大小严格取任务
输入中的 `output_bytes`，不在仿真运行时重新推导。

不提供独立 NetworkTransfer workload。`traffic/` 中的传输引擎继续服务于每个
任务的输入传输和结果传输；纯传输 fixture 只在仍有必要的路由回归中保留。

## 拓扑与时间语义

1. `LeoOrbitNodeHelper` 创建卫星并安装 `LeoCircularOrbitMobilityModel`；
2. plus-grid 在初始化时按稳定卫星身份生成固定候选，不动态选择最近异轨卫星；
3. 每次采样只依据当前坐标、距离阈值和时延模式更新候选状态；
4. `distance` 模式通常用 1 s/2 s 的 `networkUpdateIntervalSeconds`，`fixed` 模式
   可用 20 s；这两个值都是 `para.cc`/CLI 输入，不在代码中绑定；
5. distance 模式每个网络 tick 刷新活动链路时延，但只有活动链路集合改变时才
   重算 hop-based IPv4 路由；
6. topology-only 模式按独立 `topologySliceIntervalSeconds` 推进 ns-3 事件时钟，
   但不安装 InternetStack、NetDevice、路由、FlowMonitor、任务或指标模块；
7. 相同星座、参数、seed/run 和时间点必须得到相同坐标及链路状态；
8. 未来故障在精确纳秒时刻立即覆盖链路/卫星状态并重算路由，不等待周期 tick。

## 拓扑切片合同

拓扑预处理写入 `outputDirectory/topology/`：

```text
nodes_0s.json
links_0s.json
nodes_1s.json
links_1s.json
...
```

节点切片只包含当前时间和稳定坐标：

```json
{
  "simulation_time_ns": 0,
  "nodes": [
    {"node_id": 0, "node_type": "sat", "x": 0.0, "y": 0.0, "z": 0.0}
  ]
}
```

链路切片输出全部固定候选，而不是只输出活动链路：

```json
{
  "simulation_time_ns": 0,
  "links": [
    {
      "node1_id": 0,
      "node2_id": 1,
      "type": "sat",
      "active": true,
      "distance_m": 1000.0,
      "delay_ns": 3336,
      "link_bandwidth_bps": 2000000000
    }
  ]
}
```

这使故障生成器能够区分“固定候选身份”和“因距离暂时不可用”。切片不写
`schema_version`、软件版本、SHA-256 或 manifest；文件名和内容的时间来自同一个
整数纳秒采样点。

## 路由、任务与指标

正式仿真继续保留：

- `global-first`；
- `global-hash-per-flow`；
- `global-hrw-per-flow`；
- `global-size-aware-hrw`；
- `global-capacity-aware-hrw`；
- 输入传输、FCFS 非抢占计算、结果传输；
- ns-3.33 对应的 flow、task、compute、routing 和失败诊断输出。

固定任务输入、星座、seed/run 和 canonical 同时事件顺序时，所有模式必须可复现。
运行摘要可以记录实际参数值，但不写 effective config、输入哈希、软件版本或格式
版本，也不能成为第二个运行配置入口。

## 代码风格

- 遵循 ns-3.48 `.clang-format`；
- C++ public 类型和接口使用 Doxygen；
- C++ 注释和 Doxygen 不使用 Unicode 数学符号或箭头；
- 入口风格与 ns-3.33 对应：

```cpp
SatComputeConfig config = GetDefaultSatComputeConfig();
CommandLine commandLine;
commandLine.AddValue("simulationDuration", "Simulation duration in seconds",
                     config.simulationDurationSeconds);
commandLine.Parse(argc, argv);
```

- 只保留当前功能需要的抽象，不为未实现的故障、前端或 IPv6 预建框架。

## 测试策略

测试以已精简的 ns-3.33 资产为基线并针对在线轨道调整：

- 4 个 smoke：路由、capacity-aware、任务、诊断；
- 2 个 regression：完整路由和完整 workload；
- 一个快速原生轨道/topology-only smoke，验证 66 星、坐标、全候选链路、
  1 s/2 s 采样和确定性；
- 仅保留被运行路径直接使用的 Python 工具测试；
- 删除迁移审计、CI 策略、schema、effective/resolved、版本、SHA、独立生成器和
  大量散列 C++ 可执行测试。

测试代码和 fixture 均位于 `contrib/satcompute/tests/`。删除测试必须因为对应功能
已删除或已有上层回归覆盖，不能通过弱化核心路由和任务断言来获得通过。

## 边界

### 始终执行

- 修改前对照 `legacy/ns-3.33` 和当前 ns-3.48 API；
- 每个增量执行聚焦构建/测试并保持可回滚；
- 输入集合和同时事件使用 canonical 顺序；
- PR 合并后确认 head 可达 `main` 再删除临时分支；
- 整个 v0.4 阶段只运行一次 GitHub CI。

### 必须先确认

- 改变五种 IPv4 路由算法或 FNV/HRW 字节布局；
- 改变 `output_bytes` 的任务语义；
- 引入新第三方依赖；
- 修改上游 `src/`；
- 开始故障执行、前端协议、IPv6 或 SRv6。

### 禁止

- 合并或删除 `legacy/ns-3.33`；
- 恢复完整 scenario JSON；
- 在 Python 中复制卫星轨道计算；
- 正式仿真回放预处理拓扑切片；
- 在 GitHub CI 中运行 `test.py` 或上游 examples/tests；
- 提交 `.vscode` 或本地输出。

## 完成标准

1. `para.cc` 只包含默认值和中文参数解释，CLI/校验位于入口；
2. effective/resolved config、SatCompute/ns-3 版本、SHA-256 和 schema 文件/字段
   从平台及其输入输出中删除；
3. 星座使用原生六列 CSV，节点由 `LeoOrbitNodeHelper` 创建；
4. topology-only 不创建网络仿真对象，并输出每个切片的 XYZ 和全部候选链路状态；
5. 正式任务仿真在线生成同一拓扑，不读取切片；
6. 独立 scenario/topology/transfer 生成入口删除，任务内部传输继续工作；
7. 五种 IPv4 路由、FCFS 任务闭环和精简测试全部通过；
8. README 为中文且以 ns-3.33 结构和写法为主体；
9. nlohmann 位于根目录 `third-party/`，GitHub 不再跟踪 `.vscode`；
10. main 的唯一阶段 CI 通过，工作树干净，只保留 `main` 和
    `legacy/ns-3.33` 长期分支。

## 延期范围

- 基于切片生成故障 JSON；
- 故障/修复事件在仿真中的执行；
- 后端和前端状态传输接口；
- IPv6、SRv6、地面站和馈电链路；
- SGP4/TLE 与非圆轨道。
