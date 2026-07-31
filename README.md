# SatCompute

SatCompute 是基于 ns-3.33 的纯星上动态网络仿真项目，只创建卫星节点和星间链路
（ISL）。拓扑由分离式 JSON 全量快照驱动；最短路、路由表生成和动态重算仍由
ns-3 原生 `GlobalRouteManager` 完成。

本仓库的 `main` 由原 `xw` 仓库的 `customer` 分支迁移而来。迁移后已移除
cluster、地面站、星地链路、CSV 建图、簇内/簇间路由和 SDN/OpenFlow 实验。

阶段进度、冻结提交和验收证据见 [`MILESTONES.md`](MILESTONES.md)。

## 目录

```text
contrib/satcompute/
├── wscript                # 独立 ns3-satcompute 模块和运行程序
├── satcompute.cc          # 主程序和 CLI
├── para.cc                # 当前有效默认参数
├── topology/
│   ├── satellite-topology.cc/.h # 卫星与 ISL 拓扑编排
│   ├── snapshot/          # 全量快照类型、读取与时间调度
│   └── link/              # 运行期 ISL 状态与设备队列事件
├── routing/               # 原生全局路由之上的确定性逐流 ECMP 选择
├── traffic/               # JSON NetworkTransfer UDP 运行时
├── task/                  # TaskTrace、FCFS 计算服务与任务协调
├── metrics/               # 聚合、逐流和 ECMP 路由证据
├── third-party/nlohmann/  # 共享的 nlohmann JSON 3.11.3（MIT）
├── tools/                 # CI、生成器与确定性检查器
└── input/
    ├── topology/examples/xw-66sat/
    ├── topology/tests/diamond-4-*/
    ├── topology/resources/ # 静态 ComputeProfile
    └── traffic/
        ├── workload/     # 正式规模与本地压力输入
        ├── test/         # NetworkTransfer CI 和回归输入
        └── task/         # TaskTrace
```

## 构建与基本运行

```bash
source .venv/bin/activate
./waf configure --disable-examples --disable-tests --enable-modules=satcompute
./waf build
./waf --run-no-build satcompute
```

SatCompute 是默认构建的 contrib 模块，不依赖 ns-3 examples 或 tests。只有需要
检查 ns-3 上游测试套件时，才单独重新配置 `--enable-tests`。

