# 卫星拓扑、稳定身份与链路状态

平台只通过 `SatelliteTopology` 使用拓扑。该 facade 保持 ns-3.33 的节点、卫星
ID、路由和链路状态查询职责，内部统一使用 `OnlineTopologyController`。正式仿真
读取星座 CSV 并实时计算轨道，不读取 topology-only 生成的 JSON 切片。

## 稳定卫星身份与 IPv4 地址

`LeoOrbitNodeHelper` 按 plane-major 顺序创建卫星，`SatelliteIdMap` 再显式保存
外部卫星 ID 与 ns-3 节点的双向映射。平台不会用 `Node::GetId()` 充当卫星 ID。

IPv4 service 地址位于 `172.16.0.0/12`，每颗卫星保留独立 service interface。
ISL `/30` 网段从 `10.0.0.0/8` 按固定候选链路的 canonical 顺序分配。

## 固定候选链路与周期更新

`CircularOrbitTopologyPolicy` 在 `t=0` 生成固定 plus-grid 候选：同轨连接环形前后
邻居；每对相邻轨道面选择总距离最小的循环一对一 slot 偏移。首尾轨道面不跨
seam 连接。每个网络 tick 只读取当前坐标、距离门限和时延模式，不会重新选择
异轨对端。

`SatelliteLinkState` 预先安装候选设备集合。距离超过门限时保留设备、地址和
output-interface 身份，只把接口置为 down；恢复时重新启用相同接口。更新摘要将
“有效边集合变化”和“时延属性变化”分开：只有前者触发 IPv4 路由重算和 route
epoch 前进，distance 时延刷新本身不重算 hop-based 路由。

在线 controller 支持以下五种 IPv4 路由模式：

- `global-first`；
- `global-hash-per-flow`；
- `global-hrw-per-flow`；
- `global-size-aware-hrw`；
- `global-capacity-aware-hrw`。

拓扑预处理模式及节点、候选链路 JSON 合同见
[`export/README.md`](export/README.md)。预处理与正式仿真共享同一轨道和拓扑策略，
但前者不安装 InternetStack、NetDevice、路由、任务或指标对象。
