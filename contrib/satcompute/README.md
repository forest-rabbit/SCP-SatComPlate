# SatCompute 运行手册

SatCompute 是 SCP-SatComPlate 在 ns-3.48 上的项目模块，只模拟卫星、星间链路、
IPv4 路由、任务传输和星上计算。平台入口为 `satcompute.cc`；`para.h/.cc` 保存唯一
一组类型化运行参数和默认值，星座、算力与任务则使用彼此独立的数据文件。

## 执行流程

```text
para.cc 默认值 + CLI 覆盖
             |
             v
      星座 CSV 与原生 LEO 轨道
             |
             v
  t=0 固定同轨/异轨候选卫星对
             |
      +------+------------------+
      |                         |
      v                         v
topologyOnly                正式仿真
节点/链路 JSON          IPv4 + 任务 + 指标
```

正式仿真的具体步骤是：

1. 从 `para.cc` 取得默认值，再由同名 CLI 覆盖并做组合校验；
2. 读取一个 ns-3.48 `LeoOrbitalShell` CSV，按 plane-major 顺序建立稳定卫星 ID；
3. 使用 ns-3.48 原生圆轨道 mobility 实时计算 ECEF `x/y/z`；
4. 同轨连接环形前后邻居，相邻轨道面在 `t=0` 选择总距离最小的循环一对一匹配；
5. 后续只更新候选链路距离、active 状态和 distance 时延，不更换异轨对端；
6. 只有 active 边集合变化时才重算 hop-based IPv4 路由；
7. 同时提供 ComputeProfile 与 TaskTrace 时，执行输入传输、FCFS 计算和结果传输；
8. 仿真结束后写出网络、路由、任务和可选失败诊断指标。

`topologyOnly=1` 使用相同轨道和候选链路实现，但不会创建 InternetStack、
NetDevice、路由、FlowMonitor 或任务对象。

## 目录与入口

| 路径 | 职责 |
|---|---|
| `satcompute.cc` | CLI 注册、跨参数校验、两种运行模式和组件编排 |
| `para.h/.cc` | 参数结构、默认值与中文说明 |
| [`common/`](common/README.md) | 跨模块共用的秒到整数纳秒边界转换 |
| [`topology/`](topology/README.md) | 星座读取、原生轨道、固定候选、在线更新、IPv4 地址和切片 |
| [`routing/`](routing/README.md) | 五种 IPv4 策略、hash、HRW 和 reservation 状态 |
| [`task/`](task/README.md) | ComputeProfile、TaskTrace、FCFS 服务和任务协调 |
| [`traffic/`](traffic/README.md) | 任务内部的 UDP 输入/结果传输 |
| [`metrics/`](metrics/README.md) | 网络、路由、任务和失败诊断输出 |
| [`input/`](input/README.md) | 星座、算力、任务与组合示例 |
| [`tools/`](tools/README.md) | 任务生成与输出校验工具 |
| [`tests/`](tests/README.md) | SatCompute 自有 unit、smoke、regression 和 fixture |

JSON 解析统一使用仓库根目录 `third-party/nlohmann/json.hpp`。Python 工具不实现
第二套轨道传播公式。

## 构建与运行

