# Multi-tree（Published FT Rule）

复现 Zhang 与 Yang 的 *Multi-tree genetic programming for adaptive dynamic fault-tolerant
task scheduling of satellite edge computing*（FGCS 175, 108099，
DOI: 10.1016/j.future.2025.108099）原文第 12 页 Fig.14 的第三棵 FT 决策树。
这是论文某一次运行得到的公开规则，不是重新训练 GP，也不是完整 MTGP 调度器：
不接入前两棵路由/排队树，不改变本平台 FCFS、primary placement 或路由。

## 规则与尺度

规则只在 primary 首次 `TASK_RUNNING` 决定一次 RS（故障后从零重计算）或
RP（一次性申请完整副本）。尚未进入计算的任务为 UNDECIDED，不算入 RS/RP 分母。

```text
IDDL > 9:
  CL < 4.5: RS
  CL < 8.2:
    TS < 2.2: RS
    CL < 5.0: RS
    otherwise: RP
  otherwise: RP
otherwise:
  FR > 0.3: RP
  CL < 3.5: RS
  otherwise: RP
```

保留全部严格比较和阈值，包括等于边界时的分支。论文第 13 页示例
TS=2.2、IDDL=5、FR=0.32 应选择 RP。

| 特征 | 本平台适配 |
| --- | --- |
| TS | INPUT 字节在冻结 800 任务中的 tied mid-rank `r`，映射为 `1+2r` |
| IDDL | 原始 compute deadline budget 的精确整数 ns，映射为 `5+8r` |
| CL | 当前 primary 节点普通等待队列各任务的 TS 之和，不包含正在执行的任务、恢复或副本 |
| FR | 当前 F1/F2 单次检查联合概率 `q` 的等效强度 `-log1p(-q)/checkIntervalSeconds` |

排名从 1 开始：`r=((rank_first+rank_last)/2-1)/(N-1)`；N=1 时 r=0.5。
相同原始值共用排名；非参考值在冻结 knots 之间线性插值，范围外取端点值。
deadline 调用现有 `TaskRuntime::ConfigureComputeDeadline`，保留 service/deadline 两次
向上取整。FR 不使用剩余任务窗口累计概率，不包含 F3，q=1 时为正无穷（CSV 写 inf）。
概率来自 canonical engine 的当前状态只读副本；不抽随机数、不推进真实模型。

`calibration/published-ft-scale.json` 是固定数据校准表，不是完整平台配置。
它记录来源路径、任务数、源提交和 lookup；不会按 seed/run 或故障结果重新校准，
不用 SHA256。重建验证比较文件内容，而非哈希。

## 论文与平台的边界

原文第 8 页实验的 TS 为 1–3 Mb，IDDL 为独立的 5–13；本平台的计算预算与任务量有关，
单调映射不会恢复论文特征间的独立性。原文 FR 是每次仿真内固定的 Poisson 强度；
这里用因果 F1/F2 动态等效强度，永久 F3 仅沿用公共执行机制。

原文 CL 表示 bits 单位计算负载，但没有足以复原的更新方程；第 13 页表 7/8 也不能
证明它就是等待任务大小之和。因此 queue-at-first-running 是明确的平台适配，
不能声称精确复现原始 CL。原文 Fig.2 的决策时刻、允许副本/重计算排队，与本平台
首次 TASK_RUNNING、资源约束准入不同。**保留的是公开树，不是原系统所有调度行为。**

## 文件与验证

- `multitree-feature-adapter.*`：冻结排名、TS/IDDL/CL/current FR 的纯映射。
- `multitree-published-rule.*`：原文 FT 树和可审计叶分支名。
- `multitree-decision-log.*`：一次因果 snapshot 与独立决策 CSV，runtime 与 preflight 共用。
- `multitree-controller.*`：单一 RS/RP owner，共享 transport ID、placement load 与终结账本；
  RS 调用公共从零恢复，RP 调用一次性完整副本执行器，不交叉 fallback。
- 测试仍位于 `tests/unit` 与 `tests/integration/regression`；preflight 只读观察运行，
  不创建 flow/event，不申请 storage/compute，不启用实际保护。

