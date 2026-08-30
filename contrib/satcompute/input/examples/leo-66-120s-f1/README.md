# 120 秒、66 星、20 任务 F1 验证

本场景验证 N4B 第一阶段的自身状态计算故障闭环，不用于宣称现实卫星故障率。
星座使用 `synthetic-66.csv`，全部 66 颗卫星都具有 1,500,000 work-unit/s 算力。
任务由统一的 `generate-task-workload.py --profile=f1-validation` 生成：

- 节点 0、11、22 各连续处理 4 个 15 秒任务，分别在 56、66、76 秒达到临界温度；
- 第 4、9、14 号任务正在计算时发生故障并失败，恢复时间均为 8 秒；
- 第 5、10、15 号任务在各自恢复后到达并完成，证明失败的旧任务不会复活，但新任务
  可以继续运行；
- 节点 33 连续处理 3 个 15 秒任务，只形成风险 episode，不发生故障；
- 第 19、20 号短任务分散到节点 44、55，作为不会过热的稀疏对照。

`task-trace.json` 是平台输入，`workload-summary.json` 只记录生成角色和预期验证点。
故障内部参数来自 [`fault-para.cc`](../../../fault/fault-para.cc)，本目录不再保存第二份
故障模型 JSON。

## 重新生成任务输入

先让平台导出同一星座的 0 秒节点切片：

```bash
./ns3 run "satcompute \
  --simulationDuration=1 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --topologyOnly=1 \
  --topologySliceInterval=1 \
  --outputDir=/tmp/satcompute-n4b-f1-topology"
```

再使用统一任务生成器：

```bash
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --profile=f1-validation \
  --nodes-file=/tmp/satcompute-n4b-f1-topology/topology/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --seed=n4b-f1-66 \
  --output-task-trace=contrib/satcompute/input/examples/leo-66-120s-f1/task-trace.json \
  --output-workload-summary=contrib/satcompute/input/examples/leo-66-120s-f1/workload-summary.json
```

相同节点切片、ComputeProfile 和 seed 会生成逐字节相同的两个文件。

## Generate

```bash
./ns3 run "satcompute \
  --simulationDuration=120 \
  --randomSeed=1 \
  --randomRun=1 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --maxIslDistance=6171353 \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --islBandwidthBps=2000000000 \
  --routingMode=global-capacity-aware-hrw \
  --computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --taskTrace=contrib/satcompute/input/examples/leo-66-120s-f1/task-trace.json \
  --taskCompletionPolicy=report \
  --faultMode=generate \
  --faultTrace=/tmp/satcompute-f1-generate/fault-trace.json \
  --outputDir=/tmp/satcompute-f1-generate"
```

预期 trace 包含节点 0、11、22 的三次实际 compute 故障和节点 33 的一条风险-only
记录。计算故障不会改变 ISL，也不会触发路由重算。

## Replay

将上一轮已经确定的 trace 作为输入：

```bash
./ns3 run "satcompute \
  --simulationDuration=120 \
  --randomSeed=1 \
  --randomRun=1 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --maxIslDistance=6171353 \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --islBandwidthBps=2000000000 \
  --routingMode=global-capacity-aware-hrw \
  --computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --taskTrace=contrib/satcompute/input/examples/leo-66-120s-f1/task-trace.json \
  --taskCompletionPolicy=report \
  --faultMode=replay \
  --faultEnableF1=1 \
  --faultEnableF2=0 \
  --faultEnableF3=0 \
  --faultTrace=/tmp/satcompute-f1-generate/fault-trace.json \
  --outputDir=/tmp/satcompute-f1-replay"
```

回归会逐文件比较两轮的故障事件、滚动预测、任务、传输和路由证据，确保 generate
与 replay 执行结果一致。replay 的 F1/F2 开关用于重建预测影子模型，不重新抽样
trace 中已经确定的故障。
