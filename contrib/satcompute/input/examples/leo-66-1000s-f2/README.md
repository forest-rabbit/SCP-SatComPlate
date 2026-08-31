# 1000 秒、66 星、8 任务 F2 空间风险验证

本场景为真实 F2-only generate/replay 和 Monte Carlo 提供轻量任务环境，不用于声明
现实卫星的 SEU 或计算失效率，也不预先规定某个随机 run 必须恰好发生几次故障。
轨道使用 `synthetic-66.csv`，仿真 0 秒通过 `--orbitStartOffset=5210` 对齐空间加权
标定选择的 `5210--6210s` 窗口；全部 66 颗卫星都具有
1,500,000 work-unit/s 算力。

任务由统一生成器的 `f2-validation` profile 产生：

- 节点 51 和 29 各有一个 60 秒长任务及一个 10 秒后续任务；
- 节点 40 和 18 各有一个 20 秒中等任务；
- 节点 0 和 11 各有一个 5 秒短对照任务。

这些角色只描述负载形状。F2 故障由每秒实时位置、空间风险和独立 ns-3 随机流
决定；解析目标是多个 run 的平均故障数约为 2，不要求单个 run 等于 2。
`task-trace.json` 是平台输入，`workload-summary.json` 只保存生成元数据。

## 重新生成任务输入

先导出同一轨道窗口的 0 秒节点切片：

```bash
./ns3 run "satcompute \
  --simulationDuration=1 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --orbitStartOffset=5210 \
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

相同节点集合、ComputeProfile 和 seed 会生成逐字节相同的两个文件。

## Generate

```bash
./ns3 run "satcompute \
  --simulationDuration=1000 \
  --randomSeed=1 \
  --randomRun=16 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --orbitStartOffset=5210 \
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

`randomRun=16` 只提供可复现示例，当前恰好产生 3 次故障，不能用来替代多 run
均值标定。F2 compute 故障只关闭算力 8 秒，不改变 ISL，也不触发路由重算。

## Replay

将 generate 已冻结的 trace 作为输入，并保持相同轨道、任务和 F2 开关：

```bash
./ns3 run "satcompute \
  --simulationDuration=1000 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --orbitStartOffset=5210 \
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
  --faultEnableF1=0 \
  --faultEnableF2=1 \
  --faultEnableF3=0 \
  --faultTrace=/tmp/satcompute-f2-generate/fault-trace.json \
  --outputDir=/tmp/satcompute-f2-replay"
```

replay 不重新计算空间风险或抽样，只执行 trace 中已经确定的 NOTICE、START、
RECOVERY 和 NOTICE_CLEAR。概率审计仍为按需开关，正常运行默认关闭。
