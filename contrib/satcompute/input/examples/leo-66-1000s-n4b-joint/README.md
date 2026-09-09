# 66 星、1000 秒、100 任务 N4B 联合验收

本场景用于 N4B 最终联合验收，不作为吞吐压力测试，也不用于重新标定现实故障率。
平台使用 66 星原生在线轨道、全部卫星算力、固定 8 ms ISL、20 秒网络更新和
Capacity-aware 路由；冻结 `orbitStartOffset=302`、`randomSeed=1`、
`randomRun=16`，同时启用 F1、F2 和 F3。

任务由统一生成器的 `n4b-joint-validation` 档产生，共 100 个：

- 节点 0、11、22 各有 6 个连续 10 秒任务和 1 个恢复后 2 秒任务；
- 节点 33 有 5 个连续 10 秒任务，形成临界边缘风险；
- 节点 44 有 4 个连续 8 秒任务，作为较短连续负载对照；
- 8 个窗口任务覆盖节点 17/16 的两次 F2 故障与恢复、节点 28 的 F2 空间暴露、
  节点 4 的 F3 故障及节点 5 的邻星对照；
- 其余 62 个 2–5 秒任务分散到 56 个非保留计算节点，覆盖完整仿真窗口。

所有任务仅使用 4096-byte 输入和 2048-byte 输出，使验收重点保持在故障、任务、
transfer、路由和概率审计的联合生命周期，而不是网络拥塞。
`task-trace.json` 是正式平台输入；`workload-summary.json` 只记录可复查的生成角色和
冻结条件，不是第二份平台配置。

## 重新生成输入

先导出选定轨道窗口的节点切片：

```bash
./ns3 run "satcompute \
  --simulationDuration=1 \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --orbitStartOffset=302 \
  --topologyOnly=1 \
  --topologySliceInterval=1 \
  --outputDir=/tmp/satcompute-n4b-joint-topology"
```

再生成确定性任务文件：

```bash
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --profile=n4b-joint-validation \
  --nodes-file=/tmp/satcompute-n4b-joint-topology/topology/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --seed=n4b-joint-66 \
  --output-task-trace=contrib/satcompute/input/examples/leo-66-1000s-n4b-joint/task-trace.json \
  --output-workload-summary=contrib/satcompute/input/examples/leo-66-1000s-n4b-joint/workload-summary.json
```

相同节点集合、算力文件和 seed 必须生成逐字节相同的两个 JSON。

## 四轮联合验收

正式 runner 依次执行：

1. generate，默认关闭概率审计，代表正常运行；
2. generate，显式开启概率审计，验证开关不改变 Fault Trace；
3. 重复 generate，开启概率审计，验证执行等价和概率对概率一致性；
4. 重复 generate，关闭审计并复用第 3 轮目录，验证陈旧概率文件被清理。

共同参数为：

```text
simulationDuration=1000
randomSeed=1
randomRun=16
orbitStartOffset=302
delayMode=fixed
fixedDelay=0.008
networkUpdateInterval=20
routingMode=global-capacity-aware-hrw
faultEnableF1=1
faultEnableF2=1
faultEnableF3=1
taskCompletionPolicy=report
```

旧 N4B 的 93/100 完成、82 条概率记录是旧 F1/NOTICE 下的历史结果，不再是
当前运行的固定计数。输入继续保留，回归验证新模型真实事件对应的任务/传输终态。
F2 的独立空间模型不变；F3 仍为节点 4 在 `829256867404 ns` 的永久故障。
F1/F2 不重算路由，F3 引起一次即时重算；末端所有资源账本必须归零。

审计覆盖所有 RUNNING 任务，模型与预测容差 1e-12。正常运行不生成或保留
概率/状态审计文件；具体断言由 `tests/integration/regression/run-n4b-joint-acceptance.sh`
维护，当前新模型结果见 [G3 冻结索引](../../../../../docs/n4c/reviews/G3-final-freeze.md)。

在仓库根目录构建后，可单独复现正式验收：

```bash
contrib/satcompute/tests/integration/regression/run-n4b-joint-acceptance.sh
```
