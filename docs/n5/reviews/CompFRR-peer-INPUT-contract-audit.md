# CompFRR-P peer INPUT policy-contract symmetry：只读审计

## 结论与范围

只读快照证据状态：`POLICY_CONTRACT_SYMMETRY_PARTIAL_EVIDENCE`。
人工批准后的最小 production 修复及 consistency 结论见第 9 节；原窗口/R 的证据限制不变。

按人工修订，P forecast 只采用已提交的 Selective policy contract；不把 runtime
`Resolve().remainingNs` 写入 `CompFrrForecast::recoveryInputSeconds`。

5/10 Gbps Run A 的全部 **407/409 次原始决策均可证明 selected remote 不变**，
hard-feasible candidate set 也不变。分别有 49/47 条 peer 观测的 INPUT 模型项减小；
窗口与 R 的精确变化数因原 trace 缺少完整 peer trajectories 而不能全部重算，
下文明确区分精确数量、已证明界限与 UNKNOWN。

分支 `fix/compfrr-policy-aware-input-admission`，production HEAD
`05e0aad446bcb690310fa90bc722559ad5ded0eb`。
输入为 `output/compfrr/policy-aware-input-admission/{5Gbps-run11,10Gbps-run11}`：
seed 1 / run 11 / 800 tasks / 1300 s 的 development evidence。
原执行身份仍为 `b69299e6a` + dirty source patch；两份原始 patch 对当前代码
reverse-check 均通过，不改写成 clean execution。

本轮只新增离线工具、测试和本文。production、task120/F3、配置、原始结果均不变。
没有运行 ns-3、rename、CI、提交、推送或合并。

## 冻结的对比合同

| 对象 | P hypothetical INPUT 项 |
|---|---|
| 新 candidate | 保持现有 dry-run：SEND=0，DEFER=legacy `S/B_I` |
| 已保护 peer，同一 actual remote，committed SEND，准入未失败且合同有效 | 0 |
| 已保护 peer，DEFER / 无 SEND / 准入失败 / target 不符 / 已失效 | legacy `S/B_I` |
| 证据缺失或同刻先后关系不明 | UNKNOWN，不默认为 0 |

peer 合同来自 `compfrr-policy-aware-admission.csv` 的
`FINAL_REVALIDATION + start_committed=1`，逐项与 protection START 的 actual remote/time 对账；
不能使用 reference pair 或某个未被选中的 candidate 的 SEND。
再检查严格早于当前决策的 INPUT 生命周期，排除 FAILED、RELEASED、ABSENT 或 target 不符。
跨日志同纳秒出现状态变更而缺少顺序时不猜测先后。

`runtime_prefetch_admission_success` 是 Request 阶段接受记录，不代表 receiver READY。
因此同时检查后续截至当前的生命周期。有效合同下的 READY 和 IN_FLIGHT 都在 P model 中取 0；
最新执行合同进一步收紧为 Resolve 的类别：异星 REQUESTED 仍是 FETCH，保留 legacy 项；
仅已有本地交付事件的 REQUESTED 可以返回 IN_FLIGHT。本次没有 peer
处于 REQUESTED，也没有失败准入/FAILED/失效 SEND 的 peer 观测；这些分支用合成测试验证。

runtime recovery truth 保持独立：READY / IN_FLIGHT / FETCH、当前 receiver remaining、
wrong-target/failed refetch、LocalDelivery 和真实接收依赖均不修改。

本次取值只有 0 或原 serialization term，满足现有
`0 <= recoveryInputSeconds <= legacyInput`。此前“将 native FETCH 含传播时延代入 P”
的探索口径已被本修订替代；先前生成的 `output/compfrr/peer-input-contract-audit/`
不作为本次结论，原始仿真证据未变。

## peer 观测与模型项

计数单位为 `(decision, candidate remote, peer)`，不是不同任务数。

