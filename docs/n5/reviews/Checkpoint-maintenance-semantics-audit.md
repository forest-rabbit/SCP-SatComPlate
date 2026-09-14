# 检查点维护语义修复

基线：`08236b8af`，分支 `fix/checkpoint-maintenance-semantics`。本轮接受的范围是 ON 维护、对象生命周期及受影响 U 实验验证；不改 START/recovery 准入、Frequency 公式、节点选择、U、故障随机流、INPUT、路由、deadline 或场景。

## 原实现审计

| 问题 | 基线行为 |
|---|---|
| local/remote busy | `BuildResources` 同时要求两者 ComputeService available/idle，导致 ON PAUSE |
| future capture | `PauseFutureProtection` 取消未来 captureEvent，清空 nextTarget |
| 已捕获但未生成 | cL 回调保留；存储在生成完成后才预留，失败时 triggered 已前进，留下缺口 |
| 已生成待注册 | 下一 ns 的 canonical 注册继续；注册异常却会 Stop 整个保护 |
| L1 传输中 | 普通 PAUSE 不取消；真实失败释放预留，未收到的序列阻挡后续连续进度 |
| local 已收到待 batch | 留在 pool；futurePaused 阻挡形成新 batch |
| remote batch 传输中 | 保留真实流；真实失败设 batchBlocked，但后续 UPDATE 会清除此标志 |
| merge 已排队 | cR 回调保留；故障时仍按严格 pre-fault 有效性冻结 |
| 空闲后的恢复 | 后续 epoch UPDATE 恢复；仅 NO_ADMISSIBLE_PATH 另有真实 capacity-release 重评估 |
| 原测试覆盖 | 有真实 PAUSE、在途 batch、不变量和恢复测试；没有 CPU busy 时 local 继续推进的直接测试 |

实际 NetworkTransferEngine 注册只校验流定义，`StartTransferNow` 后可能进入 WAITING_ADMISSION；这不是传输失败。维护的准入前只读路径检查不能替代 NetworkTransferEngine 的最终资源竞争。

## 已确认的执行合同

- 只解除 ON maintenance 的 CPU idle/compute-availability 耦合；F1/F2 不使存储失效，F3 整星失效仍使其对象不可读。START 与 recovery compute 不变。
- 真正策略 PAUSE 仍停止创建未来操作；资源导致新配置不可准入时保留最后 committed δ/n/quota，各流水线按真实资源独立阻塞。暂停不删除已创建操作。
- local record 在捕获前取得存储预留，再推进不可变序列；未准入时不计 cL、不推进 triggered，恢复时从当下合法进度选择未来目标，不补历史快照。
- 已创建的预留/生成/待注册/在途/merge 分别保留；未注册请求可重试，真正终结失败的流不自动假装未准入，不丢失实际已发送字节。不能跳过未收到的序列。
- 不新增完整本地状态或重复 tail。现有并行 INPUT、remote state、local tail 恢复链路保持。

## 历史影响

只读工具：`tests/integration/regression/audit-checkpoint-maintenance.py`。
本地结果：`output/audits/checkpoint-maintenance/`，含 pause-events、affected-runs、staleness-at-fault 和 summary。

15 组 U 均有 busy 联动 PAUSE，每组 30–43 条决策；其中很多任务没有故障。另只读扫描的 R5/R7 N5C、noR/noU/noM 同样受影响，**不因此授权补跑这些额外 baseline**。

旧结果 KEEP 必须证明维护轨迹与资源账本等价；不能只看 pause 后是否故障。捕获前预留也改变 cL 期间的实际 reserved byte-time，不能混用旧账本。未记录的 pending target、逐路径和 idle 快照记 UNKNOWN，不能由后续故障反推。U 五轮三方比较因此需全部重新验证；历史输出保留，不覆盖。

## 验证进度

代码语义、历史影响审计与最小修复完成。目标模块构建、全部维护的 C++ tests 和 smoke suites 通过；
Frequency runtime 11,957 项、路径 2,545 项、恢复 2,745 项检查通过。Python 184 项（183 通过、1 个既有 skip）；
新增离线维护审计在 42 个已有本地 fixture 上通过。未运行 upstream examples/global tests 或 GitHub CI。

测试覆盖真实 peer compute busy 而 maintenance 继续、START 仍要求 idle、local/remote 单侧路径/存储/配额拒绝、
已有 local records 在 local 路径阻塞时继续 remote batch、资源恢复后连续序列、已有 cL/待注册/在途 batch 在策略 PAUSE 中继续、
失败流实际发送记账及不自动重发，以及原有 F3/同纳秒快照/并行恢复测试。

正式 U 补跑尚未完成。运行脚本 `run-checkpoint-maintenance.py` 比较旧命令除输出路径外逐项一致，
仅生成 15 组 U（每组 800 tasks/1300s、online generate），最多 8 组并发，保存 clean execution commit，拒绝覆盖。
输出根目录 `output/checkpoint-maintenance-fixed/`；`analyze-checkpoint-maintenance.py` 检查维护序列/字节账本、
阻塞区间、故障时 local/remote WU gap、既有恢复与独立 Frequency/N5C/Rational-U 审计，并重算三方 paired catch 和单任务长尾排除。
完成前不得宣布机制验收或 U 比较收口；FULL 默认不变。
