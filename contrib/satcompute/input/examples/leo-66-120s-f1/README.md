# 120 秒、66 星、F1 小规模验证

本场景用于验证 N4B 第一阶段的自身状态计算故障闭环，不用于宣称现实卫星故障率。
它沿用当前 50--60 秒连续计算达到临界温度的候选参数，并刻意把随机故障强度设为
0，从而分别稳定构造两类证据：

- 节点 0 连续处理 6 个约 10 秒任务，先产生风险通知，再在临界温度触发一次可恢复
  compute 故障；
- 节点 11 连续处理 5 个约 10 秒任务，进入风险后正常降温，产生一条未故障的风险
  episode；
- 其余 9 个短任务分散在不同计算节点，作为不会过热的对照组。

星座使用 `input/topology/constellations/synthetic-66.csv`，算力使用全部 66 个计算节点
的 `xw-66sat-static-2g-all-compute-profile.json`。`fault-model.json` 是功能验证配置，
其中 10 秒恢复时间是平台场景参数，不是实测卫星恢复时间。

## Generate

在仓库根目录执行：

```bash
./ns3 run "satcompute \
  --simulationDuration=120 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --routingMode=global-capacity-aware-hrw \
  --computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --taskTrace=contrib/satcompute/input/examples/leo-66-120s-f1/task-trace.json \
  --taskCompletionPolicy=report \
  --faultMode=generate \
  --faultModelConfig=contrib/satcompute/input/examples/leo-66-120s-f1/fault-model.json \
  --faultTrace=/tmp/satcompute-f1-generate/fault-trace.json \
  --outputDir=/tmp/satcompute-f1-generate"
```

预期生成一个节点 0 的实际故障和一个节点 11 的风险-only episode。计算故障不会改变
ISL 或触发路由重算；节点 0 上已经失败的旧任务不会在恢复后复活。

## Replay

将上一轮生成的 trace 作为确定性输入：

```bash
./ns3 run "satcompute \
  --simulationDuration=120 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --routingMode=global-capacity-aware-hrw \
  --computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --taskTrace=contrib/satcompute/input/examples/leo-66-120s-f1/task-trace.json \
  --taskCompletionPolicy=report \
  --faultMode=replay \
  --faultTrace=/tmp/satcompute-f1-generate/fault-trace.json \
  --outputDir=/tmp/satcompute-f1-replay"
```

项目回归会比较两轮的故障事件、任务、传输和路由证据，确保 generate 与 replay
执行结果一致。
