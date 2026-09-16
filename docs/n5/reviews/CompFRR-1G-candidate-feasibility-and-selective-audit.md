# CompFRR 1 Gbps：候选覆盖与 START–Selective 离线审计

2026-09-15；Stage 1 完成，停在人工审阅点。结论为 **COUNTERFACTUAL_EVIDENCE_INCOMPLETE**，不是“没有问题”或“已经修复”。

本轮仅使用已有 1 Gbps / seed1 / randomRun11 / 800 任务 / 1300 s 的 P 与 FA-FFP 结果。两组执行版本均为 `ebd8a4748893d538c85f0602ab207b74971c93ac`；当前分支 `feature/multitree-published-ft`。不改生产代码、默认值、故障、工作量、Frequency、SER、排名、路由或恢复合同；没有新仿真、CI、提交或推送。

## 结论

优先问题是 **single-reference 的候选检查覆盖限制**。代码与日志均确认：P 在 reference 硬拒绝后结束本次决策，尚未执行自己的 remote 排名；不能将这类失败直接归因于 P 的打分函数。

FA-FFP 的继续搜索、source=remote 的 LocalDelivery 和成功恢复提供了明确的对照证据。但两组是独立的在线运行，任务时刻/故障相同不等于存储、带宽占用等完整资源快照相同。现有日志不足以证明 P 当时每一个被跳过候选满足全部硬约束，因此不能精确给出“修改后必然救回多少任务”。

START/Selective 顺序与真实带宽限制也存在，不能用“提前 SEND”一项解释全部差距。本轮不改变原来的一次 reference Frequency 求解、固定 local 合同，也不把 Selective 判为 SEND 等同于可成功恢复。

## 1. 范围与计数

事件键为 `(task_id, fault_epoch_time_ns, decision_trigger)`。下面的事件不是独立任务，也不是故障次数。

| 口径 | 结果 |
| --- | ---: |
| OFF 决策事件 | 3,673 |
| OFF 未 START 事件 | 3,604 |
| 其中硬约束拒绝事件 `N_total_start_rejected` | 3,444 |
| deadline / initialization-too-late / no-capacity | 3,414 / 28 / 2 |
| 检查一对 reference 后硬拒绝 | 3,442 |
| 这 3,442 次中仍有其他通过节点/路径初筛的 pair | 3,442 |
| 未做 Frequency 硬检查的 pair 次数，跨事件累加 | 628,239 |
| 当次已确认路径准入集合为空 | 2（均为 task 158） |
| 曾遇到硬拒绝的独立任务 | 734 |
| 上述任务后来 START / 始终未 START | 4 / 730 |
| 原有重点队列：OFF 状态下计算故障并最终失败 | 69 |

其余 160 个 OFF 非 START 事件为 `OFF_NOT_MORE_EXPENSIVE` 138 次、`NO_FAULT_AFTER_INIT_READY` 22 次，不冒充硬约束拒绝。
3,444 个硬拒绝中，709 个来自 TASK_RUNNING，2,735 个来自 FAULT_EPOCH；其中 69 个同时遭实际故障命中，不能当成故障批次后仍可提交的 START 机会。

`N_has_alternative_feasible_candidate`、`N_no_feasible_candidate_found` 的真实总数均为 UNKNOWN。可证明的替代可行事件为 0，可证明集合为空的事件为 2，其余 3,442 个候选集合判定未知。前一个 0 表示证据不足，**不表示实际不存在替代候选**。reference coverage loss 不能给点估计，仅保留无信息界 `[0,1]`。

历史日志只保留候选过滤计数与实际检查 pair，没有逐一保存被跳过 pair 的身份、路径跳序列及完整资源快照。因此 `candidate-audit.csv` 对未知候选使用明确的 aggregate 行，而不是虚构逐节点记录。该输出不能解释为已完成全候选资源重建。

## 2. 现行代码为什么会这样

代码定位：

