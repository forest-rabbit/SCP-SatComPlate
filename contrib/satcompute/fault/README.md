# 故障模块

`fault/` 负责统一故障参数、Fault Trace、在线模型以及运行期执行。
`FaultModelEngine` 只读取实时状态并判定风险/故障；所有 compute/整星状态变化仍
统一交给 N4A `FaultController`，模型不会直接修改任务、transfer、链路或路由。

## 文件与职责

```text
fault/
├── fault-para.h/.cc             人工维护的 common/F1/F2/F3 参数
├── parameter/                   参数合法性校验
├── model/                       无运行期副作用的 F1/F2/F3 纯模型
├── runtime/                     在线判定、状态覆盖与故障执行
├── trace/                       统一记录定义、JSON 读取和写出
└── README.md
```

| 文件 | 职责 |
|---|---|
| `fault-para.h/.cc` | 按 common、F1、F2、F3 分组的唯一内置故障参数 |
| `parameter/fault-parameter-validator.h/.cc` | 有限值、范围及跨字段关系的启动前校验 |
| `model/self-state-fault-model.h/.cc` | 无运行期副作用的 F1 温度、DoD、风险、强度和单步概率 |
| `runtime/fault-model-engine.h/.cc` | 在线读取状态、维护风险 episode、使用 ns-3 随机流判定事件 |
| `runtime/fault-state.h/.cc` | 每颗卫星的 satellite/communication/compute 可用性与活动故障集合 |
| `runtime/fault-controller.h/.cc` | replay/在线事件批处理，以及任务、传输和有效拓扑联动 |
| `trace/fault-definition.h/.cc` | compute/satellite 记录以及预警、恢复、风险结束和排序时间 |
| `trace/fault-trace.h/.cc` | v1 兼容读取、v2 closed-world 校验、canonical writer |

## 三种运行模式

```text
none
  不创建 FaultController 或模型 tick，也不产生故障专用输出

generate
  当前 ComputeService 状态 -> F1 模型 -> 一次概率采样
  -> FaultController 精确执行 -> 写出 Fault Trace v2

replay
  只读取已经确定的 v1/v2 Fault Trace
  -> 不读取温度、负载或轨道重新抽样 -> 精确重放
```

`generate` 中确实会发生故障，并同时写出本轮实际执行的 trace。随后使用相同星座、
任务和该 trace 进入 `replay`，应得到相同的 NOTICE、NOTICE_CLEAR、START、RECOVERY、
任务终态、transfer 终态和路由变化。失败的旧任务不会在恢复时复活；恢复只允许后来
到达的任务继续使用节点。

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
每个 tick 只从按稳定卫星 ID 分配的 ns-3 stream 取一个随机数；禁用 F2/F3 不会改变
F1 随机序列。

当前唯一参数入口是 [`fault-para.cc`](fault-para.cc)，修改后需要重新编译。公共参数
使用秒，进入运行期后才严格换算为整数 ns；当前检查周期为 1 秒，可恢复计算故障
持续 8 秒。`coolingTauSeconds=40` 是降温曲线的时间常数，8 秒则是保护停机时长，
二者含义不同。F1 参数的校准证据见
[`docs/calibration/n4b-f1`](../../../docs/calibration/n4b-f1/README.md)。这些数值面向
1000 秒加速实验，不表示现实卫星热常数或故障率。

F2/F3 参数也已按独立分组保留在同一文件中，但当前默认关闭。其中
`f3.fixedCount` 是未来 `fixed_k` 模式下人工指定的永久撞击卫星数量，不属于任务
输入；对应模型接入后再完成参数标定。

## 风险 episode

节点第一次满足 F1 风险阈值时产生 NOTICE，并保存当时的联合单步概率：

```text
无风险 -> 风险有效       NOTICE
风险有效 -> 风险退出     NOTICE_CLEAR + fault_occurred=false
风险有效 -> 实际故障     START + warning_lead_time
无风险 -> 实际故障       START，notice/lead time 为 null
```

风险-only 记录只进入 trace 和事件证据，不改变节点可用性。仿真结束时仍未退出的风险
episode 以仿真终点关闭。未来 F2 会与 F1 共享同一个节点风险 episode；当前 trace
不暴露风险来源。

## 故障执行

同一纳秒的事件固定按下列顺序、再按 `fault_id` 升序执行：

```text
NOTICE -> NOTICE_CLEAR -> RECOVERY -> START
```

compute START 只令目标节点 `compute_available=false`，不会关闭 ISL 或重算路由。
目标节点上尚未越过计算阶段的任务按 N4A 合同失败；有限恢复只接纳新任务。

satellite START 同时关闭整星、通信和计算，在精确时刻更新有效 ISL 并在边集合变化时
立即重算 IPv4。恢复会读取当时的实时轨道位置，只恢复仍满足距离门限的固定候选；
永久故障则没有 RECOVERY。

完整 JSON 合同见 [`input/fault/README.md`](../input/fault/README.md)，运行指标见
[`metrics/README.md`](../metrics/README.md)，可执行闭环见
[`66 星 F1 示例`](../input/examples/leo-66-120s-f1/README.md)。
