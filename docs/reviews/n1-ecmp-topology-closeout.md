# N1 ECMP 拓扑子系统收尾审计

## 1. 收尾边界

```text
Temporary tag object: 6ec5956de62b556646dcb6e73db48f4d13f5cff0
Temporary tag target: a39f728b8e161e6c9f78ddaa1c8ab803017210c8
Refactor branch: refactor/n1-topology-layout
Date: 2026-07-28
```

`a39f728b8e161e6c9f78ddaa1c8ab803017210c8` 完成了 N1 ECMP 的 Git
引用与 CI 文档收尾，但尚未包含拓扑子系统目录重组。因此，当时创建的
`n1-ecmp-complete` 是临时 annotated tag，不是最终冻结点。

标签替换前已确认：

- GitHub 上没有与该标签绑定的 Release；
- 用户确认没有依赖该临时标签的外部下游；
- 旧 tag object、解引用后的提交和行为基线均已写入本审计；
- 旧目标提交仍由 `main` 历史永久可达。

本次只重组 SatCompute 自己的拓扑代码，不改变节点、链路、快照、路由、
流量、任务、指标或诊断合同，不删除任何 ns-3 上游内容，也不引入 Hypatia。

## 2. 重构前行为基线

基线使用静态四节点菱形拓扑、四条 transfer、固定 1024-byte UDP payload
和 `global-hash-per-flow` ECMP：

```text
topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static
simulationDuration=3
transferTrace=contrib/satcompute/input/traffic/test/diamond-4-static-transfers.json
transferChunkMode=fixed
transferPayloadBytes=1024
islMtuBytes=1500
transferLogMode=verbose
routingMode=global-hash-per-flow
ecmpHashSeed=1
```

运行结果为 4/4 transfer 完成，FlowMonitor `tx=16 rx=16 lost=0`。为排除
运行时长波动，`run-summary.json` 删除 `wall_clock_s` 后按键排序；两个 CSV
保持逐字节比较。重构前 SHA-256 如下：

| 输出 | SHA-256 |
| --- | --- |
| normalized `run-summary.json` | `1164bcfcee291a0c3f761c27f23c115b945d4f7bba37488c95593b0ed008d4ed` |
| `ecmp-route-events.csv` | `7dfc17e125eebd7471a266a73a652a062be0b04410f749f4743d31651f99c768` |
| `transfer-summary.csv` | `6960530ab502c5e287dfdf154c25762371c8ec78dba498e05edd0111666b80b9` |

最终验证必须在重构后重新运行同一命令，并满足规范化 JSON 相同、两个 CSV
逐字节相同。

## 3. 最终门禁与标签合同

Fast workflow 只由 pull request 触发，因此执行顺序是：

1. PR head 的 `SatCompute Fast Smoke` 成功；
2. PR 使用普通 merge 合入 `main`；
3. 最终 `main` SHA 自动触发的 `SatCompute Full Regression` 成功；
4. 对同一最终 SHA 手动触发的独立 Full Regression 成功；
5. 在该最终 SHA 创建并推送新的 annotated tag
   `n1-ecmp-complete`。

最终标签说明必须记录替换原因、旧目标 SHA、最终 merge SHA、PR URL、Fast
run URL、自动 Full run URL和手动 Full run URL。最终 Git 引用集合保持为
唯一远端分支 `main`，以及 `n0-complete`、`n1-complete` 和
`n1-ecmp-complete` 三个阶段标签。
