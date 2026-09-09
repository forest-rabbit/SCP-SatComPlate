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
├── runtime/                     概率记录、在线判定、因果预测、状态覆盖与故障执行
├── trace/                       统一记录定义、JSON 写出
└── README.md
```

| 文件 | 职责 |
|---|---|
| `fault-para.h/.cc` | 按 common、F1、F2、F3 分组的唯一内置故障参数 |
| `parameter/fault-parameter-validator.h/.cc` | 有限值、范围及跨字段关系的启动前校验 |
| `model/compute-fault-combination.h/.cc` | 计算 `q_comp`，并将独立 F1/F2 抽样折叠为一次平台结果 |
| `model/compute-failure-predictor.h/.cc` | 复制当前 F1/F2 状态，沿任务剩余窗口滚动模型并计算完成前联合故障概率的纯函数 |
| `model/f1-self-state-fault-model.h/.cc` | 无运行期副作用的 F1 温度、DoD、风险、强度和单步概率 |
| `model/f2-radiation-fault-model.h/.cc` | 原生 ECEF 转经纬度、SAA 空间风险、SEU 映射、穿越统计与单步概率 |
| `model/f3-debris-fault-model.h/.cc` | 以独立 ns-3 随机流生成 fixed-K 或 Poisson 永久整星事件 |
| `runtime/compute-failure-probability-record.h` | 在线真值与独立审计预测共用的逐时刻概率字段合同 |
| `runtime/fault-model-engine.h/.cc` | 在线推进状态、使用独立 ns-3 随机流判定事件 |
| `runtime/fault-prediction-engine.h/.cc` | 按需在 generate 中维护独立的无随机数审计状态，为所有可计算节点的 RUNNING 任务生成因果预测记录 |
| `runtime/fault-state.h/.cc` | 每颗卫星的 satellite/communication/compute 可用性与活动故障集合 |
| `runtime/fault-controller.h/.cc` | 在线事件批处理，以及任务、传输和有效拓扑联动 |
| `trace/fault-definition.h/.cc` | compute/satellite START、来源概率、温度和恢复时间 |
| `trace/fault-trace.h/.cc` | v2 输出校验与 canonical writer；不提供生产 reader |

## 两种运行模式

```text
none
  不创建 FaultController 或模型 tick，也不产生故障专用输出

generate
  当前 ComputeService 状态 -> F1 模型 ┐
                                       ├-> 各来源独立抽样 -> 同刻命中合并为一次 START
  当前原生 ECEF 坐标       -> F2 模型 ┘
  完整存活卫星集合          -> F3 模型 -> 永久 satellite START
  -> FaultController 精确执行 -> 写出 Fault Trace v2

```

`generate` 中确实会发生故障，并写出本轮实际执行的 trace。相同代码、星座、任务、
参数及 seed/run/stream 的重复 generate 应得到相同事件与业务终态。trace 只作为输出。
失败的旧任务不会在恢复时复活；恢复只允许后来
到达或仍在排队的任务继续使用节点。generate 可启用 F1-only、F2-only 或 F1+F2；联合来源按
独立竞争风险处理：F1/F2 分别抽样，平台只执行二者结果的逻辑或。F3 可单独运行，
也可与两个计算来源共同运行。

正式模式不再接受 replay，旧 reader、文件调度入口和文件回放 fixture 已移除。
测试允许通过 `tests/support/fault-injection.h` 直接安排 ns 事件，覆盖共用执行器的
故障/恢复和同刻边界；这不是生产运行模式，也不读取故障文件。

只有 `faultProbabilityAudit=1` 且存在 F1/F2 任务时才创建独立因果审计器。
保留它用于 generate 模型与独立状态演化的一致性回归，不用于在线查询。
审计默认关闭；只读风险查询不受它影响。

## 在线节点风险查询

`FaultModelEngine::QueryComputeRisk(nodeId, horizonNs=1000000000)` 返回
`nodeId/asOfTimeNs/horizonNs/status/pF1/pF2/pCompute/checkCount/permanentlyUnavailable`。
无需 NOTICE、运行任务或 CSV；空闲计算节点也可查询，不伪造 task ID。

- 概率区间是 `(now, now+horizon]`：不包含已到达的当前检查点，包含区间末端检查点。
  使用模型既有离散检查网格；默认 1 秒周期下，1 秒 horizon 对应下一次检查。
  跨多个检查点累计各来源条件概率，再按 `1-(1-pF1)(1-pF2)` 联合。
- 条件是当前忙闲状态保持不变，F2 位置按原生轨道只读外推；不读取未来任务或 F3 日程。
  horizon 可以超过本轮停止时刻，表示同一物理模型继续运行的条件外推，不是额外仿真事件。
- 当前已经不可计算返回 `UNAVAILABLE`，概率为空；永久整星失效另设标志。
  未配置、未绑定、未知节点或仿真已结束返回 `NOT_READY`；非正/溢出 horizon 抛错。
- 查询只修改局部副本，不消耗 RNG、推进时间、改变温度或产生事件；重复查询幂等。
  同一时间戳应在模型事件之后读取，才能观察刚发生的故障；查询不能预知未执行的同刻事件。

此接口不是下面按任务剩余时间计算、包含当前抽样点的 `P_fail_before_finish`，
也不自动启动备份。F3 不并入 F1/F2 概率，零值不代表所有故障风险为零。

## F1 自身状态计算故障

F1 在任务开始/完成、故障开始/恢复和每个检查点，先按**上一忙闲状态**推进实际经过
的纳秒时长，再切换状态。同刻 FCFS 任务交接不制造虚假的冷却间隙；只读查询只外推
副本。默认基础/风险/临界/热平衡温度为 17/20/30/35°C：

```text
gamma = heatingShapeGamma
dT/dt = k_h * (T_sat-T)^gamma                           # busy
k_h = ln((T_sat-T_base)/(T_sat-T_crit)) / t_heat          # gamma = 1
k_h = ((T_sat-T_crit)^(1-gamma) - (T_sat-T_base)^(1-gamma))
      / ((gamma-1)*t_heat)                              # gamma > 1
