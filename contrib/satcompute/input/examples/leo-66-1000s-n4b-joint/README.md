# 66 星、1000 秒、100 任务 N4B 联合验收

> 本目录当前保存 `n4b-complete` 的 F2 空间风险修订前历史 fixture。下面的
> `5695s`、精确故障时刻、任务终态和 72 条预测记录只描述旧基线，不代表当前
> F2 空间风险模型；它们将在第三阶段联合回归时统一重建。

本场景用于 N4B 最终联合验收，不作为吞吐压力测试，也不用于重新标定现实故障率。
平台使用 66 星原生在线轨道、全部卫星算力、固定 8 ms ISL、20 秒网络更新和
Capacity-aware 路由；冻结 `orbitStartOffset=5695`、`randomSeed=1`、
`randomRun=16`，同时启用 F1、F2 和 F3。

任务由统一生成器的 `n4b-joint-validation` 档产生，共 100 个：

- 节点 0、11、22 各有 6 个连续 10 秒任务和 1 个恢复后 2 秒任务；
- 节点 33 有 5 个连续 10 秒任务，形成临界边缘风险；
- 节点 44 有 4 个连续 8 秒任务，作为未达到风险阈值的温热对照；
- 8 个窗口任务覆盖节点 51 的 F2 故障/恢复、F2 风险、节点 4 的 F3 故障和邻星对照；
- 其余 62 个 2–5 秒任务分散到 55 个非保留计算节点，覆盖完整仿真窗口。

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
  --orbitStartOffset=5695 \
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
3. replay，开启概率审计，验证执行等价和概率对概率一致性；
4. replay，关闭审计并复用第 3 轮目录，验证陈旧概率文件被清理。

共同参数为：

```text
simulationDuration=1000
randomSeed=1
randomRun=16
orbitStartOffset=5695
delayMode=fixed
fixedDelay=0.008
networkUpdateInterval=20
routingMode=global-capacity-aware-hrw
faultEnableF1=1
faultEnableF2=1
faultEnableF3=1
taskCompletionPolicy=report
```

冻结结果应包含：100 个任务中 94 个完成、6 个按故障合同失败；节点 0/11/22 的
设计内 F1、节点 22 恢复后仍热但未影响任务的一次额外 F1、节点 51 的 F2 compute
故障，以及节点 4 在 `829256867404 ns` 的永久 F3 整星故障。F1/F2 不重算路由，F3
只引起一次即时路由重算；仿真结束时 Capacity-aware 和 Size-aware 账本必须归零。

概率审计开启时，generate 在线模型与 replay 影子预测应匹配 72 条记录；正常运行
不得生成或保留 `fault-model-probabilities.csv`、`fault-predictions.csv` 和
`fault-prediction-summary.json`。具体断言由
`tests/integration/regression/run-n4b-joint-acceptance.sh` 维护。

在仓库根目录构建后，可单独复现正式验收：

```bash
contrib/satcompute/tests/integration/regression/run-n4b-joint-acceptance.sh
```

2026-08-31 的冻结验收中，四轮运行全部通过：normal/audit generate 的 Fault Trace
逐字节相同，audit generate/replay 的事件、任务、transfer、路由和 reservation 输出
逐文件相同，72 条模型/预测概率零缺失且误差不超过 `1e-12`；最后一轮还证明复用
目录不会残留三种审计文件。该 runner 已纳入 SatCompute 完整 regression 门禁。
