# 73星6地面站 JsonTopo 示例

本目录是客户尺度 JsonTopo 运行示例，用于验证客户侧 `JsonTopo + OSPF`
路径。该示例不是 legacy 星座预设，不包含轨道元数据；节点和链路完全由 JSON 文件定义。

## 文件

```text
nodes_0s.json      # 初始卫星和地面站节点
topology_0s.json   # 初始完整链路
nodes_5s.json      # 5s 节点/簇状态快照
topology_5s.json   # 5s 完整链路快照，用于验证运行期拓扑更新
nodes_10s.json     # 10s 节点/簇状态快照
topology_10s.json  # 10s 完整链路快照
```

交付目录不包含生成脚本。甲方只需要关注 JSON 文件格式、节点编号和链路字段。
feeder 链路是特意构造的覆盖样例，不代表某个真实可见性优化结果；每个时间片中
6 个地面站各连接 2 颗卫星，用于展示一个地面站可以同时拥有多条 feeder 链路。

## feeder 覆盖情况

```text
一个地面站多条 feeder:
  每个 topology_*.json 中，2000-2005 每个地面站各有 2 条 feeder。

有 hold_time 和无 hold_time 混合:
  例如 2001<->13、2003<->37、2005<->61/2004<->53 不写 hold_time，
  用于表示由后续拓扑快照控制的稳定 feeder。

同一时刻多条 hold_time 到期:
  topology_0s.json 中 2000<->0 和 2000<->1 都是 hold_time=3，
  运行到 3s 时会用一条 [TOPO:HoldTime] 汇总日志展示。

拓扑快照恢复已到期 feeder:
  2000<->0、2000<->1 在 3s 到期，topology_5s.json 又写回，
  用于展示恢复链路。

拓扑快照主动替换/断开 feeder:
  例如 2002<->25、2003<->36、2004<->48 在 5s 快照中消失，
  用于展示完整快照会关闭未出现的链路。

保持同一 feeder 但调整 hold_time:
  例如 2002<->24 从 0s 的 hold_time=12 调整为 5s/10s 的 hold_time=15。
```

## 快速验证

```bash
./waf --run "satcompute --offeredload=0 --nodesJson=examples/satcompute/input/topology/json/examples/customer-73sat-6gs/nodes_0s.json --topologyJson=examples/satcompute/input/topology/json/examples/customer-73sat-6gs/topology_0s.json --trafficMatrix=examples/satcompute/input/traffic/traffic_matrix(73).csv"
```

该甲方分支使用 ns-3 OSPF 全局路由；内部 ECMP 和自定义簇路由不在交付范围内。
