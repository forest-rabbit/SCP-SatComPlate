# N4C G4：CompFRR Shadow Decision Evaluation

状态：实现与小规模门禁已完成；一次完整 800 任务验证待执行。完成后 STOP AT G4 REVIEW，
不进入 N5，不自动合并、运行阶段 CI 或清理当前功能分支。

依据用户补充的 `CompFRR_Backup_Frequency_Model_Simplified_v4.md` 和
`Codex_N4C_G4_CompFRR_Shadow_Decision_Evaluation_v2.md`。实现合同集中在
[protection README](../../../contrib/satcompute/protection/README.md)，不复制两份任务书。

## 实验基线

当前 branch：`feature/n4c-g4-shadow-decision-evaluation`，从 `n4c-g3-frozen` 开始。
该旧标签保留原 8 ms 历史，不移动。按用户随后确认，当前运行采用 **1 ms**，唯一真实行为
参考为 `output/n4c-g3-delay-1ms-20260909/fault-11/`，不是旧 8 ms 结果。

`para.cc` 默认对齐 1300 s、800 任务、66 星、每星 100000 WU/s、10 Gbps、1 ms、
capacity-aware HRW、size-aware、seed1/run11、deadline factor1.3。
F1 beta10/gamma1.5、F2 原参数、F3 controlled node62/time1027.055770726 s 不变。
概率 CSV 审计与 shadow 默认关闭，链路指标默认开启。保留原任务/placement/F3 JSON，
其中 F3 manifest 的 none 时序仍是历史 8 ms 来源，不伪改成 1 ms 测量。

## 门禁与范围

- G1 的全部 800 任务：C++/Python 的 K、每个合法 WU 边界及状态字节、5/10/20% 首目标一致。
- 纯模型覆盖成本档位、严格 START、零概率、负余量、空候选、确定性 tie-break、
  初始化完成/故障/提前完成、合法去重、追赶分项和初始化不双计等边界。
- 8 任务小场景比较 shadow off/on、重复 on、audit off/on；验证虚拟增量、串行 remote、
  pending 保留、动态频率、F3 单列及终止后停更。同 ns F3/初始化边界另做定向验证。
- 完整场景只运行一次 generate+shadow；先与已有 1 ms 参考逐项比较，再统计收益。
  CSV 比较原始字节，JSON 比较结构，仅排除 run-summary 中两个 wall-clock 字段。
  不使用 SHA-256，不新增 none/full-off、多种子或调参矩阵。

## 当前已知边界

这只是 shadow / analytical decision evaluation，不是执行备份后的 ns-3 性能。
节点/路径可用与存储容量为不约束假设；真实 10 Gbps 链路不被影子事件占用。
G4 正常保护成本覆盖全部 800 任务，主要恢复统计只含 F1/F2，F3 附录单列。
完整运行 deterministic 的重复证据来自小场景，不为此重跑第二次完整场景。
