# N5C U：多 run 专项审计（Gate A）

依据项目外 `N5C_U_Refinement_Audit_Codex_Taskbook.md` 及用户随后确认。
从 PR #100 head `f5a479ae36c15e356cddd063235b33318856a86e` 建立
`feature/n5c-u-refinement`；PR #100 保持 open，旧七组结果只读。

## 运行前固定的范围

- 只跑 Deferred、busy=relocate 的 FA-FFP/full/noU；seed=1，run=11–15。
  run 11 复用验证后的原始三组，新增 run 12–15 共十二组，每组 800 任务、1300 s。
- 不改变 R/M/U、min-max、tie-break、START/Frequency、Recovery、Fault、Routing、输入或 deadline。
  只为正式运行入口增加显式 random-run，旧默认仍为 11。
- 不设置 T 阈值、5% 容差或自动升 Gate B 的规则。先报告各 run 的全部指标和配对差异，
  待共同审阅后决定是否研究 recent-U；当前 FULL 和 NO_U 定义保持不变。
- 相同故障条件指相同模型/参数/随机流分配和同 run，而非强制最终故障 JSON 一样。
  在线 generate 的时序反馈如造成差异，应输出并解释，不能切换 replay 消除差异。
- 实际 full/noU 的选点差异与同一 full 候选快照中去掉 U 的反事实排名分别统计。
  START 缺失、时刻变化和未成功准入不混成“U 直接改变选点”。
- busy 分母逐组取故障时具有 designated backup 的事件；catch 输出样本数与未追平数，
  全样本与同任务/同故障时刻/同类型的配对样本分开，未追平不记为 0。
- WU 与 eq-WU 分列；历史统计只用决策时刻以前的实际执行区间，不把预留等待计作 busy。
  task 140 仅作诊断样例，recent-U 尚未实现，其值和胜者标 N/A。

## 验证与结果

待回填。运行入口、单位和输出文件见测试 README；原始数据写入 `output/n5c-u-audit/`。
本轮不合并 PR #100、不运行额外 GitHub CI，也不提前创建 recent-U 实现。
