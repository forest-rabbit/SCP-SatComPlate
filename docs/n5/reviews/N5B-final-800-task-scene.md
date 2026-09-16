# N5B：最终 800 任务场景

## 调整边界

在 `feature/n5b-compfrr-frequency` / Draft PR #96 内继续，不修改保护策略、故障模型、
seed=1/run=11、66 星、10 Gbps/1 ms、F3 node62/1027.055770726 s。
移除前置任务 801；仅增大 compression 任务 120，其余 799 任务逐字段不变。
不要求预热，F1/F2 正常抽样；筛选要求 node62 在 F3 前没有 F1/F2，其他卫星不受此限制。

| 任务 120 | 原值 | 当前冻结值 |
|---|---:|---:|
| INPUT（B） | 207142024 | 800000000 |
| WU | 310714 | 1200000 |
| RESULT（B） | 112370676 | 433985046 |
| 参考纯计算时间（s） | 3.10714 | 12 |

到达仍为 1024.682825747 s，source54/compute62/result33 不变。
当前总 INPUT=194119753287 B、RESULT=100168131855 B、WU=352513119。
240 dense / 240 sparse / 240 compression / 80 LLM；704 普通 TN 图像、15 固定大小锚点、
1 显式大小覆盖。生成器摘要明确区分覆盖与随机样本。

## B 组单任务初筛

入口：`contrib/satcompute/tests/integration/regression/run-f3-protection-check.py`。
实际运行同一平台、原生 66 星和完整故障模型，只把 workload 缩为任务 120；不读未来 F3 做决策。
原始输出保留于 `output/n5b-final800-selection/{coarse,fine}/`，不覆盖历史实验。

| INPUT（十进制 MB） | 有收益的 START | F3 时已 ON / 使用检查点 | 最终完成 |
|---|---|---|---|
| 207.142024、300、400、500 | 否 | 否 | 否 |
| 600 | 否 | 否 | 是，纯重计算 |
| 700、725、750、775 | 是 | 否 | 是，纯重计算 |
| 800 | 是 | 是 | 是，REMOTE_REDO |

全部候选在 node62 的 F3 前均无 F1/F2。800 MB 是本次已测试集合中最小的有效候选，
不是连续大小空间的全局最小值；原计划的 900/1000 MB 因首次通过即停止而没有运行。
不能把较宽 deadline 下的纯重计算完成、或 INITIALIZING 中被中断，记为保护成功。

800 MB 初筛：compute start=1025.327279419 s；START=1026 s，
`P_fail_before_finish=0.0185178196`、`q_current_sample=0`；
`Jstart=0.0217355404 < Joff=0.0243087121`。
ON=1026.651709794 s，早于 F3 约 0.404061 s；F3 后使用远端 66847 WU 检查点，
真实恢复计算 1133153 WU，非从零重计算；1038.737753981 s 完成交付。
瞬时概率为零不等于未来整个剩余计算区间的概率为零，生产预测只使用 F1/F2。

## 完整验收

**PASS，800 MB 已作为最终场景值固定。** 从干净执行提交
`50bd034a2578012fbc6587a6ca51ece29640217f` 完整运行一次 `generate + compfrr + ffp`
至 1300 s，returncode=0；audit/shadow 关闭。约耗时 1117.200 s（18 分 37 秒）。
输出：`output/n5b-final800/B-ffp-compfrr-frequency/`；专门验收：`output/n5b-final800/f3-check.json`。

| 完整 B 组指标 | 结果 |
|---|---:|
| 任务完成 / 失败 | 800 / 0 |
| 按期完成 / deadline 失败 | 800 / 0 |
| F1 / F2 / F3 实际故障 | 84 / 2 / 1 |
| 直接受害任务；接受恢复 / 恢复成功 | 83；83 / 83 |
| 故障时 OFF / INITIALIZING / ON | 0 / 5 / 78 |
| TAIL / REMOTE_REDO / MIGRATE_TAIL / MIGRATE_REDO / RECOMPUTE | 68 / 6 / 3 / 1 / 5 |
| W_waste_actual（等效 WU） | 2344498.5932 |
| protection 口径实际发送字节 | 226079890635 |

任务 120 在完整负载下仍复现上面的 START、ON 和完成时刻，local51/remote0，
F3 时已完成 172849 WU（14.4041%），远端保存 66847 WU；故障后仅计算 1133153 WU。
`REMOTE_REDO` 不是从零重算：其中 catchup 为 106002 WU，之后继续 1027151 WU，
1038.387300727 s 完成计算，1038.737753981 s 完成交付，deadline=1040.927279419 s。
node62 在 F3 前没有 F1/F2。F3 快照 pF1=pF2=q_comp=0，剩余区间预测概率仍为 1.851782%：
这是 F1/F2 的预测，不是预测到 F3。增大任务同时扩大了 deadline 预算，
所以不能仅凭最终完成就声称是保护避免了 deadline 失败；这里额外验证了真实保留并复用的进度。

所有 CSV 行完整、任务唯一终态、真实 WU/cR/迁移字节账本检查通过；
存储、placement 和网络预留归零，FlowMonitor reported drop/lost 均为 0。
容量等待/释放重试仍为 0，不将本次结果归因于 R6。
本地 targeted build、75 Python 单测（无 skip）、21 C++ 测试程序、10 组 smoke 和全部维护 regression 通过；
原 N4B 回归仍为 88 完成/12 失败、11 START、483 条概率一致。
测试日志：`output/n5b-final800-validation/`。未重跑 A/C、none、all-recompute、多 seed 或阶段 CI。
保持 Draft PR #96，未 Ready/merge/删分支，未进入 N5C。

这是按 B 组保护成功条件选择的受控 F3 实验，不是无偏随机性能样本。
它可作为后续统一固定输入，但不能据此证明 B 对任意负载普遍更优；后续算法比较需
使用同一新输入。原始 800 任务及中间 801 任务结果均仅保留为历史证据。