CI 分为 pull request 的 `SatCompute Fast Smoke` 与 `main`/手动触发的
`SatCompute Full Regression`。两级均调用可在本地直接运行的脚本，命令与
完整覆盖范围见
[`contrib/satcompute/README.md`](contrib/satcompute/README.md#ci-分级)。

默认运行 xw 66 星的 0–110 秒快照，不注入业务；这是正式保留的
`topology-only` 模式。

`para.cc` 保存默认值；命令行只覆盖当前运行。查看全部参数：

```bash
./waf --run-no-build "satcompute --PrintHelp"
```

主要参数：

```text
--topologyDir=<dir>                    JSON 全量快照目录
--simulationDuration=<s>               仿真时长
--transferTrace=<file>                 NetworkTransfer JSON；默认关闭
--computeProfile=<file>                topology/resources 下的静态计算能力
--taskTrace=<file>                     traffic/task 下的任务到达
--transferChunkMode=<fixed|size-aware>  NetworkTransfer 分包模式
--transferPayloadBytes=<uint32>         fixed 模式的 UDP payload 上限
--islMtuBytes=<uint16>                  所有 ISL 的 MTU
--islQueueBytes=<uint32>                所有 ISL DropTail 队列的字节容量
--transferLogMode=<summary|verbose|silent>
--taskLogMode=<summary|verbose|silent>
--routingMode=<global-first|global-hash-per-flow|global-hrw-per-flow|global-size-aware-hrw>
--ecmpHashSeed=<uint64>                 FNV-1a-64 seed 前缀
--outputDir=<dir>                       指标输出目录，默认 /tmp/satcompute-output
```

`computeProfile` 与 `taskTrace` 必须同时提供，任务模式不能同时指定
`transferTrace`。NetworkTransfer 与任务模式都使用 UDP；三项输入均为空时
运行纯拓扑模式。

## 统一场景生成

N2 使用统一场景配置一次生成 canonical 动态拓扑和静态计算能力：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.scenario.generate_scenario \
  --config contrib/satcompute/tools/generation/scenario/config/synthetic-66-compute-22.json \
  --output-dir /tmp/satcompute-scenario
```

默认提交场景使用固定单向时延 `8000 µs` 和
`2,000,000 Kbps = 2 Gbps` 的活动 ISL。固定时延是当前 N2 主线的实验抽象；
按距离计算单向传播时延的 `distance` 模式仍作为可选能力和回归合同保留。

输出中的 `topology/` 可直接传给 `--topologyDir`，
`resources/compute-profile.json` 可直接传给 `--computeProfile`。生成器先在
临时目录完成拓扑与资源检查，再原子发布；独立复查命令、完整字段含义、单位和
`topology/config` 的保留边界见
[`scenario/README.md`](contrib/satcompute/tools/generation/scenario/README.md)。

审查、CI 和临时验证写入 `/tmp`；正式实验应显式选择仓库外的持久目录。生成
结果不提交到 Git。

## 可选轨道可视化

统一场景可用独立、默认关闭的 Matplotlib 查看器展示三维卫星位置、轨道面、
计算/普通卫星和可选活动 ISL，也可通过 Pillow 输出 GIF。该工具不参与仿真、
scenario 哈希或默认依赖安装；使用时显式选择 `--group visualization`。入口、
配置、Headless 与时间语义见
[`orbit/README.md`](contrib/satcompute/tools/visualization/orbit/README.md)。

## NetworkTransfer

NetworkTransfer JSON 的 `schema_version` 必须为 `0.1`。每条记录只含：

```json
{
  "transfer_id": 1,
  "source_node_id": 0,
  "destination_node_id": 3,
  "size_bytes": 4096,
  "arrival_time_ns": 100000000
}
```

`source_node_id` 和 `destination_node_id` 是外部卫星 ID。JSON 不保存包数、
包长、发包间隔、MTU、速率或 UDP 端口。`fixed` 模式使用
`transferPayloadBytes`；`size-aware` 模式按 transfer 大小固定选择
`≤1 MiB → 1024`、`1–64 MiB → 8192`、`>64 MiB → 64000` bytes。最后一包
使用精确余量。每包发送后重新查询该五元组当前选中的首跳
PointToPoint 设备，并按该包的链路序列化时间调度下一包，不再使用应用层人工
发送速率。目的端口固定为 9000，并按每个源卫星的 `transfer_id` 顺序从
10000 派生唯一源端口。

已提交的 `workload-5000-varied.json` 包含 5000 个不同的 `size_bytes`，在
fixed 4096-byte cap 下覆盖 1–20 包。`mixed-large-ci.json` 含两个分级边界
探针和 10 条不小于 8 MiB 的不同大流量，最大为 125,000,000 bytes
（1 Gbit）；`mixed-large-local.json` 含 10 条 128 MiB–1 GiB 的本地完整
压力输入。两者均使用 size-aware 模式。

## 任务计算

输入按职责分离：静态计算能力 `ComputeProfile` 属于
`input/topology/resources/`，任务到达 `TaskTrace` 属于
`input/traffic/task/`。任务依次经历：

```text
PENDING → INPUT_TRANSFERRING → QUEUED → RUNNING
        → RESULT_TRANSFERRING → COMPLETED
```

每个任务派生两条 NetworkTransfer：输入传输 ID 为 `2 × task_id - 1`，结果传输
ID 为 `2 × task_id`。输入完整到达后才进入计算节点的非抢占、单服务台 FCFS
队列；服务时间严格按
`ceil(compute_work_units × 10^9 / compute_rate_work_units_per_second)` ns
计算。计算完成后立即启动结果传输。

```bash
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --computeProfile=contrib/satcompute/input/topology/resources/test/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-single-ecmp.json \
  --simulationDuration=10 \
  --taskLogMode=verbose \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --routingMode=global-hash-per-flow \
  --outputDir=/tmp/satcompute-task-single"
```

## 路由

- `global-first`：兼容模式，完整委托原生 `Ipv4GlobalRouting` 的首条路由行为。
- `global-hash-per-flow`：默认的 N1 基线，对排序、去重后的目标 service `/32`
  exact host candidates 做五元组 FNV-1a-64 取模选择。
- `global-hrw-per-flow`：对每个候选计算 HRW/Rendezvous 分数，在候选变化时
  保持稳定映射并实现最小流迁移。
- `global-size-aware-hrw`：先取得 HRW 前两名，再按活动 transfer 的声明字节
  预留选择物理下一跳；使用节点级 sticky 选择，并在发送完成后释放预留。

自定义层不复制 SPF、Dijkstra、LSDB 或 `LookupGlobal()`，也不启用原生随机
ECMP。每次完整快照调用原生 `RecomputeRoutingTables()` 后进入新的 route
epoch。大小感知模式不读取实时队列或链路利用率，也不执行中途主动迁移。当前
ECMP 验证只覆盖能够直接读取 UDP header 的未分片 IPv4 包；完整算法与边界见
[`contrib/satcompute/README.md`](contrib/satcompute/README.md)。

## Diamond 验证

```bash
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --simulationDuration=3 \
  --transferTrace=contrib/satcompute/input/traffic/test/diamond-4-static-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ecmp-static-a"
```

完整的静态重复运行、动态 `2 → 1 → 2` route epoch 验证和检查器命令见
[`contrib/satcompute/README.md`](contrib/satcompute/README.md)。

## 输出与范围

程序默认写入 `/tmp/satcompute-output`。审查、CI 和本地测试应使用独立的
`/tmp/satcompute-<case>` 目录；正式实验应通过 `--outputDir` 显式指定
仓库外的持久目录，例如：

```bash
--outputDir=/home/emsky/experiments/SatCompute/n2/run-001
```

程序不会自动判断运行属于测试还是正式实验。任何生成的 `output/`、指标或
诊断文件都不得提交到 Git。

`--outputDir` 下生成：

- `network-flow-metrics.csv`：所有 IPv4 流的聚合指标；
- `network-flow-details.csv`：FlowMonitor 五元组与 NetworkTransfer payload 的逐流指标；
- `ecmp-route-events.csv`：每个 `(route_epoch,node_id,five_tuple)` 的首次选路证据；
- `transfer-summary.csv`：声明大小、分包、发送、接收和完成时间；
- `task-events.csv`：任务的五次状态转换；
- `task-summary.csv`：每个任务的传输、排队、计算和完成时延；
- `compute-node-summary.csv`：计算节点的入队/完成数、忙时、最大队列和利用率；
- `run-summary.json`：运行配置及应用层、FlowMonitor 聚合结果。

三份任务 CSV 只在任务模式生成。

拓扑协议见
[`contrib/satcompute/input/topology/README.md`](contrib/satcompute/input/topology/README.md)。
流量输入的 `workload`/`test` 分类见
[`contrib/satcompute/input/traffic/README.md`](contrib/satcompute/input/traffic/README.md)。
当前只实现无抢占 FCFS 任务闭环；故障、checkpoint、备份和恢复语义尚未实现。
64000-byte payload 仅是降低大数据仿真事件数量的可扩展性配置，不表示真实
卫星网络使用 64 KB 物理帧。
