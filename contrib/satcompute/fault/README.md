# 故障模块

`fault/` 负责统一故障参数、Fault Trace、在线模型、因果概率预测以及运行期执行。
`FaultModelEngine` 只读取实时状态并判定风险/故障；所有 compute/整星状态变化仍
统一交给 N4A `FaultController`，模型和预测器都不会直接修改任务、transfer、链路
或路由。

## 文件与职责

```text
fault/
├── fault-para.h/.cc             人工维护的 common/F1/F2/F3 参数
├── parameter/                   参数合法性校验
├── model/                       F1/F2 状态模型、概率组合/预测与 F3 调度模型
├── runtime/                     在线判定、因果预测、状态覆盖与故障执行
├── trace/                       统一记录定义、JSON 读取和写出
└── README.md
```

| 文件 | 职责 |
|---|---|
| `fault-para.h/.cc` | 按 common、F1、F2、F3 分组的唯一内置故障参数 |
| `parameter/fault-parameter-validator.h/.cc` | 有限值、范围及跨字段关系的启动前校验 |
| `model/compute-fault-combination.h/.cc` | 计算 `q_comp`，并将独立 F1/F2 抽样折叠为一次平台结果 |
| `model/compute-failure-predictor.h/.cc` | 复制当前 F1/F2 状态，沿任务剩余窗口滚动模型并计算完成前联合故障概率的纯函数 |
| `model/f1-self-state-fault-model.h/.cc` | 无运行期副作用的 F1 温度、DoD、风险、强度和单步概率 |
| `model/f2-radiation-fault-model.h/.cc` | 原生 ECEF 转经纬度、区域判定、连续暴露、累计风险与单步概率 |
| `model/f3-debris-fault-model.h/.cc` | 以独立 ns-3 随机流生成 fixed-K 或 Poisson 永久整星事件 |
| `runtime/fault-model-engine.h/.cc` | 在线读取状态、维护风险 episode、使用 ns-3 随机流判定事件 |
| `runtime/fault-prediction-engine.h/.cc` | 在 generate/replay 中维护无随机数影子状态，并从已执行 NOTICE 和当前任务快照生成因果预测记录 |
| `runtime/fault-state.h/.cc` | 每颗卫星的 satellite/communication/compute 可用性与活动故障集合 |
| `runtime/fault-controller.h/.cc` | replay/在线事件批处理，以及任务、传输和有效拓扑联动 |
| `trace/fault-definition.h/.cc` | compute/satellite 记录以及预警、恢复、风险结束和排序时间 |
| `trace/fault-trace.h/.cc` | v1 兼容读取、v2 closed-world 校验、canonical writer |

## 三种运行模式

```text
none
  不创建 FaultController 或模型 tick，也不产生故障专用输出

generate
  当前 ComputeService 状态 -> F1 模型 ┐
                                       ├-> 各来源独立抽样 -> 同刻命中合并为一次 START
  当前原生 ECEF 坐标       -> F2 模型 ┘
  完整存活卫星集合          -> F3 模型 -> 永久 satellite START
  -> FaultController 精确执行 -> 写出 Fault Trace v2

replay
  只读取已经确定的 v1/v2 Fault Trace
  -> 不按模型重新抽样故障 -> 精确重放
  若启用 F1/F2 预测：当前任务/原生轨道 -> 无随机数影子模型 -> 预测指标
```

`generate` 中确实会发生故障，并同时写出本轮实际执行的 trace。随后使用相同星座、
任务和该 trace 进入 `replay`，应得到相同的 NOTICE、NOTICE_CLEAR、START、RECOVERY、
任务终态、transfer 终态和路由变化。失败的旧任务不会在恢复时复活；恢复只允许后来
到达的任务继续使用节点。generate 可启用 F1-only、F2-only 或 F1+F2；联合来源按
独立竞争风险处理：F1/F2 分别抽样，平台只执行二者结果的逻辑或。F3 可单独运行，
也可与两个计算来源共同运行。

当任务输入存在且 F1/F2 至少启用一个时，generate 和 replay 都启用同一个因果
预测器。replay 不重新决定故障，但预测器仍需按相同参数重建 F1/F2 影子状态；因此
重放 F2-only 或 F1+F2 trace 时，必须传入与 generate 相同的 `faultEnableF1/F2`。
预测器不消费随机数，也不改变真实模型、任务或故障状态。none 和纯 F3 运行不创建
预测器，也不生成预测文件。

## F1 自身状态计算故障

每个配置检查点读取上一时间段的 `ComputeService` 忙闲状态。忙碌时温度趋近
`T_sat`，空闲或算力不可用时趋近 `T_base`：

