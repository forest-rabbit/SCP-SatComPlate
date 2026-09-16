# CompFRR policy-aware START admission 开发验收

## 结论

本轮只修正 Selective INPUT 与 START/Frequency 的执行顺序：先对 candidate 的实际
`source → remote` 路径执行原有 SER 判定，再令 SEND 的硬准入 INPUT 项为 0、DEFER 保持完整
`S/B_I`。Frequency 搜索空间、目标函数、START 收益式、Selective S 公式和 CompFRR-P 排名公式
均未改变。actual remote 选定后会重新判定并完成 post-batch revalidation；真实恢复仍等待
READY / IN_FLIGHT / refetch，不把准入假设当成运行时就绪。

物理 proactive INPUT 准入失败时，checkpoint 保持有效并退回 Deferred。新增审计文件
`compfrr-policy-aware-admission.csv`，不修改既有 CSV schema。

## 门禁

| 门禁 | 结果 |
| --- | ---: |
| Frequency policy | PASS，215032 checks |
| Frequency runtime / same-batch / candidate coverage | PASS，12247 checks |
| CompFRR-P placement | PASS，8574 checks |
| INPUT staging runtime | PASS，374 checks |
| Recovery runtime | PASS，2745 checks |
| Protection contract / path | PASS，18026 / 2545 checks |
| 1+1 / Recompute / Multi-tree baseline isolation | PASS，855 / 395 / 517 checks |
| Python scene、runner、audit | PASS |

## Run A 开发结果

两轮均为 CompFRR-P + CUMULATIVE + Selective + Relocate、seed=1、randomRun=11、800 tasks、
1300 s、online generate。5 Gbps 仅将 task 120 到达时间从 `1024.682825747 s` 调整为
`1024.042825747 s`，以保持其相对受控 F3 的发送阶段；10 Gbps 使用冻结原始 trace。

| 指标 | 5 Gbps | 10 Gbps |
| --- | ---: | ---: |
| 完成任务 | 800/800 | 800/800 |
| START | 407 | 409 |
| primary 运行中故障任务 | 84 | 83 |
| catch mean / P50 / P90 (ms) | 271.578 / 101.661 / 694.574 | 175.073 / 75.914 / 393.600 |
| FT 额外实际流量 (GB) | 124.401748 | 113.321868 |
| proactive INPUT (GB) | 20.111162 | 19.995838 |
| recovery INPUT (GB) | 8.375120 | 8.544910 |
| 总浪费 (M eq-WU) | 2.773592 | 1.896998 |
| 最忙链路全程平均利用率 | 2.3655% | 1.1996% |
| 全链路平均利用率 | 0.8516% | 0.4200% |
| final actual pair SEND / DEFER | 74 / 333 | 72 / 337 |
| runtime prefetch admission failure | 0 | 0 |

5 Gbps 相比 10 Gbps：FT 流量增加 9.78%，catch 均值增加 55.12%，P90 增加 76.47%，
总浪费增加 46.21%。两轮均没有 `legacy deadline-infeasible → policy-aware feasible` candidate，
因此本修订在 5/10 Gbps 不改变 START 集合；它修复的是此前低带宽诊断暴露的边界语义，
不是针对 5/10 Gbps 的参数优化。

## 证据范围

结果位于 `output/compfrr/policy-aware-input-admission/`，每轮均保存执行身份、完整 source diff、
运行日志和实际账本，并由 `development-comparison.json` 汇总。两轮是 dirty-worktree development
evidence，不是正式论文矩阵；未运行 1/2/100 Gbps、randomRun 12/13、其他方案或 CI。
