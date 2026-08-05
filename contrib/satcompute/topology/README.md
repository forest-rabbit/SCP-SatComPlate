# 卫星拓扑模块

`topology/` 负责从一个 ns-3.48 原生 LEO shell 建立稳定卫星身份、实时轨道位置、
固定候选 ISL、在线链路状态和 IPv4 地址。正式仿真和 topology-only 输出共享
`OnlineOrbitConstellation`、`BuildPlusGridCandidateLinks` 与
`CircularOrbitTopologyPolicy`，不会分别维护两套轨道或拓扑算法。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `satellite-topology.h/.cc` | 平台唯一 topology facade，转发在线 controller 的查询与生命周期 |
| `satellite-id-map.h/.cc` | 稳定卫星 ID 与 ns-3 `Node` 的双向映射 |
| `satellite-endpoint-view.h` | 任务输入读取所需的稳定 ID/service address 只读接口 |
| `satellite-runtime-view.h` | 传输与 capacity-aware 路由所需的在线服务接口 |
| `satellite-topology-controller.h` | topology controller 公共生命周期和计数接口 |
| `orbit/constellation-definition.h/.cc` | 读取并严格校验一个原生 `LeoOrbitalShell` CSV |
| `orbit/online-orbit-constellation.h/.cc` | 用 `LeoOrbitNodeHelper` 创建节点和圆轨道 mobility，按需读取 ECEF 坐标 |
| `online/plus-grid-candidate.h/.cc` | 在 `t=0` 构造固定同轨/异轨候选卫星对 |
| `online/circular-orbit-topology-policy.h/.cc` | 计算距离、active 状态和 fixed/distance 时延 |
| `online/online-topology-controller.h/.cc` | 安装协议栈、周期更新链路并按需重算路由 |
| `link/satellite-link.h` | 无向候选链路的最小运行记录 |
| `link/satellite-link-state.h/.cc` | PointToPoint 设备、队列、接口启停和 Drop trace |
| `ipv4/satellite-ipv4-addressing.h/.cc` | 确定性的 service address 分配 |
| [`export/topology-slice-exporter.h/.cc`](export/README.md) | topology-only 采样和节点/链路 JSON 写出 |

## 星座与稳定身份

星座输入只包含一个 shell，格式见
[`input/topology/constellations/`](../input/topology/constellations/README.md)。若轨道面
数量为 `P`、每面卫星数为 `S`，稳定 ID 为：

```text
satellite_id(plane, slot) = plane * S + slot
```

因此 ID 空间固定为 `0..P*S-1`，与进程中其他 ns-3 节点的创建历史无关。
`SatelliteIdMap` 显式保存这一映射，代码不能用 `Node::GetId()` 代替外部卫星 ID。

星座解析器在创建节点前拒绝非 UTF-8、拼错的表头、多余数据行和非六列 shell；
随后用 ns-3.48 球形地球半径校验 `maxIslDistance` 的 80 km clearance 上限。
该跨输入校验只执行一次，不进入轨道 mobility 或周期 topology tick。

`OnlineOrbitConstellation` 只封装 ns-3.48 的
`LeoCircularOrbitMobilityModel`。坐标连续随仿真时间演化；网络更新周期和拓扑切片
周期只决定何时采样，不改变轨道自身的演化。

## 固定 plus-grid 候选

同轨候选把每个 slot 与 `(slot + 1) mod S` 相连，形成环。对于相邻轨道面 `p` 与
`p+1`，平台在 `t=0` 枚举所有循环偏移 `delta`：

```text
D_p(delta) = sum over s of
             distance(position(p, s, 0), position(p+1, (s+delta) mod S, 0))

delta_p* = argmin D_p(delta)
```

偏移按升序枚举，距离总和完全相同时保留最小 `delta`。选定偏移后，每颗卫星与
相邻轨道面的一颗卫星形成一对一连接，卫星对在整个仿真中不再改变。轨道面 `0`
与 `P-1` 之间不建立 seam。

默认 `P=6`、`S=11` 时共有 66 条同轨候选和 55 条异轨候选，合计 121 条。这里的
“最近”只发生在 `t=0` 的匹配阶段，不表示以后每个 tick 重新选择瞬时最近卫星。

## 距离门控与时延

每个采样点只评估已固定候选。对候选两端的 ECEF 直线距离 `d`：

```text
active = (d <= maxIslDistance)

fixed:    delay_ns = round(fixedDelay * 1e9)
distance: delay_ns = round(d * 1e9 / 299792458)
```

超过门限的候选不会被替换或删除，只暂时变为 inactive。恢复到门限内时重新启用
相同的设备、IPv4 地址和 output interface。

`SatelliteLinkState` 在初始化时为全部候选建立 PointToPoint 设备上界，随后以完整
快照原子应用 active 状态。它分别报告边集合变化与 delay/data-rate 属性变化，
从而避免仅因 distance 时延更新就重算 hop-based 路由。

## 在线更新与路由

正式仿真在 `t=0` 应用第一次拓扑，随后在满足
`k * networkUpdateInterval < simulationDuration` 的时刻更新。每次更新顺序为：

1. 从原生 mobility 读取当前 ECEF 坐标；
2. 重新计算全部固定候选的距离、active 状态和时延；
3. 原子应用链路快照；
4. 若 active 边集合改变，调用 ns-3 全局路由重算并推进一次 route epoch；
5. 路由表稳定后通知 capacity-aware transfer engine 处理失效路径。

因此 fixed 模式可使用较大的更新周期，例如 20 秒；distance 模式通常使用 1 秒或
2 秒刷新传播时延。两者都是 `para.cc`/CLI 输入。

## IPv4 地址

- 每颗卫星按稳定 ID 获得一个 `172.16.0.0/12` service host address；
- 候选 ISL 按 canonical 卫星对顺序从 `10.0.0.0/8` 分配 `/30` 网段；
- inactive 链路保留其地址和 interface identity，只把接口置为 down。

service address 用于任务端点和 host route。当前拓扑模块只提供 IPv4；IPv6/SRv6
会在后续阶段单独设计，不在现有类中预留半成品分支。

## 输入、输出与测试

- 星座与算力输入见 [`input/topology/`](../input/topology/README.md)；
- topology-only JSON 见 [`export/README.md`](export/README.md)；
- `tests/unit/online-orbit-foundation-test.cc` 检查原生位置和固定候选；
- `tests/unit/online-topology-controller-test.cc` 检查门控、时延、tick 与路由重算；
- `tests/integration/smoke/run-topology-smoke.sh` 检查切片合同和重复运行确定性。