实施顺序：Stage A 映射/队列审计 → Stage B 共享 RS/RP 执行机制 → 六组完整 run11。
六组为 Recompute、1+1、CB-SAT、Multi-tree（均 FA-FFP，适用时 busy=Recompute），
以及 CompFRR-P 和 FA-FFP 两种 placement 的 CompFRR Selective+Relocate。
三轮均固定 800 任务、1300s、randomSeed=1、ecmpHashSeed=1；Run A/B/C 仅将
randomRun 设为 11/12/13，树与标定保持不变。online generate 的实际故障可以因
不同负载改变，不声称各组故障轨迹完全相同。不为分支覆盖或性能调尺度/阈值。

Stage A：`MULTITREE_MAPPING_READY`。200 项 C++ 映射检查通过；校准重建内容一致；
小场景 passive on/off 的 18 个原有结果文件语义一致。完整 OFF+generate run11 中
800 个任务全部到达首次 RUNNING：RS 584、RP 216，8 个叶分支均可达；CL 的中位数为 0、
P90 为 10.7935、最大为 21.2816（最大等待队列 10 个）。每条队列快照均由 task-events
独立重建核对。例：task 5 的等待任务 247/407/740/191，CL=7.98498，选择 RP；
task 8 的等待任务 302/791，CL=3.13141，选择 RS。
这是映射开发检查，不是 Multi-tree 实际保护效果。证据位于
`output/multitree/stage-a/`，原基线 11 组小场景快照保留于 `mechanism-before/`。

Stage B：共享 RS/RP 执行、真实 INPUT、LocalDelivery、一次性拒绝、完整故障批次后
接管、F3 和最终账本清空均有小场景验证。同纳秒完成测试发现的公共边界问题已获
单独批准修复（`7067f4bc9`）：到期任务先结算，队列下一项在故障批次结束后才可派发；
正常 FCFS 不变。修复后旧 11 组的 1984 个文件（含 1723 CSV）仍严格等价。

平台入口为 `--protectionScheme=multitree`，私有 placement 默认 FA-FFP；不继承
CompFRR INPUT、Frequency、checkpoint/tail 或 relocation。需要 online generate 和
F1/F2 当前状态；不能把 unavailable prediction 补零。RS/RP 在首次 RUNNING 冻结，
未到达该状态的任务保持 UNDECIDED，RP 准入失败不重试、不暗中转为 RS。

六组比较使用 `tests/integration/regression/run-multitree-comparison.py`，传入新
`--output-root`；`--random-run=11|12|13` 选择 A/B/C，默认 11；`--smoke` 为 15s
小门禁，默认完整 1300s，`--audit-only` 不启动仿真。
带宽对照使用 `--isl-bandwidth-bps=1000000000` 或 `100000000000`，仅允许
`--random-run=11`；其余参数与 10 Gbps Run A 一致，不修改平台默认值。
三轮汇总使用同目录的 `summarize-multitree-rounds.py --run-a ... --run-b ...
--run-c ... --output-root ...`，只读审计原始记录，不启动仿真；不会将不同带宽混成
三轮随机重复，也不会把未追平样本补零。
可同时传入 `--bandwidth-1gbps ... --bandwidth-100gbps ...` 汇总带宽对照。
100 Gbps CB-SAT 由用户明确取消时，须有 cancellation.json 与状态记录；
只审计已完成五组，取消项不补零、不当作完整执行。其他缺失或失败仍阻断汇总。
须保持干净执行提交，不覆盖已有输出。`comparison.json` 保留每项实际 WU/服务时间、
payload 与 catch 的来源；RP catch 根据实际连续副本服务追平 W_f 推导，不能把
接管时间直接当 catch 或将未追平记为 0。结果同时列完成数、流量、执行浪费、
常态与预留空等等效成本；不把未完成任务少做的工作当优化收益。

Stage C 已完成：10 Gbps 三轮 18 次、1 Gbps 六次、100 Gbps 五次，共 29 次完整执行；
另一次 100 Gbps CB-SAT 按用户要求取消。所有完整项通过账本审计，保持原始记录。
两种 CompFRR 在 10 Gbps 均 2400/2400；1 Gbps 自有方案 730/800，FA-FFP 对照 787/800，
暴露了单 reference pair 准入的限制，本轮不调参、不修算法。完整表格、分账和边界见
[整体结果](../../../../../docs/n5/reviews/Multi-tree-published-FT-comparison.md)。