- [EvaluateOffPairs](../../../contrib/satcompute/protection/policy/compfrr/compfrr-controller.cc)：先做节点、三条状态路径以及 Deferred INPUT 第四路径初筛。P 分支只取 `pairs.front()`，执行一次 Frequency；只有 START 成立才调用实际 remote 排名。FA-FFP 分支在硬拒绝后继续检查后续 pair，但不按目标函数跨 pair 挑选。
- [BuildResources（同一文件）](../../../contrib/satcompute/protection/policy/compfrr/compfrr-controller.cc)：已经按当前 candidate 的 source→remote 路径估计 INPUT 带宽，source=remote 已采用本地交付；并非缺少 target-aware 建模。
- [Frequency](../../../contrib/satcompute/protection/policy/compfrr/frequency/compfrr-frequency-policy.cc)：Deferred INPUT 代价进入 deadline 检查；该检查先于存储可行性检查。deadline 拒绝不能推导存储也不可行。风险参与收益，不存在一个独立的风险硬阈值。
- [实际 remote 适配](../../../contrib/satcompute/protection/policy/compfrr/compfrr-placement-adapter.cc)：reference START 后保留 local 和配置，再进行 P 的 remote 可行性/排名。
- [Selective](../../../contrib/satcompute/protection/policy/compfrr/input/selective-input-snapshot.cc)：actual pair 确定、批次后重验通过时冻结 snapshot；初始化真正准入后执行原有 SER。拒绝 START 不会留下真实 prefetch。

所以，节点/路径初筛通过不等于 Frequency 硬可行；reference 失败也不等于所有候选失败。reference 根本没有选出配置时，“同一固定配置下可行”和“另一个配置下可行”也不能混为一项。

## 3. FA-FFP 对照

P 完成 **730/800**，FA-FFP 完成 **787/800**；净差 57。实际为 **58 个仅 FA 完成、1 个仅 P 完成（task 302）**，不是 57 个已经证明可以被修复挽救的任务。

- 58 个仅 FA 完成的任务全部选择 source=remote，其中 57 个两组具有相同主节点、故障时间和故障类型/来源。
- P 实际保护 69 个任务，其中 11 个 source=remote；FA 实际保护 373 个，其中 316 个 source=remote。
- P 曾硬拒绝的 734 个任务中，308 个在 FA 运行中 START；301 个拒绝事件能对应到 FA 相同事件键的真实 START。
- task 120 在初始 INPUT 阶段遭 F3，尚未 TASK_RUNNING，两组均失败；它不属于上述 69 个计算中 OFF 失败任务。

代表案例：task 53 在 **64.944436730 s** 开始计算。P 检查 local=31 / remote=0 后 deadline 拒绝；FA 在相同事件检查 **40 对**，前 39 对 deadline 拒绝，最后 local=31 / remote=43（source=43）启动，配置 δ=0.017、n=3。两组同在 66 s 遭故障，P 失败而 FA 完成。P reference 的全 INPUT 序列化约 2.78 s，已超过约 1.564 s 的恢复额外预算。该例支持检查替代候选与 LocalDelivery，但不能借用 FA 的资源快照宣称 P 的反事实已证实。

## 4. Selective：只报告可证明的条件界

不生成第二套概率轨迹。仅对初始 TASK_RUNNING、progress=0、计算起点一致的记录，使用现有 P_F、first sample、整数计算时长和已记录 reference 带宽，对原 SER 的 `Σ w_k min(T_ser, lead_k)` 做上下界；严格大于比较不变。`1e-12` 只沿用生产代码已有概率质量表示校验范围，不是新增 SEND 阈值。

TASK_RUNNING 的 QueryTaskPrediction 为 finish-exclusive；历史 FAULT_EPOCH 是批次前 inclusive 输入，不能冒充批次后的 Selective snapshot，保留 UNKNOWN。1 s 采样周期来自执行版本编译时的 `fault/fault-para.cc`，不是从不存在的 CLI 参数猜测。

