# SatCompute

[![SatCompute CI](https://github.com/forest-rabbit/SCP-SatComPlate/actions/workflows/phase_gate.yml/badge.svg)](https://github.com/forest-rabbit/SCP-SatComPlate/actions/workflows/phase_gate.yml)

SatCompute 是基于官方 ns-3.48 的纯星上动态网络与计算仿真平台，只创建卫星节点
和星间链路（ISL）。卫星位置由共享的 C++ 圆轨道核心在线计算；同一核心也可以
生成带 ECEF `x/y/z` 坐标的 JSON topology trace，供确定性 replay、后续故障建模
和前端可视化使用。最短路和路由表重算仍由 ns-3 原生
`GlobalRouteManager` 完成。

仓库的 `main` 是 ns-3.48 开发主线；`legacy/ns-3.33` 永久保留原实现，作为目录、
输入、行为、指标和测试的只读基线。当前迁移规格见
[平台 v0.3 规格](docs/specs/platform-v0.3.md)，逐文件结论见
[迁移矩阵](docs/plans/ns3-33-to-48-matrix.md)。

本版本保留 NetworkTransfer、任务计算、五种 IPv4 路由和分层指标。卫星故障
执行、前后端实时传输接口、IPv6 与 SRv6 明确留到后续开发，不在兼容迁移中
提前加入。

## 目录

```text
contrib/satcompute/
├── CMakeLists.txt              # ns-3.48 contrib 模块和项目 executable
├── satcompute.cc               # 平台主入口
├── para.h/.cc                  # 全部运行默认值、CLI 和组合校验
├── effective-config.*          # 解析结果、输入哈希和运行证据
├── resolved-config.*           # 秒到整数纳秒后的内部配置
├── topology/
│   ├── satellite-topology.*    # online/replay 的统一 facade
│   ├── orbit/                  # 唯一 C++ 圆轨道与星座结构读取器
│   ├── online/                 # 实时位置、固定候选、距离门控和时延
│   ├── replay/                 # JSON 全量切片回放
│   ├── export/                 # ECEF XYZ、有效 ISL 和 manifest 导出
│   ├── snapshot/               # 切片读取与时间调度
│   ├── link/                   # 固定 ISL 设备、队列和启停状态
│   └── ipv4/                   # 稳定 service /32 与 ISL /30 地址
├── routing/
│   ├── common/                 # 五元组、候选和确定性 hash
│   ├── algorithm/              # First、Hash、HRW、Size、Capacity
│   ├── state/                  # flow、字节和完整路径容量账本
│   └── ns3/                    # ns-3.48 IPv4 路由适配
├── traffic/                    # JSON NetworkTransfer UDP 运行时
├── task/                       # TaskTrace、FCFS 计算服务与任务协调
├── metrics/                    # core、routing、diagnostics 分层输出
├── input/                      # 正式星座、拓扑、算力和 workload
├── tools/                      # 生成、分析、校验和可视化工具
├── tests/                      # unit、smoke、regression 与 fixtures
└── third-party/nlohmann/       # 固定版本 nlohmann JSON 单头文件
```

与 ns-3.33 相比，`CMakeLists.txt` 替代 `wscript`；新增的
`topology/orbit`、`online`、`replay`、`export` 和 `ipv4` 只承担 ns-3.48
在线轨道与稳定接口职责。平台入口仍在模块外层，不使用 `app/`。Hypatia/TLE
传播后端不进入当前主线。

## 构建与基本运行

在仓库根目录只配置 SatCompute 及其依赖；不启用 ns-3 上游 examples、全局
tests，也不运行 `test.py`：

```bash
./ns3 configure --enable-modules=satcompute \
  --disable-examples --disable-tests -G Ninja
./ns3 build
```

快速校验默认输入、短时间 online 运行和参数帮助：

```bash
./ns3 run "satcompute --validateOnly=true"
./ns3 run "satcompute --simulationDuration=2 \
  --topologyExportEnabled=false"
./ns3 run "satcompute --help"
```

不带参数时按 `para.cc` 默认值运行 1000 秒、每 20 秒更新网络状态，并每 1 秒
导出一份 topology trace。日常开发应显式设置较短的 `simulationDuration`，或先
使用 `validateOnly`。

SatCompute 自有测试仍位于 legacy 的分层目录。统一入口为：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

`smoke/run-all.sh` 编排路由、capacity-aware、任务、诊断和在线拓扑五个具名
runner；`regression/run-all.sh` 编排完整路由与完整 workload 两个 runner。详细
入口和可选可视化测试见
[测试说明](contrib/satcompute/tests/README.md)。GitHub Actions 只允许手动触发
项目阶段门禁，每个大阶段在全部 PR 合并后只运行一次。

## 配置与输入

平台不读取“完整运行配置 JSON”。运行参数只来自有类型的 `para.h/.cc` 默认值
和同名 CLI 覆盖；人工设置的时间均以秒输入，解析后精确转换为整数纳秒。核心
默认值为：

```text
simulationDuration       = 1000 s
topologySource           = online
delayMode                = fixed
fixedDelay               = 0.008 s
networkUpdateInterval    = 20 s
topologyExportInterval   = 1 s
islBandwidthBps          = 2000000000
routingMode              = global-capacity-aware-hrw
routingRecomputePolicy   = on-topology-change
randomSeed/randomRun     = 1/1
outputDir                = /tmp/satcompute-output
```

JSON 按职责保持分离：

- `input/topology/constellations/*.json` 只描述 Walker 结构、轨道面、每面卫星数、
  高度、倾角、相位和 epoch；
- `nodes_<time>s.json`、`topology_<time>s.json` 与 `manifest.json` 是 replay
  topology trace；
- ComputeProfile 保存节点静态算力；
- TaskTrace 保存任务到达、输入/输出字节和计算工作量；
- NetworkTransfer 保存直接网络传输；
- 未来故障事件仍将是独立输入，不写入 constellation 或 `para.cc`。

有冲突或重复的仿真时间、网络/导出间隔、距离门限、时延、带宽、路由、随机数
和输出参数只允许放在 `para.cc`/CLI，constellation reader 会拒绝这些字段。
每次运行写出的 `effective-config.json` 包含解析后的全部参数和输入 SHA-256；它是
只读证据，不是下一次运行的配置入口。

全部参数、约束和默认值见
[模块运行说明](contrib/satcompute/README.md#参数合同)。

## 在线拓扑、回放与切片

online 和 replay 使用同一组稳定 satellite ID、固定 plus-grid 候选和 ISL 设备
身份。平台不会在每个时刻改选“最近的异轨卫星”：候选身份先固定，距离门限只
决定该候选当前是否有效。

- `fixed` 与 `distance` 使用同一星座和候选集合；
- `networkUpdateInterval` 决定网络状态应用周期，可按实验设为 1、2 或 20 秒；
- `topologyExportInterval` 独立决定 JSON 位置/拓扑切片精度；
- distance 模式在每个 network tick 更新传播时延；
- 只有有效链路集合变化时才重算当前 hop-based IPv4 路由，单纯距离/时延变化
  不触发 SPF；
- replay 在两个被应用的切片之间保持上一份全量状态；未来故障会在精确纳秒
  时刻立即改变状态并重算，不等待下一个周期 tick。

短时间 replay 示例必须同时给出与切片节点数一致的 constellation：

```bash
./ns3 run "satcompute \
  --runName=diamond-replay \
  --simulationDuration=5 \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.json \
  --topologySource=replay \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=2 \
  --routingMode=global-first \
  --topologyExportEnabled=false \
  --outputDir=/tmp/satcompute-diamond-replay"
```

共享拓扑生成器直接调用 online 平台的同一 C++ 轨道核心。下面生成 0–20 秒、
间隔 1 秒的切片，同时保留 20 秒网络周期语义：

```bash
./ns3 run "satcompute-topology-generator \
  --runName=synthetic-66-distance \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.json \
  --simulationDuration=20 \
  --topologyExportInterval=1 \
  --networkUpdateInterval=20 \
  --delayMode=distance --fixedDelay=0 \
  --maxIslDistance=6174589 \
  --outputDir=/tmp/synthetic-66-trace"

python3 contrib/satcompute/tools/generation/topology/check_export.py \
  --trace-dir=/tmp/synthetic-66-trace
```

输出目录直接包含 `manifest.json`、成对的 `nodes_<time>s.json` 和
`topology_<time>s.json`。manifest 固定有序文件清单与 SHA-256，可作为后续故障
生成的确定性位置/拓扑证据。相同参数下，生成器与平台 `--exportOnly=true` 的
`topology-trace/` 输出逐字节一致。

`tools/generation/scenario/` 不再生成完整平台配置，只把已经生成的 topology
trace、ComputeProfile 和 TaskTrace/NetworkTransfer 组合成可校验的独立输入
bundle；预留的 `fault_trace` 当前固定为 `null`。

## 可选轨道可视化与前端边界

`tools/visualization/orbit/` 只读取 manifest 中的稳定卫星 ID、仿真时间、ECEF
`x_m/y_m/z_m` 和活动 ISL，不在 Python 中重新传播轨道。它支持 Matplotlib
交互、headless 渲染和可选 GIF：

```bash
MPLBACKEND=Agg MPLCONFIGDIR=/tmp/satcompute-matplotlib \
python3 -m contrib.satcompute.tests.integration.smoke.orbit_visualization \
  --work-dir=/tmp/satcompute-orbit-smoke --export-gif
```

后续前端可以复用相同状态合同；卫星状态到后端/前端的实时网络接口尚未确定，
因此当前只提供离线 trace 消费者，不提前固定传输协议。

## NetworkTransfer

NetworkTransfer JSON 版本固定为 `0.1`，每条记录只含：

```json
{
  "transfer_id": 1,
  "source_node_id": 0,
  "destination_node_id": 3,
  "size_bytes": 4096,
  "arrival_time_ns": 100000000
}
```

源宿是外部 satellite ID。JSON 不保存包数、包间隔、包长、端口、MTU 或速率。
`fixed` 使用 `transferPayloadBytes`；`size-aware` 按传输大小固定选择
1024/8192/64000-byte payload，最后一包使用准确余量。目的 UDP 端口固定为
9000，源端口按源卫星和 canonical transfer ID 从 10000 派生。

直接传输、任务输入与纯拓扑三种模式互斥：`transferTrace` 可单独使用；
`computeProfile` 与 `taskTrace` 必须同时提供；三者为空时不安装业务应用。

## 任务计算

ComputeProfile 与 TaskTrace 继续分离。每个任务派生输入和结果两条
NetworkTransfer，并依次经历：

```text
PENDING → INPUT_TRANSFERRING → QUEUED → RUNNING
        → RESULT_TRANSFERRING → COMPLETED
```

输入完整到达后，任务才进入计算节点的单服务台、非抢占 FCFS 队列；相同纳秒
按 task ID 决定顺序。计算时间严格为：

```text
ceil(compute_work_units × 1,000,000,000
     / compute_rate_work_units_per_second) ns
```

可执行的 4 星任务示例：

```bash
./ns3 run "satcompute \
  --runName=task-replay \
  --simulationDuration=5 \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.json \
  --topologySource=replay \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic \
  --delayMode=fixed --fixedDelay=0.001 \
  --networkUpdateInterval=2 \
  --computeProfile=contrib/satcompute/tests/fixtures/task/compute-profile-single.json \
  --taskTrace=contrib/satcompute/tests/fixtures/task/task-single.json \
  --routingMode=global-size-aware-hrw \
  --topologyExportEnabled=false \
  --outputDir=/tmp/satcompute-task-replay"
```

`taskCompletionPolicy=strict` 在有未完成对象时先写出全部指标，再以退出码 3
返回；`report` 写出同样的 `PARTIAL` 结果但退出码为 0。配置或运行错误返回 2。

## 路由

- `global-first`：使用原生 `Ipv4GlobalRouting` 的 canonical 首条路由；
- `global-hash-per-flow`：对 UDP 五元组和 seed 做固定 FNV-1a-64 取模；
- `global-hrw-per-flow`：对每个候选计算 Rendezvous/HRW 分数，候选变化时实现
  稳定映射和最小迁移；
- `global-size-aware-hrw`：在 HRW 前两名中按活动 transfer 的声明字节预留选择
  物理下一跳，并保持节点级 sticky assignment；
- `global-capacity-aware-hrw`：在等价最短路图上做完整路径容量准入，按剩余
  瓶颈速率 pacing；路径失效时暂停未发数据、整路径释放并重新准入。

五种模式全部是 IPv4。固定输入、五元组、候选、seed 和 canonical 同时事件
顺序时，Hash、HRW、size-aware 与 capacity-aware 都可复现；平台不使用随机
逐包 ECMP。IPv6/SRv6 后续实现时必须保留这些现有策略，当前不提供半成品迁移。

## Diamond 验证

legacy 的四个具名 smoke 和两个 full regression 已恢复并适配 ns-3.48。单独
验证路由或完整回归：

```bash
contrib/satcompute/tests/integration/smoke/run-routing-smoke.sh
contrib/satcompute/tests/integration/smoke/run-capacity-aware-smoke.sh
contrib/satcompute/tests/integration/regression/run-full-routing-regression.sh
```

路由回归验证静态重复输出、动态 `2 → 1 → 2` 候选、五种模式、online/replay、
1 秒导出与 2 秒网络应用的时间语义，以及生成 trace 的确定性回放。算法细节和
单项 checker 命令见
[模块运行说明](contrib/satcompute/README.md#地址与路由)。

## 输出与范围

默认输出目录为 `/tmp/satcompute-output`。正式实验应显式选择仓库外的持久目录；
生成输出、指标和诊断不得提交 Git。每次运行至少写出：

- `effective-config.json`：resolved 参数、输入路径/哈希、随机数和软件版本；
- `run-summary.json`：运行状态、拓扑、路由、传输、任务和 FlowMonitor 聚合；
- `network-flow-metrics.csv`、`network-flow-details.csv`：网络聚合与逐流证据；
- `ecmp-route-events.csv`：按 epoch、卫星和五元组记录的首次路由选择。

存在 workload 时增加 `transfer-summary.csv`；任务模式增加
`task-events.csv`、`task-summary.csv`、`compute-node-summary.csv`；size-aware
与 capacity-aware 模式增加各自 reservation/summary 文件。online 导出位于
`topology-trace/`。

`diagnosticMode=failure` 的直接传输只额外生成
`diagnostics/failure/flow-drop-reasons.csv`；未完成任务会生成九份 legacy 失败
证据，包括未完成任务/传输、ISL queue Drop、UDP socket Drop、链路集中度和
诊断摘要。完整文件合同见
[指标说明](contrib/satcompute/metrics/README.md)。

当前任务调度只支持单服务台、非抢占 FCFS；网络业务使用 UDP，不提供可靠
重传。卫星故障、checkpoint、备份、恢复、前端传输、IPv6 和 SRv6 尚未实现。
64000-byte payload 只用于降低大数据仿真事件数量，不表示真实卫星链路使用
64 KB 物理帧。

## ns-3 与许可证

本仓库保留官方 ns-3.48 历史，SatCompute 作为 `contrib/satcompute` 模块开发。
ns-3 的安装、模型和 API 文档见 [ns-3 官方文档](https://www.nsnam.org/documentation/)。
仓库代码按 [GNU GPL-2.0-only](LICENSE) 发布；第三方 nlohmann JSON 的 MIT
许可证随其源码保留。