```text
T_next = T_sat  - (T_sat  - T) * exp(-dt / tau_h)       # busy
T_next = T_base + (T      - T_base) * exp(-dt / tau_c)  # idle
```

计算结束不会清零温度。DoD 仅按计算增量功率、秒和 Wh 增长；能源项只作为小权重
修正。温度风险在 `T_risk..T_crit` 内使用指数曲线，最终综合风险为：

```text
R_F1 = 1 - (1 - R_T) * (1 - alpha_E * pressure_E)
lambda_F1 = lambda_F1_max * R_F1
q_F1 = 1 - exp(-lambda_F1 * dt)
```

达到 `T_crit` 时保留确定性保护停机，即当前步 `q_F1=1`。其他时间每个可计算节点
每个可计算 tick 只从按稳定卫星 ID 分配的 ns-3 stream 取一个随机数；来源开关不会
改变节点间的随机流分配。

当前唯一参数入口是 [`fault-para.cc`](fault-para.cc)，修改后需要重新编译。公共参数
使用秒，进入运行期后才严格换算为整数 ns；当前检查周期为 1 秒，可恢复计算故障
持续 8 秒。`coolingTauSeconds=40` 是降温曲线的时间常数，8 秒则是保护停机时长，
二者含义不同。F1 参数的校准证据见
[`docs/calibration/n4b-f1`](../../../docs/calibration/n4b-f1/README.md)。这些数值面向
1000 秒加速实验，不表示现实卫星热常数或故障率。

## F2 连续辐射暴露故障

F2 直接读取正式平台共享的 `OnlineOrbitConstellation` ECEF 坐标，再调用 ns-3.48
`GeographicPositions` 转为经纬度。当前闭区间为：

```text
-90 deg <= longitude <= 5 deg
-50 deg <= latitude  <= 5 deg
```

进入区域时连续暴露 `tau` 从 0 开始，留在区域内按 1 秒检查周期累加，离开区域则
关闭风险 episode 并清零；下一次进入是一个新 episode。累计风险只用于预警：

```text
R_F2(tau) = 1 - exp(-lambda_F2 * tau)
q_F2(dt)  = 1 - exp(-lambda_F2 * dt)
NOTICE when R_F2 >= theta_F2
```

实际故障每步只按条件概率 `q_F2` 抽样，绝不会把不断增大的累计风险 `R_F2` 当成本步
概率重复抽样。因此故障可能发生在 NOTICE 之前，也可能先预警后故障，或者只形成
风险-only 记录。8 秒算力停机期间轨道和暴露仍继续演化，但暂停新的故障抽样；恢复
只接纳后续任务。

F1 与 F2 同时启用时，两者使用互不共享状态的 ns-3 随机流分别抽样：

```text
X_F1 ~ Bernoulli(q_F1)
X_F2 ~ Bernoulli(q_F2)
compute START = X_F1 OR X_F2
q_comp = 1 - (1 - q_F1) * (1 - q_F2)
```

`q_comp` 是 trace、观测和后续预测使用的联合概率，不替代两个来源的真实抽样。同一
节点同一检查时刻即使两个来源同时命中，也只提交一次可恢复 compute 故障。

当前 66 星、1000 秒功能窗口冻结
`lambda_F2=0.00015569048731122528 s^-1`、`theta_F2=0.06925814255738115`。
orbit-only 工具先用 66 星冻结参数，再以相同参数验证 351/720 星的规模效应；它不
创建网络、路由、任务或故障执行。正式 F2-only 验证使用 `--orbitStartOffset=5695`
对齐选定窗口，并通过真实平台 Monte Carlo 检查事件数。完整证据见
[`docs/calibration/n4b-f2`](../../../docs/calibration/n4b-f2/README.md)。这些数值是
有限窗口内的系统级有效计算故障强度，不是原始 SEU 计数或现实卫星绝对失效率。

F2 与 F3 参数都按独立分组保留在 [`fault-para.cc`](fault-para.cc) 中，默认关闭。
`f3.fixedCount` 是 `fixed_k` 压力测试中人工指定的永久撞击卫星数量，不属于任务
输入，也不代表现实碰撞频率。

## F3 致命碎片整星故障

F3 不依赖任务，只从完整的稳定卫星 ID 集合选择节点，并为每次事件写出：

```text
fault_type = satellite
fault_occurred = true
notice_time_ns = null
failure_probability = null
duration_ns = null
```

