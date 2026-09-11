# Pre-N5C：Recompute 与 1+1 baseline

当前状态：实施中，尚未验收。仅使用 `feature/n5-baselines`，Draft PR 的 base 为 `n5`。

## 基线与冻结合同

N5B [PR #96](https://github.com/forest-rabbit/SCP-SatComPlate/pull/96) 已于 2026-09-11
合入 `n5@2f8e6a96a`，阶段 [CI](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/34557596746)
的构建、Python/C++ unit、smoke 和 maintained regression 全通过。旧分支已确认可达后清理。
`main@009788ca9` 不变。

- Recompute：无常态保护，故障后经公共 FFP/操作可行性选择节点，真实重传原 INPUT、从零计算。
- 1+1：首次 TASK_RUNNING 一次性申请资源受限副本，主任务不等待；未准入不重试，不创建第三副本。
- 正常副本不免疫；同纳秒完整 fault batch 处理后才允许幸存副本 takeover，随后沿用恢复 attempt 的 F1/F2 免疫，F3 始终有效。
- 沿用原始绝对 compute deadline；按时完成计算后的 RESULT 可继续交付，首个有效逻辑 RESULT 获胜并取消另一 attempt。
- planned 等待/WU 取决策时估计；actual 仅取实际预留等待和已执行 WU。失败任务不以 sum(WU)-W 产生负开销。
- 两种 baseline 首版仅 FFP，共享 Routing/Fault/Runtime；不占 checkpoint 备份池，active working-set 存储仍未独立量化。

实施顺序为 Recompute、1+1、全部本地维护测试、R5 兼容性，然后 R0-R4。
R5 必须复现 `output/n5b-architecture/B-ffp-compfrr-frequency`，不一致则停止报告。
六组均固定最终 800-task/1300s 场景和 online generate/seed=1/run=11；不同负载允许产生不同 F1 故障轨迹。
该场景是受控工程比较，不是无偏多 seed 论文统计。完成后停在 **PRE-N5C BASELINE AUDIT**，不自动合并或进入 N5C。

## Recompute 增量验证

独立 Recompute 已接入，checkpoint 方案的原后备分支保持不变。专项 278 项检查覆盖
零常态开销、FFP、INPUT/LocalDelivery、从零计算、deadline/F3/仿真结束截断和清理。
F3 fixture 的 planned catch-up 为 9,717 WU、actual 为 817 WU；deadline fixture 的
100,000 WU 计划只实际执行 99,997 WU，未执行部分不计费。
全部维护验证通过：Python 79 项、C++ 22 个程序、smoke 10 组、regression 4 组。
其中原恢复检查 1,539 项；N4B 联合 fixture 为 88 完成/12 失败、483 条概率匹配。
本地证据位于 `output/n5-baselines-validation/*-recompute.log`。
尚未开始 R0-R5 正式运行，以上不替代 R5 完整兼容性门禁。
