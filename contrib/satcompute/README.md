# SatCompute ns-3.48 运行说明

`main` 基于官方 ns-3.48；`legacy/ns-3.33` 永久保留为只读行为基线。平台代码只
位于 `contrib/satcompute/`，入口仍是根目录的 `satcompute.cc`，参数默认值仍集中
在 `para.cc`。

## 执行模型

1. 从 `para.cc` 取得默认值，再由同名命令行参数覆盖；
2. 读取 ns-3.48 原生 LEO shell CSV，按稳定卫星 ID 创建节点并实时计算 ECEF
   `x/y/z`；
3. 初始化时生成固定 plus-grid 候选 ISL，运行中不会改成“选择当前最近的异轨
   卫星”；
4. 每个网络更新时间重新计算坐标、距离门控和 distance 时延；只有有效链路集合
   发生变化时才重算 hop-based IPv4 路由；
5. 可选读取分离的 ComputeProfile 与 TaskTrace，依次执行输入传输、FCFS 计算和
   结果传输；没有任务输入时只运行在线网络；
6. `topologyOnly=1` 时不创建 InternetStack、NetDevice、路由、FlowMonitor 或
   任务对象，只按独立间隔输出节点和候选链路切片；
7. 正常网络仿真始终在线计算拓扑，不读取预生成切片。

程序当前不创建地面站。故障执行、前后端状态接口、IPv6、SRv6、SGP4/TLE 和
非圆轨道属于后续阶段。

## 源码布局

```text
contrib/satcompute/
├── satcompute.cc       平台入口、CommandLine 和跨字段校验
├── para.h/.cc          参数结构、默认值和中文解释
├── topology/           原生轨道、固定候选、在线链路和切片导出
├── routing/            五种 IPv4 路由策略及其运行状态
├── task/               ComputeProfile、TaskTrace、FCFS 与任务协调
├── traffic/            任务内部的输入/结果 UDP 传输
├── metrics/            网络、路由、任务和失败诊断输出
├── input/              星座 CSV、算力 JSON 和任务 JSON
├── tools/              任务生成器与最小输出检查工具
└── tests/              精简 unit、smoke、regression 和小型 fixture
```

JSON 统一使用仓库根目录 `third-party/nlohmann/json.hpp`。`tools/` 不复制轨道
传播公式，卫星位置只由共享的 ns-3.48 C++ 实现计算。

## 构建与运行

在仓库根目录执行：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
./ns3 run "satcompute --simulationDuration=2"
./ns3 run "satcompute --help"
```

配置时不启用 ns-3 全局 examples 或 tests，也不运行上游 `test.py`。SatCompute
自有 C++ 测试作为普通 executable 构建，因此在上述配置下仍可运行。

不带参数会按默认值运行 1000 秒。日常开发建议显式给出较短的
`simulationDuration` 和独立 `outputDir`。

## 默认参数

人工设置的时间和间隔均以秒输入，入口校验后统一转换为 ns-3 `Time` 和整数纳秒。

```text
simulationDuration        = 1000
constellationConfig       = contrib/satcompute/input/topology/constellations/synthetic-66.csv
islCandidateStrategy      = plus-grid
seamEnabled               = false
maxIslDistance            = 6174589
delayMode                 = fixed
fixedDelay                = 0.008
networkUpdateInterval     = 20
islBandwidthBps           = 2000000000
islMtuBytes               = 1500
islQueueBytes             = 1500000
receiverRcvBufBytes       = 131072
routingMode               = global-capacity-aware-hrw
ecmpHashSeed              = 1
computeProfile            = empty
taskTrace                 = empty
transferChunkMode         = fixed
transferPayloadBytes      = 1024
taskCompletionPolicy      = strict
topologyOnly              = false
topologySliceInterval     = 1
includeFinalTopologyState = true
outputDir                 = /tmp/satcompute-output
taskLogMode               = summary
diagnosticMode            = off
randomSeed                = 1
randomRun                 = 1
```

`para.cc` 只负责这些赋值及其中文注释；`CommandLine::AddValue`、文件检查、组合
校验和时间转换都在入口或使用参数的组件中。

## 星座与拓扑更新

星座 CSV 只描述一个原生 Walker shell：

```text
altitudeKm,inclinationDegrees,numberOfPlanes,numberOfSatellitesPerPlane,phasingFactor,raanSpanDeg
780.0,86.4,6,11,1,180
```

卫星 ID 按 plane-major 创建顺序稳定映射，不直接使用全局 `Node::GetId()`。
plus-grid 候选身份在初始化时固定；`maxIslDistance` 只决定某个候选在当前时刻是否
有效，不会替换它的对端。

- `delayMode=fixed`：活动链路使用 `fixedDelay`，实验通常可把
  `networkUpdateInterval` 设为 20 秒；
- `delayMode=distance`：每个网络 tick 根据当前距离刷新传播时延，实验通常设为
  1 秒或 2 秒；
- 两个间隔都是输入，不在代码中写死；
- 时延变化不会单独触发 hop 路由重算，只有活动链路集合变化才会重算。

固定星座、参数、seed/run 和时间点会得到相同坐标、候选链路状态与路由输入。

## topology-only 与未来故障流程

生成 0–1000 秒、每秒一个切片的示例：

```bash
./ns3 run "satcompute \
  --simulationDuration=1000 \
  --topologyOnly=1 \
  --topologySliceInterval=1 \
  --includeFinalTopologyState=1 \
  --outputDir=/tmp/satcompute-topology"