`fixed_k` 在 `[0,T)` 独立均匀采样 K 个时刻并排序，再从卫星集合无放回选择 K 个
节点；默认功能场景使用 `K=1`。`poisson` 在每个事件后按当前存活卫星数更新星座级
强度：

```text
Lambda_F3 = N_alive * lambda_F3
Delta t ~ Exponential(Lambda_F3)
```

每次到达再从存活集合均匀选择一个节点并移除，因此两种模式都不会重复选择已永久
失效的卫星。事件时间流与节点选择流彼此分离，也不改变 F1/F2 的随机序列。

同节点同刻同时命中 F3 与 compute 故障时，只生成 F3。若 F3 到来时该节点正处于
8 秒 compute 停机区间，平台会把 compute 恢复提前到 F3 时刻，再立即执行永久整星
START；生成 trace 中两个区间首尾相接而不重叠，generate/replay 的事件顺序均为
`RECOVERY -> START`。永久失效后不再更新该节点的 F1/F2 状态或消耗其抽样随机数。

## 事件标识与语义

平台没有名为 `COMPUTE_START` 的独立事件类型。文档中的 **compute START** 是
`fault_type=compute` 与 `event_type=START` 的组合，用于区别整星故障的
**satellite START**。各标识的含义为：

| 标识 | 产生条件 | 是否改变节点状态 |
|---|---|---|
| `NOTICE` | F1 或 F2 的风险首次达到或超过各自阈值，使联合风险从无效变为有效 | 否，仅开始记录风险 episode |
| `NOTICE_CLEAR` | F1、F2 均回到各自阈值以下，或 F3 即将永久关闭卫星，且该 episode 未发生 compute 故障 | 否，仅结束风险-only episode |
| compute `START` | F1、F2 独立抽样中至少一个真实命中 | 是，关闭目标节点的计算能力 |
| compute `RECOVERY` | 可恢复 compute 故障到达结束时刻 | 是，重新允许后续任务使用计算能力 |
| satellite `START` | F3 命中目标卫星 | 是，永久关闭整星、通信和计算能力 |

因此，可以把 `NOTICE` 理解为“达到或超过任一 F1/F2 风险阈值”，把 compute `START` 理解为
“实际触发计算故障”，但两者不是必然的先后关系。抽样命中可能早于阈值，因而允许
没有 `NOTICE` 的 compute `START`；同一检查时刻既越过阈值又命中时，预警提前量为
0。compute `START` 也不是主动备份开关；后续备份策略只会把风险与预测概率作为
决策输入，再单独产生 `BACKUP_START`、`BACKUP_READY` 和 `TAKEOVER` 等事件。

## 风险 episode

F1、F2 各自判断风险阈值，再合并为节点级风险 episode：

```text
risk_active = (R_F1 >= theta_F1) OR (R_F2 >= theta_F2)
```

联合风险第一次由无效变为有效时产生一次 NOTICE，并把当时的单步联合概率
`q_comp` 作为 trace 元数据保存。该值不冻结后续预测。若 F1 已经令 episode 有效，
F2 随后越过阈值不会产生第二次 NOTICE；只有
两个来源都回到阈值以下后，下一次达到阈值才会开始新的 episode：

```text
无风险 -> 风险有效       NOTICE
风险有效 -> 风险退出     NOTICE_CLEAR + fault_occurred=false
风险有效 -> 实际故障     START + warning_lead_time
无风险 -> 实际故障       START，notice/lead time 为 null
```

风险-only 记录只进入 trace 和事件证据，不改变节点可用性。仿真结束时仍未退出的风险
episode 以仿真终点关闭。F1-only、F2-only 与 F1+F2 共用同一种 trace 记录，trace
不暴露风险来源；联合模式的 `failure_probability` 保存 `q_comp`，而真实发生仍来自
F1/F2 各自的独立随机判定。有预警故障记录
`warning_lead_time_ns=start_time_ns-notice_time_ns`；未发生故障的风险-only 记录
`risk_duration_ns=clear_time_ns-notice_time_ns`，二者不会同时出现。

## 任务完成前的因果故障概率预测

预测器在每个 F1/F2 检查点为所有正在计算的任务准备内部预测，但只有以下两个条件
同时满足时才输出正式记录：目标节点已有一个尚未结束的 compute 风险 episode，且
该节点当前正在计算任务。NOTICE 是输出门控和风险标识，不是预测模型的起点，也不
改变 F1/F2 的故障抽样。因此 NOTICE 前仍允许真实故障；这种无预警 START 没有正式
预测记录，应作为“未通知故障率”单独评价。