cooling_rate = (T_crit-T_base)/coolingFromCriticalToBaseSeconds
T_next = max(T_base, T-cooling_rate*dt)                  # non-busy
```

从 17°C 连续计算 30 秒到 30°C，k_h 由此派生，不独立调参。gamma=1 为旧一阶指数，
gamma>1 令前中期升温更快而仍在第30秒到30°C；渐近温度仍为35°C。
实现采用当前温度与真实 elapsed time 的闭式更新，不用离散积分，不在新任务开始时重置。
G3 v3 仅比较 gamma=1.5/2、beta=8/10，可用 --faultF1Gamma/--faultF1Beta 覆盖；
正式选择见阶段报告。非忙碌时统一按 3.25°C/s 线性降温，30°C 到 17°C 用 4 秒。
计算完成和恢复都不清零温度，DoD 只按真实 busy 时长累计，不在恢复时重置。

温度直接映射为**当前参考 1 秒的条件故障概率**，不再使用最大强度 lambdaMax：

```text
pT = 0                                          T <= 20
pT = expm1(beta*(T-20)/10) / expm1(beta)          20 < T < 30
pT = 1                                          T >= 30
pF1_1s = min(1, pT * (1 + 0.1*energyPressure))
```

能源项仅乘性修正，不能在低温时独立制造故障。当前 beta=3；同一温度下 beta 越小，概率越高；
候选/冻结结果见 [G3 阶段证据](../../../docs/n4c/reviews/G3-hotspot-fault-calibration.md)。
可用 `--faultF1Beta` 临时覆盖，其他参数仍集中在 [fault-para.cc](fault-para.cc)。
若改变检查周期，则用 `q(dt)=1-(1-pF1_1s)^dt` 换算抽样概率；实际状态更新 dt 和
概率参考周期不是同一个量。历史多步累计概率不参与当前抽样。

F1 命中时按 START 温度派生恢复时长：
`duration=(T_start-17)/3.25` 秒，向上取整到 ns，范围为 (0,4] 秒。
检查点临界过冲会先钳位到 30°C，再采样/记录，因此不会生成超过 4 秒的 F1 停机。
F2 独立固定 8 秒；同刻两者命中取两种时长的最大值。冷却可以早于 F2 恢复完成，
但不能提前恢复算力。连续每秒抽样允许在低于 50% 的当前概率处发生真实故障；
不能把“多数 START 概率超过 50%”作为验收条件。这是加速实验模型，不是实测热参数。

## F2 空间辐射故障

F2 直接读取正式平台共享的 `OnlineOrbitConstellation` ECEF 坐标，再调用 ns-3.48
`GeographicPositions` 转为经纬度。当前闭区间为：

```text
-90 deg <= longitude <= 5 deg
-50 deg <= latitude  <= 5 deg
```

SAA 内部以文献观测热点 `(-60 deg, -28 deg)` 为中心，使用东西向尺度不同的
two-piece Gaussian 作为系统级平滑空间近似：

```text
w_F2 = exp(-0.5 * ((lon-lon_c)/sigma_side)^2
                 -0.5 * ((lat-lat_c)/sigma_lat)^2)
