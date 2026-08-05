# 卫星拓扑、稳定身份与链路状态

平台只通过 `SatelliteTopology` 使用拓扑。该 facade 保持 ns-3.33 的公共职责和
节点/卫星 ID 查询外观，并依据 resolved config 在内部选择
`OnlineTopologyController` 或 `ReplayTopologyController`。流量、任务、路由指标
和平台入口都面向 facade 的运行时接口，不直接判断具体 controller。

online 模式额外通过 facade 暴露只读的 `OnlineOrbitConstellation`，供正常仿真中
的拓扑切片导出使用；replay 模式访问该接口会明确失败。轨道、candidate、距离
门控和时延的具体算法仍留在 `orbit/` 与 `online/` 内部。

## 稳定卫星身份与 IPv4 地址

卫星身份来自星座或拓扑输入。`SatelliteIdMap` 保存显式双向映射，绝不从
`Node::GetId()` 推导卫星 ID。canonical 顺序按外部卫星 ID 升序确定，与 ns-3
节点创建顺序无关。

IPv4 service 地址位于 `172.16.0.0/12`，按 canonical 卫星顺序分配。每颗卫星
保留 legacy 的独立 service interface，因此首个 PointToPoint ISL 仍是 interface
2。ISL `/30` 网段从 `10.0.0.0/8` 按固定候选链路的 canonical 顺序分配。

facade 同时提供两种无歧义查询：

- `GetServiceAddress(index)`：按 ns-3.33 外观使用节点下标；
- `GetServiceAddressBySatelliteId(satelliteId)`：按稳定外部卫星 ID 查询，供运行时
  数据合同使用。

## 固定候选链路与更新

`SatelliteLinkState` 每次原子应用一个完整有效链路集合。它先校验并 canonicalize
全部逻辑输入，再改变设备状态。被移除的链路保留设备、地址和 output-interface
身份，只把两端 IPv4 interface 置为 down；恢复时重新启用相同接口。

replay controller 在仿真开始前扫描选中的全部切片，并预安装候选链路并集。
online controller 则预安装 plus-grid 固定候选集合。初始时无效的候选两端均为
down；运行期只重配属性或切换已知接口，不会晚建 TrafficControl 尚未初始化的
NetDevice。

更新摘要把“有效边集合变化”与“时延/带宽属性变化”分开。controller 只在
`ActiveEdgeSetChanged()` 为 true 时重算 IPv4 路由并推进 route epoch；仅更新
distance 时延不会重建当前 hop-based 路由。路由更新 callback 只在新路由和
epoch 已可见后触发。

## online 与 replay 的共同合同

两种 controller 都安装相同的 ns-3.48 SatCompute 全局路由适配器，支持：

- `global-first`；
- `global-hash-per-flow`；
- `global-hrw-per-flow`；
- `global-size-aware-hrw`；
- `global-capacity-aware-hrw`。

reservation-aware 模式共享一个 controller 所有的 `FlowRouteRegistry`，确保每颗
卫星看到同一份 flow 状态。facade 同时提供完整路径只读视图、稳定下一跳映射、
ISL 速率、route epoch、定向链路和队列 Drop 事件。

online 模式连续计算 ns-3.48 圆轨道位置，但只在 `networkUpdateInterval` 网络 tick
应用链路状态。replay 模式按同一参数从全量 JSON 切片目录选取应用点。两种模式
都使用 `para.cc`/CLI 中的带宽、MTU、队列、时延模式和路由参数；拓扑 JSON 不会
覆盖这些平台运行参数。

独立的切片导出周期、manifest 和前端只读状态边界见
[`export/README.md`](export/README.md)。