| 条件口径 | SEND 可判定 | DEFER 可判定 | selector 未知 |
| --- | ---: | ---: | ---: |
| 734 个任务的首次硬拒绝 | 17 | 655 | 62 |
| 其中原有 69 个 OFF 故障失败任务 | 14 | 27 | 28 |

这些只是 **该时刻、保持该 reference 不变** 的结果，不是实际执行次数，不能推广到换 remote 后或后续决策。比如 task 574 在 P 初始 reference 下条件为 DEFER，但 FA 后来在 178 s 使用 source=remote 启动并最终完成。

按照完整 counterfactual 分类，69 个任务中只有 27 个可条件归入 `SELECTIVE_WOULD_DEFER`，其余 **42 个仍为 UNKNOWN**。其中 14 个虽能判 SEND，仍无法证明 flow 准入、checkpoint 建立和 deadline 内恢复。

- `N_provable_selective_false_rejection=0`：尚未证明任何一个，不能据此认为无需修复。
- `N_selective_in_flight`、`N_selective_still_infeasible`：UNKNOWN，JSON 使用 null，不填 0。
- 69 个任务中，30 个在初次计算开始即向该非本地 reference 发送，即使只按满 1 Gbps 序列化下界也来不及在已观察故障前完整到达。这只能证明该条件下 **NOT_READY**，不能证明真实有流 IN_FLIGHT，也不能证明无部分复用收益。实际故障时刻仅用于事后标签，不输入 SER 决策。

实际故障状态：69 个原任务没有真实保护初始化、checkpoint 或 proactive INPUT。真实状态与“假设早启动”的状态分列。恢复必须遵循现有 INPUT、remote state、local tail 的依赖/并发关系；本轮没有将三项简单串行相加，也没有假造 receiver completion 或可救任务数。

## 5. 产物与验证

[离线产物目录](../../../output/compfrr/1g-candidate-feasibility-audit/)：

- `candidate-audit.csv`：7,187 行，区分 P reference、未知候选数量聚合、空集合与仅在 FA 可证实的实际 pair。
- `reference-vs-alternatives.csv`、`selective-evidence.csv`：各 3,444 个硬拒绝事件，保留原 CSV 行号、触发来源及条件范围。
- `fault-state-evidence.csv`：80 个实际 RUNNING 主任务故障，加 1 个初始 INPUT 阶段 F3。
- `classification-summary.json`、`representative-cases.md`：任务/事件双口径、缺失证据、代表任务。

[审计入口](../../../contrib/satcompute/tests/integration/regression/audit-1g-candidate-feasibility.py) 复用既有 actual-ledger、执行身份和 pair-retry 审计，拒绝覆盖已有结果。再次运行请指定新的输出目录：

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/audit-1g-candidate-feasibility.py \
  --output output/compfrr/1g-candidate-feasibility-audit-repeat
```

验证：12 项新离线边界测试、9 项既有对比/账本口径测试、6 项既有生产 SER 纯函数测试通过，共 **27 项**。含 1,125 组合成概率轨迹及保留的 causal selector fixture；未启动平台仿真。两组 actual-ledger PASS，83 个原始证据文件的大小/修改时间在读审前后不变。生产及场景文件相对 Run A 未改，未做 C++ 重编译或全套仿真 regression。

## 6. 人工审阅点

可以确认候选覆盖被 single-reference 提前截断，并优先审议这一流程限制；不能确认每个 P 拒绝事件均存在替代可行候选，更不能确认净差 57 全由该项造成。现有数据也不支持直接放松 deadline、改 SER 或给 1 Gbps 单独调参。

如果进入下一阶段，应先明确如何调整原“一次 reference 求解 + 固定 local”的合同、如何补齐同一因果快照内的候选证据，再验证候选覆盖修复；不能未经审阅直接改成逐 remote 重跑 Frequency。Selective ordering 单独评估，不与 coverage 修复混为一项。本轮到此停止。
