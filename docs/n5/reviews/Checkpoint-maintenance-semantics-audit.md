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
本地结果：`output/audits/checkpoint-maintenance-closeout/`，含 pause-events、affected-runs、staleness-at-fault 和 summary；首次审计目录同样保留。

15 组 U 均有 busy 联动 PAUSE，每组 30–43 条决策；其中很多任务没有故障。另只读扫描的 R5/R7 N5C、noR/noU/noM 同样受影响，**不因此授权补跑这些额外 baseline**。

旧结果 KEEP 必须证明维护轨迹与资源账本等价；不能只看 pause 后是否故障。捕获前预留也改变 cL 期间的实际 reserved byte-time，不能混用旧账本。未记录的 pending target、逐路径和 idle 快照记 UNKNOWN，不能由后续故障反推。历史 checkpoint 快照仅标作最后观测事件，不能当成同纳秒精确 decision UID 状态。U 五轮三方比较因此全部重新验证；历史输出保留，不覆盖。

## 验证结果

代码语义、历史影响审计与最小修复完成。目标模块构建、全部维护的 C++ tests 和 smoke suites 通过；
Frequency runtime 11,957 项、路径 2,545 项、恢复 2,745 项检查通过。Python 185 项（184 通过、1 个既有 skip）；
新增离线维护审计在 42 个已有本地 fixture 上通过。未运行 upstream examples/global tests 或 GitHub CI。

测试覆盖真实 peer compute busy 而 maintenance 继续、START 仍要求 idle、local/remote 单侧路径/存储/配额拒绝、
已有 local records 在 local 路径阻塞时继续 remote batch、资源恢复后连续序列、已有 cL/待注册/在途 batch 在策略 PAUSE 中继续、
失败流实际发送记账及不自动重发，以及原有 F3/同纳秒快照/并行恢复测试。

run11–15 × FULL/noU/Rational-U 共 **15 组全量重新执行通过**，无旧输出复用。
每组 66 星、800 tasks、1300s、online generate；除输出路径外与对应旧运行参数一致。
执行代码为 clean commit `c7889de89`；最多 8 组并发，本机合计约 44.66 分钟。后续提交仅整理审计工具与文档。
输出根目录 `output/checkpoint-maintenance-fixed/`，最终状态 `CHECKPOINT_MAINTENANCE_U_PASS`。
既有恢复、资源账本、独立 Frequency/N5C 检查通过；Frequency 共核验 61,949 条，
Rational-U 独立核验 112,397 条候选历史记录，不读取未来故障。
15 组相对各自旧运行的实际故障事件签名均相同；这不是宣称整个风险/概率 trace 逐字段相同。

### 五轮三方结果

每列共 4000 个任务；三方共同的 409 次故障恢复全部具有有效 catch。
`T_catch` 指从故障到真实计算追平故障进度，不是 RESULT 交付时延。GB 为十进制。

| 指标 | FULL | noU | Rational-U |
|---|---:|---:|---:|
| 完成 / 失败 | 4000 / 0 | 4000 / 0 | 4000 / 0 |
| busy-at-fault | 5 | 5 | 9 |
| Direct / Relocate / Recompute | 402 / 7 / 0 | 402 / 7 / 0 | 398 / 11 / 0 |
| catch 均值 / P50 / P95（ms） | 322.003 / 284.699 / 631.434 | 324.356 / 285.176 / 641.928 | 323.636 / 284.947 / 631.434 |
| 实际执行浪费（百万 WU） | 3.094173 | 3.116075 | 3.111865 |
| 常态保护 / 预留空闲成本（百万 eq-WU） | 2.167600 / 10.075751 | 2.167530 / 10.150069 | 2.167140 / 10.124832 |
| 总等效浪费（百万 eq-WU） | 15.337524 | 15.433674 | 15.403837 |
| 额外应用层发送量（GB） | 533.936125 | 534.704936 | 534.580506 |
| assignment / storage HHI（五轮均值） | 0.040438 / 0.051454 | 0.067528 / 0.048642 | 0.040365 / 0.047742 |
| 平均链路利用率（%） | 0.415064 | 0.416321 | 0.417151 |