sigma_side = sigma_west, lon < lon_c
             sigma_east, lon >= lon_c
lambda_SEU(t) = lambda_SEU_max * w_F2(t)
lambda_F2(t) = rho_SF * lambda_SEU(t)
q_F2(t) = 1 - exp(-lambda_F2(t) * dt)
analysis high-risk region: w_F2(t) >= theta_F2
```

`sigma_west < sigma_east` 令高风险等值线区域相对热点呈现西侧较短、东侧较长的形状；
`sigma_lat` 决定南北宽度。`theta_F2` 仅供独立暴露分析划分高风险边界，`rho_SF` 表示模型化 SEU
到计算服务中断的场景级映射系数；它不是实测条件概率。SAA 外第一版令 `w_F2=0`。
SAA 内只要 `w_F2>0` 就可能发生故障；高风险穿越也不保证命中。生产不再产生
NOTICE、NOTICE_CLEAR 或 risk-only 记录；保留的 spatialRiskThreshold 只用于独立绘图/暴露分类。

连续暴露时间、累计 hazard 和一次穿越期间至少发生一次故障的累计概率仍保留为
episode 统计量，但不参与当前 `lambda_F2`、`q_F2` 或随机采样。实际故障
每步只按当前位置对应的 `q_F2(t)` 抽样。8 秒算力停机期间轨道继续演化，但暂停新的
故障抽样；恢复按 FCFS 继续处理队列与后续任务。

F1 与 F2 同时启用时，两者使用互不共享状态的 ns-3 随机流分别抽样：

```text
X_F1 ~ Bernoulli(q_F1)
X_F2 ~ Bernoulli(q_F2)
compute START = X_F1 OR X_F2
q_comp = 1 - (1 - q_F1) * (1 - q_F2)
```

`q_comp` 是 trace、观测和后续预测使用的联合概率，不替代两个来源的真实抽样。同一
节点同一检查时刻即使两个来源同时命中，也只提交一次可恢复 compute 故障。

当前三种星座统一使用以下 [`fault-para.cc`](fault-para.cc) 参数，不按星座规模分别
调参：

```text
sigmaLongitudeWestDegrees = 12
sigmaLongitudeEastDegrees = 24
sigmaLatitudeDegrees = 12
spatialRiskThreshold = 0.5
referenceSeuIntensityPerSecond = 0.002859196111093899
seuToComputeFailureProbability = 0.5
kappa_F2 = 0.0014295980555469494 s^-1
```

在 `theta_F2=0.5` 下，高风险等值线相对热点约向西延伸 `14.13 deg`、向东延伸
`28.26 deg`，南北各延伸 `14.13 deg`。配置矩形仍是整个模型 exposure region，
该等风险线只是其中的 active high-risk region。

每种星座只使用各自空间加权扫描选出的轨道 epoch offset：

| 星座配置 | `orbitStartOffset` | 对应轨道窗口 | 加权暴露量 | 1000 秒解析期望故障数 |
|---|---:|---:|---:|---:|
| `synthetic-66.csv` | 302s | 302--1302s | 1398.9946 | 2.0000 |
| `synthetic-351.csv` | 4704s | 4704--5704s | 7150.5901 | 10.2225 |
| `synthetic-720.csv` | 3478s | 3478--4478s | 14463.6294 | 20.6772 |

`orbitStartOffset` 直接把仿真 `t=0` 映射到相应轨道 epoch，不会先空跑几千秒。
351/720 星若也分别反调到平均 2 次，会破坏规模效应，因此正式实验必须继续使用表中
同一组 F2 强度参数。orbit-only 工具只负责选择窗口和验证规模效应，不创建网络、
路由、任务或故障执行。完整证据见
[`docs/calibration/n4b-f2`](../../../docs/calibration/n4b-f2/README.md)。这些数值是
有限窗口内的系统级加速实验参数，不是原始 SEU 计数或现实卫星绝对失效率。

论文空间分布图已经使用 66 星、100 万秒、`orbitStartOffset=302`、`randomSeed=1`、
`randomRun=1`、1 秒检查周期和 8 秒恢复重新生成。结果为 1888 次故障，条件期望
1880.59，风险—故障率相关系数 0.8224，全部五项空间验收通过。该 orbit-only 验证
只在显式运行工具时生成 CSV/JSON/PNG/SVG/PDF；正常 `none/generate` 不会输出
这些分析文件。50 万秒结果属于对称经度模型的历史证据，不作为当前参数的最终图。
标定、原始证据和论文候选图见
[`docs/calibration/n4b-f2`](../../../docs/calibration/n4b-f2/README.md)。

F2 与 F3 参数都按独立分组保留在 [`fault-para.cc`](fault-para.cc) 中，默认关闭。
`f3.fixedCount` 是 `fixed_k` 压力测试中人工指定的永久撞击卫星数量，不属于任务
输入，也不代表现实碰撞频率。

## F3 致命碎片整星故障

G3 另提供 `--faultF3Mode=controlled --faultF3Node=N --faultF3Time=S` 单事件场景接口，
秒制时间进入模型时转为整数 ns。仅调度器知道未来目标/时刻，在线风险查询不返回它们；
fixed_k/poisson 保留。受控场景不屏蔽该卫星的 F1/F2，也不恢复 production replay。
实验命令必须记录 F1 beta 的覆盖值。probability audit 开启时额外输出 `fault-model-state.csv`，
记录每次模型更新的温度、F1/F2 风险、位置及采样资格；普通运行不输出该诊断文件。

F3 不依赖任务，只从完整的稳定卫星 ID 集合选择节点，并为每次事件写出：

```text
fault_type = satellite
fault_occurred = true
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
活动 compute 停机区间，平台会把 compute 恢复提前到 F3 时刻，再立即执行永久整星
START；生成 trace 中两个区间首尾相接而不重叠，generate 的事件顺序均为
`RECOVERY -> START`。永久失效后不再更新该节点的 F1/F2 状态或消耗其抽样随机数。