generate 与 replay 分别维护一份独立、无随机数的 F1/F2 影子状态。每个检查点先用
当前忙闲状态和原生 ECEF 坐标推进影子状态，再复制快照并在任务剩余窗口内滚动：

```text
q_F1,k = F1(state_F1,k, busy=true | conditional survival)
q_F2,k = F2(state_F2,k, native ECEF(t_k))
q_comp,k = 1 - (1 - q_F1,k) * (1 - q_F2,k)

K = floor(remaining_compute_time / check_interval) + 1
P_fail_before_finish = 1 - product(k=0..K-1, 1 - q_comp,k)
```

`k=0` 是当前检查点；后续点不超过当前任务的预计完成时刻。F1 的未来分支是在“任务
此前没有故障且继续忙碌”的条件下推进，F2 则直接调用 ns-3.48 原生 mobility 的
只读按时刻坐标接口，不复制第二套轨道公式。达到 F1 临界温度时相应步概率为 1，
累计概率也为 1。F3 不属于该 compute 联合概率。

纯预测函数只修改局部状态副本，不消费 F1/F2 随机流，也不知道真实 START。运行期
先准备当前时刻的模型轨迹，待同一时刻的控制器事件执行后再决定是否输出，因此：

```text
NOTICE 时刻且有运行任务       输出 risk_elapsed_time_ns = 0
活动 NOTICE 的后续检查点       持续输出滚动概率
同刻 NOTICE + compute START    输出一次，再由 START 关闭 episode
无 NOTICE 的 compute START     不输出正式预测
NOTICE_CLEAR / satellite START 关闭 episode，不输出该时刻记录
```

预测记录中的 `risk_elapsed_time_ns=now-notice_time_ns` 是当前时刻已经观察到的风险
持续时间；它不是 trace 在 episode 结束后才能确定的 `risk_duration_ns`。运行期还
明确禁止读取未来的 compute START、`fault_occurred`、`warning_lead_time_ns` 或最终
风险时长。generate 和 replay 因此使用同一条因果路径；使用相同任务和已生成 trace
时，预测输出应逐字节一致。

`observed_compute_failure_before_finish` 只在仿真结束后由 metrics 根据实际 START
补充，用于 Brier score 和后续 Monte Carlo 校准，不会反馈给预测器。若预计任务完成
前被 F3/其他竞争事件终止，或预计完成时刻达到/超出仿真终点且窗口内没有故障，该
标签留空并按右删失处理，不进入评分。模型内部一致性由逐步概率单元测试验证，随机
实现的校准需要多 seed/run，模型对现实故障
的有效性仍需外部数据；三者不能混为一种“准确率”。当前输出只是后续主动备份的
候选输入；本阶段不启动副本、不选择备份节点，也不产生
`BACKUP_START`、`BACKUP_READY` 或 `TAKEOVER`。F1/F2 单步概率随状态变化的预测扩展
已经完成，阈值/收益最优点仍必须以后续多次运行的校准证据为准，不能从单次轨迹
推断。

## 故障执行

同一纳秒的事件固定按下列顺序、再按 `fault_id` 升序执行：

```text
NOTICE -> NOTICE_CLEAR -> RECOVERY -> START
```

预测运行期采用同一纳秒内的两阶段顺序：先更新无随机数影子状态并准备轨迹，再让
模型/控制器执行 NOTICE、NOTICE_CLEAR、RECOVERY、START，最后只依据已经执行的事件
决定是否正式写出。这样首条记录可以位于 NOTICE 当刻，已通知的 START 当刻仍保留
最后一条因果预测，同时不会提前读取 START 或把无预警故障伪装为命中预测。

compute START 只令目标节点 `compute_available=false`，不会关闭 ISL 或重算路由。
目标节点上尚未越过计算阶段的任务按 N4A 合同失败；有限恢复只接纳新任务。

satellite START 同时关闭整星、通信和计算，在精确时刻更新有效 ISL 并在边集合变化时
立即重算 IPv4。恢复会读取当时的实时轨道位置，只恢复仍满足距离门限的固定候选；
永久故障则没有 RECOVERY。

完整 JSON 合同见 [`input/fault/README.md`](../input/fault/README.md)，运行指标见
[`metrics/README.md`](../metrics/README.md)，可执行闭环见
[`66 星 F1 示例`](../input/examples/leo-66-120s-f1/README.md)与
[`66 星 F2 示例`](../input/examples/leo-66-1000s-f2/README.md)；不需要任务输入的
F3 fixed-K 流程见
[`66 星 F3 示例`](../input/examples/leo-66-1000s-f3/README.md)。
