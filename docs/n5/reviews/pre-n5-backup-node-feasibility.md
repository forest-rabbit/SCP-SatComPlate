# Pre-N5 备份节点存在性审计

2026-09-10。**Gate A = PASS；Gate B = PASS；STOPPED BEFORE N5。**
这是普通任务冻结轨迹下的离线、逐任务候选存在性审计，不是备份节点选择或资源联合分配验证。

## 身份与复现

- 基线 `main@n4-complete`：`009788ca9c5042160e50a014c6d657e785f225f3`。
- 分支：`feature/pre-n5-backup-node-feasibility`；工具运行提交：`48418bd809c439d569b9e6a106dc098c89432a85`，运行时工作区干净。
- 正式输入：[LEO-66](../../../contrib/satcompute/input/experiments/leo-66/README.md)：66星、800任务（240/240/240/80），
  每星100000 WU/s、10Gbps、fixed单向1ms、seed1/run11、到达1..1050s、结束1300s、deadline系数1.3。
- 只读源：`output/n4-release-validation/`，原运行提交 `a78ee0b4d646f5d88195eb80e55bc61b273c0275`。
  任务/故障/链路日志和shadow五份文件均存在；核对717完成/83失败，F1/F2/F3 START=84/2/1。
- shadow assumptions：`kind=shadow / analytical decision evaluation`、B=1250000000 Byte/s，
  无真实包/CPU预留；原有node/path可用、存储不约束假设仍如实记录。此次另外依据真实日志检查候选，
  不把这些假设当作健康或可达性的证明；完整身份保存于 `summary.json`。

```bash
.venv/bin/python contrib/satcompute/tools/validation/pre-n5-backup-feasibility/analyze.py \
  --source output/n4-release-validation --output-dir output/pre-n5-backup-feasibility
```

结果位于 `output/pre-n5-backup-feasibility/` 的四份CSV和 `summary.json`，均被gitignore忽略。
重复分析使用新目录 `output/pre-n5-backup-feasibility-repeat/`，五份结果逐字节一致。
**没有新增1300s仿真**，没有修改N4输入、故障参数、随机流或业务逻辑；原始输出文件大小与修改时间未变。

## 候选结果

身份检查：387 START、382 ON；82个F1/F2直接RUNNING victim中77 ON、5 INITIALIZING、0 OFF。
候选排除自身，要求当前健康、无RUNNING且队列为空、主节点到候选存在active路径。

| 检查集合 | 样本 | 零候选 | min | P10 | P25 | median | P75 | P90 | max |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| START | 387 | 0 | 60 | 61 | 62 | 63 | 63 | 64 | 65 |
| F1/F2故障前 | 82 | 0 | 60 | 61 | 62 | 63 | 63 | 63 | 64 |
| persistent（事后） | 387 | 0 | 59 | 60 | 61 | 62 | 62 | 63 | 64 |

ON victim的候选 min/median/max=60/63/64；INITIALIZING为61/62/63，均没有零候选。
当前任务集中于热点，其他卫星大量空闲；不能把候选数量直接当成已预留的备份资源。

原生网络242条有向ISL，共314600个1s可用时长窗口全部核对。自然边集合保持不变，
F3在1027.055770726s切断node62关联的6条有向边，仅引起一次故障路由重算。
候选还原只应用检查时刻之前已经发生的通信变化，不重新生成轨道。
START无非主节点同纳秒忙闲/健康或通信变化歧义；故障使用整纳秒左极限，不使用故障后状态。

`persistent` 要求同一候选全程健康、空闲（含队列为空）且可达。正常任务区间至RESULT交付，
包含结束点；故障任务至故障左极限。它是 retrospective 强审计，不是在线信息，也不是硬门禁。

## 保护提前量与五个初始化未完成任务

82个victim的 START→fault 提前量（秒）：min=0.027651340，P10=0.4081751355，
median=2.2514956305，P90=5.4262075855，max=8.067042887。

| task | 主星 | START时刻(s) | fault时刻(s) | 提前量(s) | 计划初始化耗时(s) | 距计划完成尚差(s) |
|---|---:|---:|---:|---:|---:|---:|
| 247 | 11 | 943.873765116 | 944 | 0.126234884 | 0.132852521 | 0.006617637 |
| 548 | 9 | 154.903297940 | 155 | 0.096702060 | 0.163088404 | 0.066386344 |
| 564 | 12 | 71.797684055 | 72 | 0.202315945 | 0.215804195 | 0.013488250 |
| 591 | 9 | 205.972348660 | 206 | 0.027651340 | 0.056250004 | 0.028598664 |
| 734 | 9 | 105.941069707 | 106 | 0.058930293 | 0.369507353 | 0.310577060 |

五个均为F1，均在首次计算开始的同一纳秒START，初始合法状态字节为0。
现有shadow初始化耗时主要是复制INPUT的 `S/B+cR`；故障发生得早于这段窗口结束，
并非没有备份候选或额外延迟了START。实际初始化完成时间留空，表中仅为计划时间差，
没有把已取消的初始化当作后来完成，也没有调整模型使其进入ON。

F3附录：task120/node62，故障前有62个候选、shadow为OFF；不计入主门禁，不能据此声称能恢复F3。

## 验证与限制

- 定向构建通过；新增24项聚焦synthetic测试通过，全部53项Python测试通过，原生生成器复现实际执行、无skip。
- 独立以task-summary的QUEUED/计算结束区间、故障区间和真实链路端点交叉计算：469个START/fault集合与387个persistent集合全部一致。
- 校验冻结六份输入字节、事件/summary时间戳和终态、故障恢复记录、START/ON/直接victim身份；缺字段或边界不确定时报错。
- 两次已提交工具上的离线分析结果逐字节一致；未跑完整ns-3回归、GitHub CI或新的网络仿真。
- 通过仅证明本场景各时刻独立存在候选；不证明多任务同时分配、备份状态已放置、同一备份可持续接管、
  剩余链路容量/存储足够或真实恢复满足deadline。F2没有直接victim，不能外推其故障接管覆盖率。

工具与边界详见[分析器README](../../../contrib/satcompute/tools/validation/pre-n5-backup-feasibility/README.md)。
本阶段提交审阅后停止；N5真实checkpoint、传输、预留、节点选择和恢复执行均未开始。