## 事件与概率审计

生产事件仅保留 `START` / `RECOVERY`。compute START 表示真实计算故障，不是
主动备份开关；satellite START 表示永久整星故障。旧预警阈值、risk episode、
notice/lead/risk-duration 字段已移除。trace 与事件表保存 START **抽样当时**的
`p_f1/p_f2/failure_probability=q_comp`、来源命中、温度及连续 busy 时长。
RECOVERY 行携带的是对应 START 元数据，不表示恢复时的概率或温度。

`faultProbabilityAudit=1` 才启用独立影子状态和逐任务 CSV。每个检查点对当前
可计算节点的所有 RUNNING 任务，在实际抽样前准备预测，不由风险阈值门控：

```text
q_comp,k = 1 - (1-q_F1,k)*(1-q_F2,k)
K = floor(remaining_compute_time/check_interval) + 1
P_fail_before_finish = 1 - product(k=0..K-1, 1-q_comp,k)
```

k=0 包含当前抽样点，后续点不越过任务预计完成时刻。F1 未来按“任务存活并持续
计算”推进，F2 复用原生轨道只读外推，F3 不并入该概率。compute START 当刻保留
最后一条抽样前记录；故障后、任务完成后不再为该任务预测。同刻永久 F3 优先时不
输出该节点的 compute 预测。没有读取未来故障、未来队列或任何事后风险时长。

验证器按 `(simulation_time_ns,node_id,task_id)` 比较真实在线状态与独立影子状态的
q_F1、q_F2、q_comp、P_fail_before_finish 和任务窗口上下文，容差 1e-12。
它验证实现一致性，不把一次随机命中作为概率真值，也不计算 Brier score。
正式运行默认关闭审计/CSV；在线 QueryComputeRisk 不依赖此开关。
本阶段不实现主动备份阈值、checkpoint、BACKUP_START 或接管；这些留待 N5。

## 故障执行

同一纳秒的事件固定按下列顺序、再按 `fault_id` 升序执行：

```text
RECOVERY -> START
```

compute START 只令目标节点 `compute_available=false`，不会关闭 ISL 或重算路由。
只直接中断 RUNNING 任务；QUEUED 保留，INPUT/新到达可继续传输入队，RESULT 不受影响。
有限恢复后按原 FCFS 调度，旧 FAILED victim 不复活。停机期间仍更新模型状态，
但不新增 F1/F2 抽样或重叠停机；同次 F1/F2 命中仅产生一次 compute START。

satellite START 同时关闭整星、通信和计算，在精确时刻更新有效 ISL 并在边集合变化时
立即重算 IPv4。恢复会读取当时的实时轨道位置，只恢复仍满足距离门限的固定候选；
永久故障则没有 RECOVERY。

完整 JSON 合同见 [`input/fault/README.md`](../input/fault/README.md)，运行指标见
[`metrics/README.md`](../metrics/README.md)，可执行闭环见
[`66 星 F1 示例`](../input/examples/leo-66-120s-f1/README.md)与
[`66 星 F2 示例`](../input/examples/leo-66-1000s-f2/README.md)；不需要任务输入的
F3 fixed-K 流程见
[`66 星 F3 示例`](../input/examples/leo-66-1000s-f3/README.md)。