在仓库根目录执行：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
./ns3 run "satcompute --simulationDuration=2"
./ns3 run "satcompute --help"
```

不带参数时，平台使用下表中的默认值运行 1000 秒。日常开发建议显式指定较短的
`simulationDuration` 和独立的 `outputDir`。完整任务运行见
[100 秒、66 星、20 任务示例](input/examples/leo-66-100s-20tasks/README.md)。

## 参数边界

人工设置的时长和间隔统一以秒传入，平台在组件边界转换为 ns-3 `Time` 或有符号
整数纳秒。星座 CSV 只描述轨道结构，算力和任务分别位于独立 JSON；两类数据与
`para.cc` 不重复。

### simulation

| CLI | 默认值 | 类型/单位 | 含义与约束 |
|---|---:|---|---|
| `--simulationDuration` | `1000` | 秒 | 仿真持续时间；必须为可转换为正整数纳秒的有限值 |
| `--randomSeed` | `1` | `uint32` | ns-3 全局随机 seed；必须大于 0 |
| `--randomRun` | `1` | `uint64` | ns-3 独立运行编号；与 seed 共同固定随机流 |

### topology

| CLI | 默认值 | 类型/单位 | 含义与约束 |
|---|---:|---|---|
| `--constellationConfig` | `input/topology/constellations/synthetic-66.csv` | 路径 | 一个原生 LEO shell CSV；不能为空且必须通过星座校验 |
| `--maxIslDistance` | `6174589` | 米 | 候选 ISL 的最大有效距离；必须为有限正数 |
| `--networkUpdateInterval` | `20` | 秒 | 正式仿真的链路状态/时延更新周期；必须大于 0 |
| `--topologyOnly` | `false` | bool | 只输出轨道和拓扑切片；启用时禁止任务输入 |
| `--topologySliceInterval` | `1` | 秒 | topology-only 采样周期；必须大于 0 |
| `--includeFinalTopologyState` | `true` | bool | cadence 未覆盖终点时，是否额外输出仿真终点状态 |

表中的星座默认路径相对于仓库根目录，完整值为
`contrib/satcompute/input/topology/constellations/synthetic-66.csv`。

### link

| CLI | 默认值 | 类型/单位 | 含义与约束 |
|---|---:|---|---|
| `--delayMode` | `fixed` | 枚举 | `fixed` 或 `distance` |
| `--fixedDelay` | `0.008` | 秒 | fixed 模式的单向链路时延；该模式下必须大于 0 |
| `--islBandwidthBps` | `2000000000` | bit/s | 每条 ISL 的数据速率；必须大于 0 |
| `--islMtuBytes` | `64028` | 字节 | ISL MTU；至少 68，size-aware 分包时至少 64028 |
| `--islQueueBytes` | `1500000` | 字节 | 每条 ISL 队列容量；必须大于 0 |

`distance` 时延按当前 ECEF 直线距离除以光速并四舍五入到整数纳秒；
`fixedDelay` 在该模式下不参与链路时延。

### routing

| CLI | 默认值 | 类型/单位 | 含义与约束 |
|---|---:|---|---|
| `--routingMode` | `global-capacity-aware-hrw` | 枚举 | 五种 IPv4 模式之一，见下文与 routing README |
| `--ecmpHashSeed` | `1` | `uint64` | 逐流 hash 和 HRW 的确定性 seed |

合法路由值为 `global-first`、`global-hash-per-flow`、
`global-hrw-per-flow`、`global-size-aware-hrw` 和
`global-capacity-aware-hrw`。

### workload

| CLI | 默认值 | 类型/单位 | 含义与约束 |
|---|---:|---|---|
| `--computeProfile` | 空 | 路径 | 卫星静态算力 JSON；必须与 `taskTrace` 同时提供 |
| `--taskTrace` | 空 | 路径 | 任务到达 JSON；必须与 `computeProfile` 同时提供 |
| `--transferChunkMode` | `size-aware` | 枚举 | `fixed` 或 `size-aware` 分包 |
| `--transferPayloadBytes` | `1024` | 字节 | fixed payload，范围 `1..65507`，加 28-byte IPv4/UDP 头后不能超过 MTU |
| `--receiverRcvBufBytes` | `131072` | 字节 | 每个 UDP 接收 socket 的缓冲区；必须大于 0 |
| `--taskCompletionPolicy` | `strict` | 枚举 | `strict` 对部分完成返回 3；`report` 只报告部分结果并返回 0 |

size-aware 分包按声明传输大小选择 1024、8192 或 64000-byte payload，此时
`transferPayloadBytes` 不参与分包，但仍需位于合法整数范围。

### output

| CLI | 默认值 | 类型/单位 | 含义与约束 |
|---|---:|---|---|
| `--outputDir` | `/tmp/satcompute-output` | 路径 | 结构化结果目录；不能为空 |
| `--taskLogMode` | `summary` | 枚举 | `summary`、`verbose` 或 `silent` |
| `--diagnosticMode` | `off` | 枚举 | `off` 或 `failure`；后者在部分完成时写失败证据 |

运行摘要会记录实际使用的关键参数和各层结果，仅作为本次仿真的输出证据，不是
第二个配置入口。

## 星座与动态拓扑

默认星座为 780 km 高度、86.4 度倾角、6 个轨道面、每面 11 星。稳定卫星 ID 按
plane-major 顺序编号为 `0..65`，不直接使用全局 `Node::GetId()`。

每颗卫星固定连接同轨前后两个槽位。对于每对相邻轨道面，平台在 `t=0` 枚举全部
循环 slot 偏移，选择链路总距离最小的偏移，形成一对一异轨匹配；首尾轨道面不
建立 seam。候选卫星对在整个仿真期间保持不变，`maxIslDistance` 只控制候选当前
是否 active。

- fixed 实验通常可把 `networkUpdateInterval` 设为 20 秒；
- distance 实验通常设为 1 秒或 2 秒，以刷新传播时延；
- 每个 tick 都更新 distance 时延，但只在 active 边集合变化时重算路由；
- 固定星座、参数、任务、seed/run 与同时事件顺序时，拓扑和路由结果可复现。

## topology-only 与未来故障流程

生成 0–100 秒、每秒一个切片：

```bash
./ns3 run "satcompute \
  --simulationDuration=100 \
  --topologyOnly=1 \
  --topologySliceInterval=1 \
  --includeFinalTopologyState=1 \
  --outputDir=/tmp/satcompute-topology"
