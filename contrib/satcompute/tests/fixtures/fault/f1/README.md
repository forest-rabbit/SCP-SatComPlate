# 120 秒、66 星、20 任务 F1 验证

本场景验证 N4B 第一阶段的自身状态计算故障闭环，不用于宣称现实卫星故障率。
星座使用 `tests/fixtures/topology/leo-66.csv`，全部 66 颗卫星都具有 1,500,000 work-unit/s 算力。
本目录保留的固定任务角色为：

- 节点 0、11、22 各有 4 个连续 15 秒任务及后续任务，验证温升、中断和恢复；
- 节点 33 有 3 个连续 15 秒任务，节点 44、55 为短任务对照。

这是保持不变的旧功能输入，不是当前 G3 的故障数量标定输入。新的 F1 直接概率与
动态恢复会改变具体故障任务和时刻；不再承诺 task 4/9/14 故障或固定 8 秒恢复。

`task-trace.json` 是平台输入，`workload-summary.json` 只记录生成角色和预期验证点。
故障内部参数来自 [`fault-para.cc`](../../../../fault/fault-para.cc)，本目录不再保存第二份
故障模型 JSON。

## 固定测试输入

本目录输入由 Git 保留，不再提供旧 profile 的重新生成入口。正式任务生成器只维护 LEO-66 正式实验；本 fixture 的角色、算力和历史验证参数保持不变。

## Generate

```bash
./ns3 run "satcompute \
  --simulationDuration=120 \
  --randomSeed=1 \
  --randomRun=1 \
  --constellationConfig=contrib/satcompute/tests/fixtures/topology/leo-66.csv \
  --maxIslDistance=6171353 \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --islBandwidthBps=2000000000 \
  --routingMode=global-capacity-aware-hrw \
  --computeProfile=contrib/satcompute/tests/fixtures/task/compute-profile-66.json \
  --taskTrace=contrib/satcompute/tests/fixtures/fault/f1/task-trace.json \
  --taskCompletionPolicy=report \
  --faultMode=generate \
  --faultEnableF1=1 --faultEnableF2=0 --faultEnableF3=0 \
  --faultTrace=/tmp/satcompute-f1-generate/fault-trace.json \
  --outputDir=/tmp/satcompute-f1-generate"
```

trace 只记录本轮真实 compute START，不输出 risk-only。计算故障不会改变 ISL，也不会触发路由重算。

## 重复验证

保持上述参数与 seed/run 不变，将输出路径改为新目录后再次 generate；故障事件与
业务输出应一致。可加 `--faultProbabilityAudit=1` 比较独立预测与在线概率（仅 F1/F2）。