```

输出位于 `/tmp/satcompute-topology/topology/`：

```text
nodes_0s.json    links_0s.json
nodes_1s.json    links_1s.json
...
```

节点切片包含 `simulation_time_ns`、稳定 `node_id`、`node_type` 和 ECEF
`x/y/z`。链路切片列出全部固定候选，并包含 `active`、`distance_m`、
`delay_ns` 和 `link_bandwidth_bps`；暂时超过距离门限的候选不会从 JSON 消失。
切片不包含 schema、软件版本、SHA 或 manifest。

后续故障建模计划是：先用同一配置生成完整节点/链路切片，再基于切片产生故障
JSON，最后让正式仿真在线计算同一拓扑并读取故障事件。故障输入和精确纳秒故障
执行尚未实现；未来实现后，故障会在事件时刻立即禁用资源并重算路由，不等待下一
周期切片。

## ComputeProfile 与 TaskTrace

算力属于 topology side，位于 `input/topology/resources/`；任务到达属于 traffic
side，位于 `input/traffic/workload/`。两者必须同时通过 `--computeProfile` 和
`--taskTrace` 指定，均为 closed-world JSON，且不包含 `schema_version`。

ComputeProfile：

```json
{
  "compute_nodes": [
    {"node_id": 3, "compute_rate_work_units_per_second": 1000000}
  ]
}
```

TaskTrace：

```json
{
  "tasks": [
    {
      "task_id": 1,
      "source_node_id": 0,
      "compute_node_id": 3,
      "result_node_id": 0,
      "input_bytes": 4096,
      "output_bytes": 2050,
      "compute_work_units": 1000000,
      "arrival_time_ns": 100000000
    }
  ]
}
```

每个计算节点是单服务台、非抢占 FCFS，排序键为
`(queue_enter_time_ns, task_id)`。整数服务时间为：

```text
ceil(compute_work_units * 1,000,000,000
     / compute_rate_work_units_per_second) ns
```

平台不执行真实算法，因此计算完成后的结果大小严格取任务输入中的
`output_bytes`。任务 ID `T` 在运行时派生输入传输 `2*T-1` 和结果传输 `2*T`；
独立 NetworkTransfer workload、`--transferTrace` 及 transfer 生成器已经删除。

最小任务运行：

```bash
./ns3 run "satcompute \
  --simulationDuration=5 \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
  --maxIslDistance=30000000 \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=2 \
  --computeProfile=contrib/satcompute/tests/fixtures/task/compute-profile-single.json \
  --taskTrace=contrib/satcompute/tests/fixtures/task/task-single.json \
  --routingMode=global-size-aware-hrw \
  --outputDir=/tmp/satcompute-task"
```

## IPv4 路由

当前保留五种模式：

- `global-first`：使用 ns-3 原生全局路由的首条等价最短路；
- `global-hash-per-flow`：对稳定五元组做固定 FNV hash，再映射等价下一跳；
- `global-hrw-per-flow`：使用 HRW/Rendezvous hash，候选变化时减少无关流迁移；
- `global-size-aware-hrw`：在 HRW 候选间维护活动传输的声明字节账本；
- `global-capacity-aware-hrw`：按完整等价路径剩余容量准入，并按瓶颈带宽 pacing。

任务、星座、seed/run 与同时事件 canonical 顺序固定时，size-aware 和
capacity-aware 的选择同样确定；平台不使用随机逐包 ECMP。当前路由仍是 IPv4，
IPv6 和 SRv6 不在本阶段迁移。

## 完成策略与输出

`taskCompletionPolicy=strict` 在任务未全部完成时先落盘再返回退出码 3；`report`
写出同一结果但正常返回。`diagnosticMode=failure` 为未完成任务额外输出未完成
任务/传输、ISL 队列 Drop、UDP socket Drop 和 FlowMonitor DropReason。

常用输出包括：

- `run-summary.json`；
- `network-flow-metrics.csv`、`network-flow-details.csv`；
- `transfer-summary.csv`、`task-events.csv`、`task-summary.csv`；
- `compute-node-summary.csv`、`ecmp-route-events.csv`；
- size-aware/capacity-aware 对应的 reservation 与 summary；
- `diagnostics/failure/` 下的失败证据。

运行摘要记录实际参数和运行证据，但不写 effective/resolved 配置、软件版本、输入
哈希或格式版本，也不能作为第二个配置入口。

## 测试与 CI

本地完整门禁：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

测试保留 4 个 legacy 业务 smoke、一个 ns-3.48 原生 topology-only smoke、两组
完整 regression 和少量聚焦 unit。旧 replay fixture、schema/SHA 测试以及已被
集成层覆盖的大量散列 C++ executable 已删除。

开发过程只在一个大阶段全部 PR 合并并清理后，于 `main` 手动触发一次
`SatCompute CI`；中间分支只运行本地测试。