| 项目 | 5 Gbps | 10 Gbps |
|---|---:|---:|
| P-ranking 决策 | 407 | 409 |
| candidate snapshots | 22,773 | 23,105 |
| 含 peer 的 candidate | 363 | 395 |
| peer 观测 | 366 | 395 |
| 有效 committed SEND | 52 | 51 |
| 保持 legacy（本次均为 committed DEFER） | 314 | 344 |
| SEND 中异星：模型 INPUT/catch 确定减小 | 49 | 47 |
| SEND 中同星：原 INPUT 已为 0 | 3 | 4 |
| catch 确定不变 | 317 | 348 |

仅供生命周期核对：5 Gbps 的 READY/IN_FLIGHT/FETCH 为 49/3/314，
10 Gbps 为 49/2/344。这些状态不会将 remaining time 带入 P model。
本次所有有效 SEND 均已出现 PREFETCH_STARTED 或 PREFETCH_LOCAL_STARTED，
且在相应决策之前没有合同失效。

10 Gbps decision 400 / task 391 / remote 30 的 peer 身份存在一个缺口：
记录一个 peer，可由历史确定的候选身份有任务 166、144，未记录谁通过 BuildResources。
两者都是同目标 committed DEFER，因此“一个 legacy peer”的模型结论确定；
工具保留两个可能身份，不擅自挑一个，也不将两者都计数。

## 窗口、R、feasible set 与 selected remote

| 指标 | 5 Gbps | 10 Gbps |
|---|---:|---:|
| 窗口集合改变的 peer 观测：可证明范围 | 45–49 | 44–47 |
| 精确窗口集合改变数 | UNKNOWN | UNKNOWN |
| 单条 occupancy window 改变总数 | UNKNOWN | UNKNOWN |
| candidate R 改变数：可证明范围 | 0–49 | 0–47 |
| 精确 R 改变数 | UNKNOWN | UNKNOWN |
| R 可以证明不变的 candidate | 22,724 | 23,058 |
| hard-feasible candidate set 改变 | **0** | **0** |
| selected remote 改变 | **0/407** | **0/409** |

这些界限没有使用新的概率阈值、模拟风险轨迹或真实未来事件：

1. **窗口下界**：45/44 个受影响 candidate 只有一个 peer，且旧 R>0。
   因而该 peer 至少有一个有效、有正权重的旧窗口。保持其他条件，去掉其正 INPUT 项后，
   旧窗口仍符合 deadline，但结束点提前，窗口集合必变。
   场景中的最小 serialization 差值大于 1 ns，不会被 ceil-to-ns 消掉。
   其余 4/3 个异星 SEND peer 的旧 R=0，可能有未与自身窗口相交的有效窗口，
   也可能没有窗口；不能据 R=0 判定没有窗口。
2. **R 的界限**：只有 49/47 个 candidate 包含 INPUT 项改变的 peer。
   同星 SEND、DEFER 和无 peer 的其余 candidate 的 R 必定不变。
   peer 窗口变短不等于 R 必变；新增 deadline-feasible 窗口也可能使冲突增加。
   原日志没有保留逐时刻 first-failure mass，不能给出精确新 R。
3. **selected remote 不变证明**：对上述 49/47 个 candidate，让未知新 R 在整个合法
   区间 `[0,1]` 内变化，保留 U/M 和 `(bottleneck, propagation, stable node ID)`。
   其最有利排序仍无法击败原 winner；原 winner 本身不属于 INPUT 项改变的候选。
   因此每个旧 snapshot 的 winner 都确定不变，不需要伪造新 R。
4. **hard-feasible set**：源码先检查 candidate 自身的 deadline、依赖和 storage quota，
   再用 peers 计算 R。此次固定自身 forecast、peer 集合、资源和 Frequency 配置，
   仅替换 peer INPUT 项，所以不会改变硬可行集合。peer 内部窗口的 deadline 过滤变化
   不等于 candidate 被硬淘汰。

以上 selected/feasible 结论严格针对既有决策快照。未跑修改后的端到端执行，
不把这些证明写成“新版本 completion/catch/traffic 已验收”。

