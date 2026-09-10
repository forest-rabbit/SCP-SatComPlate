# 1000 秒、66 星、8 任务 F2 空间风险验证

本场景为真实 F2-only generate 和 Monte Carlo 提供轻量任务环境，不用于声明
现实卫星的 SEU 或计算失效率，也不预先规定某个随机 run 必须恰好发生几次故障。
轨道使用 `tests/fixtures/topology/leo-66.csv`，仿真 0 秒通过 `--orbitStartOffset=302` 对齐空间加权
标定选择的 `302--1302s` 窗口；全部 66 颗卫星都具有
1,500,000 work-unit/s 算力。

本目录保留的固定任务角色为：

- 节点 17 和 16 各有一个覆盖确定性 F2 START 的 60 秒长任务及一个恢复后 10 秒任务；
- 节点 28 和 27 各有一个处于空间暴露窗口内的 20 秒中等任务；
- 节点 0 和 11 各有一个 5 秒短对照任务。

这些角色只描述负载形状。F2 故障由每秒实时位置、空间风险和独立 ns-3 随机流
决定；解析目标是多个 run 的平均故障数约为 2，不要求单个 run 等于 2。
`task-trace.json` 是平台输入，`workload-summary.json` 只保存生成元数据。

## 固定测试输入

本目录输入由 Git 保留，不再提供旧 profile 的重新生成入口。正式任务生成器只维护 LEO-66 正式实验；本 fixture 的角色、算力和历史验证参数保持不变。

## Generate

```bash
./ns3 run "satcompute \
  --simulationDuration=1000 \
  --randomSeed=1 \
  --randomRun=16 \
  --constellationConfig=contrib/satcompute/tests/fixtures/topology/leo-66.csv \
  --orbitStartOffset=302 \
  --maxIslDistance=6171353 \
  --delayMode=fixed \
  --fixedDelay=0.008 \
  --networkUpdateInterval=20 \
  --islBandwidthBps=2000000000 \
  --routingMode=global-capacity-aware-hrw \
  --computeProfile=contrib/satcompute/tests/fixtures/task/compute-profile-66.json \
  --taskTrace=contrib/satcompute/tests/fixtures/fault/f2/task-trace.json \
  --taskCompletionPolicy=report \
  --faultMode=generate \
  --faultEnableF1=0 \
  --faultEnableF2=1 \
  --faultEnableF3=0 \
  --faultTrace=/tmp/satcompute-f2-generate/fault-trace.json \
  --outputDir=/tmp/satcompute-f2-generate"
```

`randomRun=16` 只提供可复现生命周期示例：节点 17 在 `236s`、节点 16 在 `850s`
各发生一次 F2 START，任务 1/3 失败，恢复后的任务 2/4 与 暴露窗口/对照任务均完成。
该单次结果不能替代多 run 均值标定。F2 compute 故障只关闭算力 8 秒，不改变 ISL，
也不触发路由重算。显式开启概率审计时，F2-only generate 覆盖全部 RUNNING 检查点并匹配 136 条
模型概率和因果预测记录。

## 重复验证

保持上述参数与 seed/run 不变，将输出路径改为新目录后再次 generate；故障事件与
业务输出应一致。可加 `--faultProbabilityAudit=1` 比较独立预测与在线概率（仅 F1/F2）。
