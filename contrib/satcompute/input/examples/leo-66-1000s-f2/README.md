# 1000 秒、66 星、8 任务 F2 验证

本场景验证 N4B 第二阶段的空间辐射连续暴露计算故障闭环，不用于宣称现实卫星的
SEU 或计算失效率。轨道使用 `synthetic-66.csv`，仿真 0 秒通过
`--orbitStartOffset=5695` 对齐 F2 标定选出的 5695–6695 秒窗口；全部 66 颗卫星都
具有 1,500,000 work-unit/s 算力。

任务由统一的 `generate-task-workload.py --profile=f2-validation` 生成，共 8 个：

- 节点 51 和 29 各有一个长任务覆盖固定 seed 下的 383 秒和 621 秒 F2 故障；
- 两个旧任务在故障开始时失败，不会在恢复后复活；
- 两节点各有一个 8 秒恢复后到达的新任务，均正常完成；
- 节点 40 和 18 各有一个任务覆盖风险-only 或终点截断风险；
- 节点 0 和 11 各有一个稀疏对照任务。

`task-trace.json` 是平台输入，`workload-summary.json` 只记录生成角色和固定验证条件。
故障区域、强度、阈值和恢复时间来自
[`fault-para.cc`](../../../fault/fault-para.cc)，不存在第二份故障模型 JSON。

## 重新生成任务输入

先导出同一轨道窗口的 0 秒节点切片：

```bash
./ns3 run "satcompute \
  --simulationDuration=1 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --orbitStartOffset=5695 \
  --topologyOnly=1 \
  --topologySliceInterval=1 \
  --outputDir=/tmp/satcompute-n4b-f2-topology"
```

再使用统一任务生成器：

```bash
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --profile=f2-validation \
  --nodes-file=/tmp/satcompute-n4b-f2-topology/topology/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --seed=n4b-f2-66 \
  --output-task-trace=contrib/satcompute/input/examples/leo-66-1000s-f2/task-trace.json \
  --output-workload-summary=contrib/satcompute/input/examples/leo-66-1000s-f2/workload-summary.json
```

相同节点切片、ComputeProfile 和 seed 会生成逐字节相同的两个文件。

## Generate

```bash
./ns3 run "satcompute \
  --simulationDuration=1000 \
  --randomSeed=1 \
  --randomRun=1 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --orbitStartOffset=5695 \
  --maxIslDistance=6171353 \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --islBandwidthBps=2000000000 \
  --routingMode=global-capacity-aware-hrw \
  --computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --taskTrace=contrib/satcompute/input/examples/leo-66-1000s-f2/task-trace.json \
  --taskCompletionPolicy=report \
  --faultMode=generate \
  --faultEnableF1=0 \
  --faultEnableF2=1 \
  --faultEnableF3=0 \
  --faultTrace=/tmp/satcompute-f2-generate/fault-trace.json \
  --outputDir=/tmp/satcompute-f2-generate"
```

固定 seed/run 下，预期产生节点 51（383 秒）和节点 29（621 秒）的两次实际 compute
故障，并保留 7 条风险-only 记录。第 1、3 号任务失败，第 2、4–8 号任务完成；F2
计算故障不改变 ISL，也不触发路由重算。

## Replay

将 generate 已确定的 trace 作为输入：

```bash
./ns3 run "satcompute \
  --simulationDuration=1000 \
  --randomSeed=1 \
  --randomRun=1 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --orbitStartOffset=5695 \
  --maxIslDistance=6171353 \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --islBandwidthBps=2000000000 \
  --routingMode=global-capacity-aware-hrw \
  --computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --taskTrace=contrib/satcompute/input/examples/leo-66-1000s-f2/task-trace.json \
  --taskCompletionPolicy=report \
  --faultMode=replay \
  --faultTrace=/tmp/satcompute-f2-generate/fault-trace.json \
  --outputDir=/tmp/satcompute-f2-replay"
```

回归逐文件比较 generate/replay 的故障事件、任务、传输和路由证据，并验证相同
seed/run 再次 generate 会产生逐字节相同的 `fault-trace.json`。