## 为什么没有精确的 window/R 数字

`CompFrrPlacementTracker::Record()` 在保存记录前清空：

```cpp
trace.referenceInput.risk.futureSteps = {};
c.demand.input.risk.futureSteps = {};
c.peers = {};
```

CSV 仅保留 candidate 汇总分数、`peer_count` 等，缺少 peer 的完整 future steps、
原始窗口、同一决策时点的完整资源/cadence 快照。因此当前数据能恢复合同有效性、
证明上述选择不变，但不能完整重算新的窗口和 R。

工具只使用严格早于决策的事件；同纳秒 primary 完成边界由当时已知的 WU/速率判断
remaining=0，遵循 QueryTaskPrediction 的非正剩余时间过滤。两处同刻
PREFETCH_FAULT_SNAPSHOT 不改变 INPUT 状态，使用其之前的 READY。
没有读取未来 receiver completion、fault 后状态来填补缺失的模型输入。
没有另外实现风险预测器或复跑故障模型。

若需要精确值，后续须经人工决定是否补充只读 snapshot，至少包括：
actual peer IDs、每个 peer 的 committed cadence/forecast/risk future steps/ready cutoff、
依赖有效性，以及 committed Selective actual-pair contract 和截至当前的准入/失效状态。
本轮没有因此新增 instrumentation 或启动仿真。

## 产物和验证

输出：`output/compfrr/peer-policy-contract-symmetry-audit/summary.json`，以及每轮的
`peer-observations.csv`、`candidate-bounds.csv`、`selection-bounds.csv`。
原始 92 个证据文件的大小、mtime 在审计前后完全一致，工具不写原输出。

工作树复用主仓库的 uv 环境；复现时使用一个不存在的新输出目录：

```bash
/home/emsky/project/SCP-SatComPlate/.venv/bin/python \
  contrib/satcompute/tests/support/protection/peer_input_contract_audit.py \
  --source-root output/compfrr/policy-aware-input-admission \
  --output-dir output/compfrr/peer-policy-contract-symmetry-audit-review

/home/emsky/project/SCP-SatComPlate/.venv/bin/python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_peer_input_contract_audit.py' -v
```

18 个纯离线测试 PASS：合同/actual target、准入失败与失效、未来事件隔离、同刻歧义、
REQUESTED/READY/IN_FLIGHT、model/runtime 分离、ranking 上下界和 tie-break。
`git diff --check` PASS。以上是原只读审计的证据范围。

## 9. Production policy-contract consistency：CONSISTENT

人工批准后单独修复 existing peer forecast，先于 canonical rename 提交：

- `Resolve(task, remote).mode` 为 READY / IN_FLIGHT：P 的 INPUT 项为 0。
- FETCH（ABSENT / FAILED / wrong-target / 未建立有效流）：不设置 override，沿用 `S/B_I`。
- 同星 INPUT 仍由 `inputLocal` 保证网络时间为 0。
- 不读取 `remainingNs` 到 P；runtime 的 remaining（含 UNKNOWN）、真实 receiver completion、
  refetch 和 dependency DAG 完全不变。没有改 START、Frequency、P ranking 或 peer 集合筛选。

Focused C++ runtime fixture 在真实 CheckpointManager、InputStagingManager 和网络上覆盖
READY / IN_FLIGHT / REQUESTED / FAILED / ABSENT / wrong-target / LocalDelivery，
对每次 peer 构造比较前后 Resolve 的 mode、flow ID、remaining，以及 candidate/peer INPUT 项。
该 suite 共 12,352 checks 通过。

复算输出：`output/compfrr/n5-closeout-gates/peer-consistency-audit/`。
5/10 Gbps 的 407/409 次 snapshot winner、全部 hard-feasible set 仍不变；92 个原始证据文件
大小及 mtime 不变。窗口/R 精确差值仍为 UNKNOWN，不把 consistency 当作性能复跑。

后续仅允许 canonical rename 与 small semantic-equivalence gate；不重跑完整矩阵。