总等效浪费包括实际执行浪费、常态保护和预留空闲的 equivalent cost，不等于实际 CPU 执行量。
额外应用字节包括保护/恢复的实际发送（含取消前已发送部分），不是全部业务流量或逐跳 IP 字节。

### 修复效果与剩余资源阻塞

旧 FULL/noU/Rational-U 分别完成 3996/3998/3997 个任务，现在均完成 4000。
相对各自旧结果，实际执行浪费分别降低 **53.91% / 43.33% / 52.77%**，总等效浪费降低 **18.88% / 12.98% / 18.10%**；
代价是额外应用字节增加 **1.25% / 0.87% / 1.20%**，常态保护成本增加 **1.19% / 0.94% / 1.24%**。

旧失败没有 catch，不能填零或直接混用不同样本。逐组旧→新共同有效样本分别为 406/407/407：
配对 catch 均值（ms）为 **338.266→321.562 / 341.403→324.046 / 353.434→323.327**。
故障时 local gap 均值（WU）为 **11104.39→7565.22 / 10988.77→7557.23 / 12065.14→7546.94**；
remote gap 均值也降低，但并非所有分位数都改善，不将机制修复解释成优化器保证。

以下为旧失败对应的 FULL 证据；其余组同一任务的记录可在 staleness-at-fault/recovery-summary 中复核。

| run / task | 故障时间（s） | 故障进度 WU | local WU：旧→新 | 新恢复路径 / 结果 |
|---|---:|---:|---:|---|
| 12 / 119 | 653 | 409826 | 60556→399508 | TAIL / 完成 |
| 12 / 573 | 367 | 576815 | 265268→575402 | TAIL / 完成 |
| 13 / 427 | 724 | 523463 | 322656→517823 | MIGRATE_TAIL / 完成 |
| 14 / 573 | 367 | 614414 | 308954→608855 | TAIL / 完成 |

这些案例 remote WU 仍为 0：Deferred 的零进度初始 remote 对象仍合法，local tail 提供真实连续增量；没有凭空增加 remote 状态。
三方汇总 local PATH 阻塞分别为 116/117/117 段，累计 16.095/15.980/15.962 task-s；
remote PATH 为 4/5/6 段，累计 0.650/0.692/0.793 task-s。没有 ON busy 联动阻塞。
正式运行中的配置 resource-hold 均为路径不可准入，未出现真正策略 PAUSE；两者与流水线阻塞分别统计，不能混为故障次数。
存储/配额耗尽、真正 PAUSE、终结失败和序列缺口由单元/集成测试覆盖。

### 长尾审计与停止边界

三方共同样本重算后，FULL 的 catch 和总等效浪费略低；Rational-U 的 storage HHI 更低。
对 FULL vs Rational-U 分别剔除单个最大正/负贡献任务，Rational-U 的总等效浪费仍多 92,952/38,873 eq-WU，
catch 仍慢 2.290/0.965 ms。Rational-U vs noU 的方向会随长尾剔除翻转，不能据此宣称稳定优越性。
完整正负贡献、逐轮和 pooled 证据位于 `paired-comparison.json`、`leave-one-out.json`；
旧新配对与新鲜度位于 `before-after.json`，对象/资源审计位于 `maintenance-impact.json` 和 `audit-results.json`。

**保留 FULL 默认，停止等待用户审阅。** 不继续调参或测试 exponential/其他 U；
历史 R5/R7、noR/noM 等未重跑的额外基线仅保留为历史证据，不能直接混入新机制的公平比较。
本轮仅本地提交，未 push、PR、CI、merge 或 tag，PR #100/#101 保持原状。
