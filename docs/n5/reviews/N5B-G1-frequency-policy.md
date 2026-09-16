# N5B-G1：策略拆分与频率纯求解器

## 状态与范围

**G1 实现和本地验证完成，等待用户审阅；STOPPED AT N5B-G1。**

N5A CLOSEOUT COMPLETE：[PR #95](https://github.com/forest-rabbit/SCP-SatComPlate/pull/95)
已普通合入 n5，集成提交 `b626a154d423219bc1503f060252962e1965b4cf`。
确认包含收尾提交 `2a047c0cd0ac46331f8a856687de8aaa3328623c` 后，已删除 N5A 本地/远端功能分支。
本阶段从该 n5 创建唯一 `feature/n5b-compfrr-frequency`；main 未改动，未运行阶段 CI、未打标签。

采用 N5B 启动任务书及 Probability Timing/Shadow Alignment 补充文件；用户最后确认覆盖
原 G3 冻结回放要求：N5B/N5C 正式算法实验使用在线 **generate**。validation-replay 仅保留
N5A-G4 执行验收，不扩展回放预测接口；原 N5A 输出和验收结论保持不变。

## 实现与接口

- `policy/placement-policy.h`：只读候选和完整 local/remote pair。候选可提供健康、idle/queue、
  可达/一跳、storage free、assignment/recovery counts；不包含未来队列或故障事件。
- `policy/baseline/first-feasible-placement/`：FFP 从 FixedProtectionPolicy 抽出；仍先取最小
  可行一跳 local，再取排除主星/local 的最小可行 remote。存储/负载不参与 FFP 排序。
- `policy/experimental/least-recovery-load/`：诊断 LRL，`assignment + weight*activeRecovery`
  后按 stable ID；权重是显式非负整数，乘加用宽整数防溢出。只做合成单测，未接 CLI/正式场景。
- `policy/compfrr/frequency/compfrr-frequency-policy.h/.cc`：独立生产纯求解器，包含 J_OFF、
  J_START、J_ON、Rbar、初始化/deadline/storage 可行域。无验证目录依赖。
- `frequency-decision-gate.h/.cc`：单任务纯 proposed/committed 状态合同；不创建网络流、
  checkpoint 实体或随机抽样，不替代 N5A 资源账本。

`MakeFrequencyRisk(currentSamplerQ, predictionInput)` 调用唯一生产
`PredictComputeFailureBeforeFinish`，逐值核对其当前联合概率与 sampler 已计算的 q。
F1/F2 仍独立抽样，policy 不生成另一份概率、不访问 future F3/故障文件。
q 用于 ON，包含当前点的 P_finish 用于 OFF→START；继承预测器原整数 horizon/endpoint。

成本通过 `FrequencyInput.costs` 从 `GetProtectionCosts(Kvar)` 传入，不建立第二份档位表；
单测可以显式注入零成本来构造精确同分，这是纯数学锚点，不是生产参数。
候选枚举 delta=10..100 permille、n=1..100、n×delta≤1000；排序精确按 `(J,delta,n)`，
不增加 epsilon。START 严格小于 OFF 且初始化早于剩余计算结束；Rbar≤Rmax 为闭区间。

存储采用必填的每候选 `storageDemand` 额外峰值估计，与当前 local/remote free bytes 比较。
缺少估计器会拒绝输入，不隐式放宽容量；空可行域下 OFF 不启动，ON 保留已提交状态但暂停
新 target 和新 batch。测试使用真实 BackupStoragePool 的 used/reserved/free 验证容量边界。
**G2 才实现当前 N5A pending/H/初始化/merge 实体到额外峰值的适配，G1 未声称已完成实时读池。**

Gate 的 Propose 不改变当前 phase/config；Resolve 只有在同轮无故障且主任务仍计算时提交。
START 提交后是 INITIALIZING，只有外部真实初始化提交才 ON。delta 前向取 TaskStateAdapter
合法边界；新 n 作用于未组批记录，不触及已有 batch。PAUSE 保留 pending/committed 实体的
所有权在 N5A，Gate 不删除它们；RECOVERING/DONE 停止策略，不允许 ON→OFF。

## Deliberate differences from G4 shadow

1. q 是本轮 actual sample 的联合概率，不是 next-1s QueryComputeRisk。
2. P_finish 使用现有 current-step-inclusive 生产预测器，不复制未来检查点循环。
3. G2 决策对齐 fault-check grid；任务在两次检查之间开始时等下一检查，不另建任务相对定时器。
4. 生产可行域将使用 FFP 实际节点、恢复速率、路径和真实 headroom；纯对照显式使用统一
   10 Gbps、相同主/恢复速率及不约束存储的测试输入，不能把该测试假设带入生产。
5. 同数学输入要求相同评分和最优候选；真实 START 数、配置轨迹和恢复结果不要求等于旧 shadow。
6. tie-break 恢复并保持 shadow 的 `(J,delta,n)` 升序。
7. no-feasible ON 暂停新批次，也暂停新 target；已创建操作继续，未组批记录保留。
8. 正式实验使用 generate；策略改变恢复负载/温度而改变 F1 故障属于闭环结果，不强求不同策略
   在同 seed/run 下故障文件完全一致。不向 policy 暴露抽样结果或未来 F3。

## 验证证据

| 检查 | 最终结果 |
|---|---|
| FFP 对原规则 | 4个候选的65,536组健康/空闲/可达/一跳组合完全一致，包括主星排除、输入乱序 |
| 独立频率求解 | 160组同输入参数组合与旧 shadow 对齐；另有5个重点锚点 |
| N5B 专项 | 143,378项检查通过，含 LRL 合成均衡、容量/截止边界、概率/同轮提交/合法边界 |
| 既有 C++ 加新程序 | 20个程序全部通过；原保护合同25,430项、路径2,018项、恢复848项 |
| Python unit | 64项通过，无跳过；包含生产/验证代码隔离及因果概率 API 约束 |
| smoke | 9组全部通过，真实 fixed 流、恢复、同星交付、generate/验收回放一致性保持 |
| maintained regression | routing、workload、fault lifecycle、N4B joint 全部通过 |

N4B 既有联合回归仍为66星/1000s/100任务，88完成、12失败，483条实际模型/预测概率一致。
这是维护回归，不是 N5B 频率上线后的性能结果。本次未运行800任务/1300s正式场景、配对种子
实验或GitHub阶段CI。没有修改故障模型、抽样、task/recovery runtime 或 checkpoint mechanism。

五个 oracle 锚点（J 为候选项，START 完整分数再加 Cinit）：

| 锚点 | J | Rbar/s | 可行候选 | delta permille | n |
|---|---:|---:|---:|---:|---:|
| OFF/START | 0.1024 | 0.0887142857143 | 2315 | 10 | 7 |
| ON high q | 0.0228888888889 | 0.0866666666667 | 2315 | 10 | 6 |
| ON q=0 | 0.000466666666667 | 0.9318 | 2315 | 100 | 10 |
| 精确同分（合成零成本） | 0 | 0.075 | 2315 | 10 | 1 |
| Rmax 边界 | 0.0316666666667 | 0.075 | 1 | 10 | 1 |

START 手算：J_OFF=7.84s、J_START=0.1049s、T_init=0.802s。
数值验证允许1e-12相对/绝对误差；该容差仅用于断言，不参与候选排序或可行性决策。

中途一次 CLI fixed smoke 因旧 executable 配合新库出现段错误：`satcompute` CMake target
只构建模块库。显式重编译 `satcompute_app_satcompute` 后，重新运行 Python/smoke/regression
均通过；未为此改动业务机制。正式验收只使用这一轮成功结果。

复现（仓库根目录，项目 uv 环境，已有 satcompute configure）：

```bash
.venv/bin/cmake --build cmake-cache -j 4 --target satcompute_app_satcompute satcompute_test_satcompute-n5b-policy-test
./ns3 run --no-build satcompute-n5b-policy-test
```

完整测试入口见 [tests README](../../../contrib/satcompute/tests/README.md)。本地日志：
`/tmp/scp-n5b-g1-cpp.log`、`/tmp/scp-n5b-g1-final-python.log`、
`/tmp/scp-n5b-g1-final-smoke.log`、`/tmp/scp-n5b-g1-final-regression.log`。
测试临时输出由既有 runner 清理；未覆盖 N5A-G4 原始输出，不新增正常运行 CSV。

## 下一门禁（尚未授权执行）

G1 的故障前决策、同轮 hit 不提交、delta/n 前向性是**纯合同测试**。G2 才把这些接口
接到在线 generate 的实际 epoch、N5A checkpoint/批次和真实资源上下文，并输出逐次
proposed/effective/committed 审计。该真实事件接线目前未实现、未标为通过。
G3 改为 FFP+fixed 与 FFP+CompFRR 的在线 generate 配对对比，不启用 LRL/N5C。

**STOPPED AT N5B-G1：等待用户审阅，不进入 G2，不合并本阶段 PR、不运行 N5 阶段 CI。**