```

输出位于 `outputDir/topology/`，每个采样点包含一对 `nodes_<time>s.json` 和
`links_<time>s.json`。节点文件记录稳定 ID 与 ECEF `x/y/z`；链路文件保留全部
固定候选并记录 `active`、距离、时延和带宽。详细合同见
[topology/export](topology/export/README.md)。

计划中的故障工作流是：先生成整个周期的拓扑切片，再据此生成故障 JSON，最后让
正式平台在线计算同一拓扑并读取故障事件。未来故障将在精确事件时刻立即禁用资源
并重算路由，不等待下一个网络 tick；故障生成和执行当前尚未实现。

## 任务与计算

ComputeProfile 与 TaskTrace 必须成对提供。每个任务显式给出源卫星、计算卫星、
结果卫星、`input_bytes`、`compute_work_units`、`output_bytes` 和整数纳秒到达时间。

每个计算节点是单服务台、非抢占 FCFS，排序键为
`(queue_enter_time_ns, task_id)`。服务时间为：

```text
ceil(compute_work_units * 1,000,000,000
     / compute_rate_work_units_per_second) ns
```

平台不执行真实业务算法，因此结果传输大小严格采用 `output_bytes`。任务 ID `T`
派生输入传输 ID `2*T-1` 和结果传输 ID `2*T`。详细输入合同见
[task](task/README.md) 与 [input](input/README.md)。

## IPv4 路由

- `global-first`：直接采用 ns-3 `Ipv4GlobalRouting` 的原生首条路由行为；
- `global-hash-per-flow`：固定五元组 hash 映射等价下一跳；
- `global-hrw-per-flow`：使用 Rendezvous/HRW hash 选择最高分候选；
- `global-size-aware-hrw`：在两个最高 HRW 候选中选择已保留声明字节更少者；
- `global-capacity-aware-hrw`：在完整等价路径中最大化剩余瓶颈容量。

所有模式均为逐流确定性选择，不使用随机逐包 ECMP。公式、tie-break、粘滞状态和
释放时机见 [routing README](routing/README.md)。当前只支持 IPv4；IPv6 与 SRv6
留给后续阶段。

## 输出与验证

正式仿真常用输出包括 `run-summary.json`、网络逐流指标、传输/任务指标、计算节点
利用率、路由事件，以及相应 size-aware/capacity-aware 汇总。只有显式启用失败
诊断且运行部分完成时，才保留 `diagnostics/failure/`。完整文件说明见
[metrics README](metrics/README.md)。

测试命令、覆盖范围和阶段 CI 规则统一放在
[tests README](tests/README.md)，本手册不重复维护测试清单。
