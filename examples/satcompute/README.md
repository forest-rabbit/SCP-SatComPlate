# SatCompute 运行说明

## 执行模型

SatCompute 只有一条主流程：

1. 扫描配对的 `nodes_<time>s.json` 与 `topology_<time>s.json` 全量快照；
2. 从最早快照创建全部卫星节点；
3. 创建星间 PointToPoint 链路并分配 `/30` 地址；
4. 使用 ns-3 `Ipv4GlobalRouting` 生成路由表；
5. 在后续快照时刻更新 ISL，并统一重算原生全局路由；
6. 可选地累计 100 个流量时间片，并按得到的 NxN 矩阵创建 UDP 或 TCP 业务；
7. 输出 FlowMonitor 和应用层统计。

程序不会创建地面站，不解析 cluster，不支持 CSV 拓扑，也没有自定义路由模式。

## 构建与运行

```bash
source .venv/bin/activate
./waf configure --enable-examples --enable-tests
./waf build
./waf --run satcompute
```

默认配置：

```text
topologyDir       = examples/satcompute/input/topology/json/examples/xw-66sat
simulationDuration= 110
offeredLoad       = 0
transport         = udp
trafficMatrix     = examples/satcompute/input/traffic/traffic_matrix(66).csv
outputDir         = examples/satcompute/output
```

## 参数

- `--topologyDir`：卫星 JSON 全量快照目录。
- `--simulationDuration`：仿真时长，单位秒，必须大于 0。
- `--offeredLoad`：业务矩阵倍率，必须非负；为 0 时不读取矩阵。
- `--transport`：`udp` 或 `tcp`。
- `--trafficMatrix`：100×N 行、N 列的 CSV 业务输入，值的单位为 Gbps。
- `--outputDir`：结构化指标输出目录。

示例：

```bash
./waf --run "satcompute \
  --topologyDir=/path/to/snapshots \
  --simulationDuration=110 \
  --offeredLoad=0.001 \
  --transport=udp \
  --trafficMatrix=/path/to/traffic.csv \
  --outputDir=/tmp/satcompute-run"
```

## 地址与路由

卫星按外部 `sat_id` 升序映射到 ns-3 节点。每颗卫星获得稳定的
`172.16.0.0/12` 范围 `/32` 业务地址；每条曾出现的 ISL 获得独立的
`10.0.0.0/8` 范围 `/30` 网段。

初始建图使用 `PopulateRoutingTables()`，运行期完整快照使用
`RecomputeRoutingTables()`。链路的消失、恢复和首次出现均支持。

## 业务输入

业务文件必须恰好有 100×N 行、N 列，N 等于卫星数。每连续 N 行作为一个
时间片，行列顺序都是 `sat_id` 数值升序。程序沿用旧版 xw 的索引和累计逻辑：
输出矩阵的第 `m` 行等于输入第 `m + n×N` 行在 `n=0…99` 上的逐列求和。
其他单元表示源卫星到目的卫星的 Gbps 需求。
实际发送速率为：

```text
matrix_value × offeredLoad × 1e9 bps
```

## 输出

- `network-flow-metrics.csv`：原生 FlowMonitor 汇总；
- `task-metrics.json`：运行配置摘要和所有 PacketSink 的接收字节。
