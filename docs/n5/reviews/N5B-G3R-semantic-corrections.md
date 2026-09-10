# N5B-G3R：语义修订与正式复验

状态：R1–R4 实现及维护测试通过，等待三组正式复验，不作为冻结证据。

## 范围与版本

沿用 `feature/n5b-compfrr-frequency`、Draft PR #96、base=`n5`。
`n5` 基线为 `b626a154d423219bc1503f060252962e1965b4cf`，修订起点为
`a3800c4b02a08ce3d5d4da73b420be31bd1c2f27`。
[原 G3 报告](N5B-G3-frequency-evaluation.md)及 `output/n5b-g3/` 完整保留为 pre-revision evidence。
本次只修订故障预测查询、frequency 接线、网络准入预览、checkpoint 迁移和对应指标/测试；
不改变冻结场景、故障参数/RNG、评分公式、成本档位、delta/n 网格、deadline 或链路配置。
具体文件以该起点至执行提交的 `git diff --stat` 为准。

## R1–R4 合同

- **R1**：TASK_RUNNING 立即评估 OFF→START，未启动者后续仍在故障检查点评估。
  复用模型副本，从下一真实全局抽样点预测，不新增 Bernoulli draw；同刻检查未执行时包含当前点，
  已执行时排除，任务预计完成时刻排除。START 仅进入 INITIALIZING，真实收齐及 cR 后才 ON。
  常规抽样前预测与 ON UPDATE/PAUSE 的提案→抽样→存活提交顺序保留。
- **R2**：eventual F3 不关闭 F1/F2；实际 F3 时刻另记因果快照，`F1F2_sampled=0`。
  未读未来 F3 时间。低温且位于 F2 域外的零概率保留为零。
- **R3**：保护与恢复统一使用 NetworkTransferEngine 的只读路径查询，capacity-aware
  复用真实完整 ECMP FindPath/reservation；非容量模式复用相应 next-hop policy 的隔离预览。
  NO_ROUTE 与 NO_ADMISSIBLE_PATH 分开；不预留资源、不重路由已有流，实际传输仍重新准入。
  START 的硬路径是 primary→remote、primary→local、local→remote；source→remote INPUT
  重放是 OFF 成本的软条件。不可用时不伪造带宽，P_finish>0 且 START 可行可以启动；零风险不强制启动。
- **R4**：有效 checkpoint 的原 remote 忙/计算不可用但存储可读时，先尝试稳定 ID 顺序的空闲目标。
  MIGRATE_REDO 真实传输 `CommittedStateBytes(rf)`；MIGRATE_TAIL 同时注册 state 与实际 tail
  记录之和（含 H），收齐后一次 cR。目标真实预留存储，旧状态保留到新目标可接管或任务终态。
  可行 checkpoint 优先于零起点重算；全部不可行才 RECOMPUTE。

细节见 [protection README](../../../contrib/satcompute/protection/README.md)、
[fault README](../../../contrib/satcompute/fault/README.md)、
[traffic README](../../../contrib/satcompute/traffic/README.md)。

## 验证与边界

本地 targeted build、21 个 C++ 测试程序、71 个 Python unit（无 skip）、10 组 smoke、
全部维护 regression 通过。聚焦 frequency runtime 3262 项、policy 211047 项、path 2025 项检查；
新增迁移对账测试独立核对完整状态字节与双接收/cR 屏障，不只依赖运行时 summary。
原 N4B 维护验收仍为 88 完成/12 失败、11 START、483 条模型/预测概率一致。
测试日志保留于本机 `/tmp/scp-g3r-{cpp-final,python-final,smoke,regression}.log`。

聚焦覆盖：开始即评估/开始即 START/稍后 START、真实网格同刻相位、无额外 RNG、
eventual F3 的正风险与模型零风险、首选路径忙而备选可准入、四种任务精确 committed state 字节、
直接 TAIL/REDO 不变、迁移 TAIL/REDO、目标忙/存储不足、迁移传输及 F3 失败、
旧状态保留、单次终态、实际 WU 和 reserved-idle、存储/网络锁清零。

路径快照不是容量预约，不估计未来队列释放；并行注册不保证两条流同时获得带宽。
已接受迁移发生真实传输失败时，仍遵守单次恢复合同，不偷偷增加第二次恢复。
迁移等待计 reserved-idle，RECOVERY_STATE 计实际网络账本；零字节 LLM 初始 state 不制造 UDP。
三组正式运行后再给出原 17 个 OFF 和 5 个 REMOTE_BUSY 锚点对照及完整资源结果。

阶段边界：不进入 N5C、不 Ready/merge、不清理分支、不触发阶段 CI；完成后推送原 Draft PR 并停止于 G3R。
