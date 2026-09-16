# Pre-N5 备份节点存在性审计

消费 N4 已有输出，检查真实 shadow START 和真实 F1/F2 计算故障前是否至少存在一个
备份计算候选。不选择节点，不占用资源，不调用仿真器，不实现 N5 production API。

## 文件和运行

| 文件 | 职责 |
|---|---|
| `analyze.py` | 冻结身份、日志完整性和原生链路证据校验，候选/提前量统计与 CSV/JSON 输出 |
| `audit_state.py` | 按过去事件还原计算忙闲、聚合健康状态与通信可达性；百分位和 retrospective 查询 |

在仓库根目录使用项目 uv 环境：

```bash
.venv/bin/python contrib/satcompute/tools/validation/pre-n5-backup-feasibility/analyze.py \
  --source output/n4-release-validation \
  --output-dir output/pre-n5-backup-feasibility
```

必须使用不存在的新输出目录；拒绝覆盖、与源目录重叠，或写入仓库非 `output/` 位置。
源文件缺失时明确停止，**不会自动重跑正式1300s仿真**。此适配器只接受当前冻结 N4 release，
不把 G4 参考目录或任意其他 workload 当作同一身份。

输入包括 `execution.json`、`run-summary.json`、task events/summary、fault events/impact、
link summary/1s windows，以及 shadow events/decisions/task-summary/faults/assumptions。
与 `main@n4-complete` 的六份正式输入逐字节核对，不添加 SHA-256。
结果记录工具 Git 提交及工作区是否有未提交内容；源输出和冻结输入只读。

## 候选与时间合同

- 非主计算节点；当前健康；**无 RUNNING 且队列为空**，对应 `ComputeService::IsIdle()`；
  当前真实 active graph 中存在从主节点到候选的路径。
- 任务状态消费 `task-events.csv` 的原始事件顺序，并与 task summary 的对应时间戳、终态核对。
- 健康采用 `fault-events.csv` 的 aggregate after-state；F1 恢复时若 F2 仍未恢复，节点仍不可用。
  `NOTICE` 不是故障，未来故障记录不参与当前候选过滤。
- 故障候选使用整数纳秒的左极限 `t^-`：同一纳秒的故障/恢复/任务转换均尚未应用。
  START 时若其他节点存在同纳秒忙闲/健康变化，或存在通信变化，由于跨 CSV 没有共同 event UID，
  分析器报错，不自行指定排序。当前正式源没有这类 START 歧义。
- 首个完整1s窗口给出原生初始 active 边，所有查询必须晚于该证据窗口。逐一校验整个运行中的
  原生链路可用时长，结合“初始路由一次、故障重算一次”验证自然边集合不变；通信故障按日志
  精确纳秒移除关联边。该方式不重写轨道模型，也不对一般动态链路做秒级插值猜测。
  后续窗口仅校验还原是否成立，不以未来状态筛选过去候选。
- 可达只表示存在路径，**不代表 Capacity-aware 可以立即准入或已有足够剩余带宽**。
  不加入距离、跳数、风险、存储或时延评分。

`persistent` 为事后强审计：同一节点全程健康、空闲（含队列为空）且可达。
正常任务观察至真实任务完成（RESULT 已交付），包含结束点；故障任务至故障左极限。
正常观察区间比 G4 在计算完成时停止保护更长，是保守统计，不代表实际保护必须持续到 RESULT。
它不是在线信息，零候选不影响主门禁。

初始化完成时间只读取真实 ON 事件。未 ON 的任务实际完成时间/余量留空；单独记录计划完成
时间及 `fault - planned completion`，不会把取消的初始化当作真实完成。百分位采用排序后
`(样本数-1)×分位` 的线性插值；整数纳秒先相减，再转秒展示。

## 输出和门禁

- `backup-candidates-at-start.csv`：START 候选节点与排除原因计数。
- `backup-candidates-at-fault.csv`：F1/F2 故障前候选，保留 ON/INITIALIZING 分类。
- `backup-candidates-persistent.csv`：全程候选与区间边界。
- `victim-protection-lead-time.csv`：START 到故障提前量、实际/计划初始化余量。
- `summary.json`：身份、分布、五个初始化未完成任务、F3附录及限制。

Gate A/B 分别要求所有实际 START / F1/F2 victim 的对应候选数大于0。
身份不符或无法精确还原时退出2，不输出假定健康的结果；候选门禁失败时保存明细并退出1。
F3 task120/node62单列，不纳入 F1/F2 门禁。G4 的 START 与真实 compute fault START 不混用。

通过只证明冻结普通任务运行轨迹下的**逐任务、逐时刻独立存在性**：不同任务可能竞争同一
候选，START 与 fault 的候选也可能不同。它不证明联合资源分配、备份状态已放置、存储足够、
恢复能满足 deadline 或真实备份流量可行。

## 聚焦测试

```bash
.venv/bin/python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_backup_feasibility.py' -v
```

测试使用小型 synthetic records，覆盖忙闲/队列、F3与临时故障、重叠恢复、可达性、
未来事件隔离、纳秒边界、persistent、分类、分位、错误输入与确定性输出。
不运行 ns-3 full regression，不启用 upstream examples/test.py。完成审计后停止，提交审阅。
