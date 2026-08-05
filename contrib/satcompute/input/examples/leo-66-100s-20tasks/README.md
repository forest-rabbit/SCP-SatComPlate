# 100 秒、66 星、20 任务示例

本示例给出当前平台的一条完整可运行路径，但不复制星座和算力配置：

- 星座：`input/topology/constellations/synthetic-66.csv`，6 个轨道面，每面 11 星；
- 算力：`input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json`，
  22 个计算节点；
- 任务：本目录 `task-trace.json`，20 个任务、20,000,000 输入字节，任务在
  1–20 秒区间确定性到达；
- 仿真：100 秒、fixed 时延、20 秒网络更新、capacity-aware HRW。

## 构建

在仓库根目录执行：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
```

该配置不会启用 ns-3 上游 examples、全局 tests 或 `test.py`。

## 运行任务仿真

```bash
./ns3 run "satcompute \
  --simulationDuration=100 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --routingMode=global-capacity-aware-hrw \
  --computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --taskTrace=contrib/satcompute/input/examples/leo-66-100s-20tasks/task-trace.json \
  --outputDir=/tmp/satcompute-66-100s-20tasks"
```

成功运行时，标准输出以 66 颗卫星和 `"status":"completed"` 结束；
`run-summary.json` 应记录 20 个已完成任务和 40 个已完成传输。

## 生成同周期拓扑切片

下面的命令只推进轨道并每秒输出一次节点坐标和候选链路状态，不运行网络与任务：

```bash
./ns3 run "satcompute \
  --simulationDuration=100 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --topologyOnly=1 \
  --topologySliceInterval=1 \
  --includeFinalTopologyState=1 \
  --outputDir=/tmp/satcompute-66-100s-topology"
```

输出目录包含 0–100 秒共 101 对 `nodes_<time>s.json` 与 `links_<time>s.json`。
这些切片面向可视化和未来故障生成；正式任务仿真仍在线计算拓扑。

## 任务输入来源

`task-trace.json` 使用仓库任务生成器和 0 秒节点切片确定性生成，生成 seed 为
`leo-66-100s-20tasks`。任务总输入为 20,000,000 字节，计算量与结果大小均显式
写入，不需要运行时推导。
