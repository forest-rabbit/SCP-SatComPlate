# 66 星、1000 秒 F3 永久整星故障闭环

本示例验证 F3 本身，不运行任务：使用 66 星在线轨道与固定 ISL 候选，在
显式指定 `fixed_k`、`K=1` 条件下生成一颗永久失效卫星，重复运行核对事件一致性。F3 不依赖 ComputeProfile 或 TaskTrace，因此本目录没有伪造任务
输入文件。

## Generate

在仓库根目录完成构建后执行：

```bash
./ns3 run "satcompute \
  --simulationDuration=1000 \
  --randomSeed=1 \
  --randomRun=1 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --maxIslDistance=6171353 \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --islBandwidthBps=2000000000 \
  --routingMode=global-first \
  --faultMode=generate \
  --computeProfile=none --taskTrace=none --faultF3Mode=fixed_k \
  --faultEnableF1=0 \
  --faultEnableF2=0 \
  --faultEnableF3=1 \
  --faultTrace=/tmp/satcompute-f3/fault-trace.json \
  --outputDir=/tmp/satcompute-f3/generate"
```

冻结的 seed/run 应产生一条记录：稳定卫星 `62` 在 `33469258100 ns` 发生永久
`satellite` 故障。该记录没有 NOTICE、概率、持续时间或 RECOVERY；运行期立即关闭
卫星通信与计算，并在有效边集合变化后重算一次路由。仿真结束时活动故障数为 1。

## 重复验证

保持上述参数与 seed/run 不变，将输出路径改为新目录后再次 generate；故障事件与
业务输出应一致。可加 `--faultProbabilityAudit=1` 比较独立预测与在线概率（仅 F1/F2）。
