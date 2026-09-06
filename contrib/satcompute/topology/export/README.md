# topology-only 切片

`TopologySliceExporter` 是 `satcompute --topologyOnly=1` 使用的轻量输出组件。
实现位于 `topology-slice-exporter.h/.cc`：头文件定义采样时间、文件名和运行结果
合同，源文件负责调度、共享拓扑求值以及临时文件加 rename 的原子 JSON 写出。

该模式只创建原生轨道节点和 mobility，不创建 InternetStack、NetDevice、路由、
FlowMonitor、任务或正式指标。

## 采样时间

给定仿真持续时间 `T` 和切片间隔 `Delta`，基础采样集合为：

```text
{0} union {k * Delta | k >= 1 and k * Delta < T}
```

`includeFinalTopologyState=true` 时再加入精确终点 `T`。所有秒制参数先统一转换为
整数纳秒，因此事件时刻、文件名和 JSON 中的 `simulation_time_ns` 使用同一数值。

例如 `T=2.5`、`Delta=1`、包含终点时，文件名时间依次为 `0`、`1`、`2`、`2.5`；
`T=100`、`Delta=1` 时共输出 101 对文件。

## 运行

```bash
./ns3 run "satcompute \
  --simulationDuration=100 \
  --topologyOnly=1 \
  --topologySliceInterval=1 \
  --includeFinalTopologyState=1 \
  --outputDir=/tmp/satcompute-topology"
```

切片写入 `/tmp/satcompute-topology/topology/`：

```text
nodes_0s.json
links_0s.json
nodes_1s.json
links_1s.json
...
```

非整数秒会去掉多余尾零，例如 `1.000000001s`。每个 JSON 文件只描述文件名所示
采样时刻的节点或候选链路状态。

## 节点文件

```json
{
  "simulation_time_ns": 0,
  "nodes": [
    {
      "node_id": 0,
      "node_type": "sat",
      "x": 7149137.0,
      "y": 0.0,
      "z": 0.0
    }
  ]
}
```

`x/y/z` 是 ns-3.48 原生 mobility 在该仿真时刻给出的 ECEF 米制坐标。节点按稳定
卫星 ID 升序写出。

## 链路文件

```json
{
  "simulation_time_ns": 0,
  "links": [
    {
      "node1_id": 0,
      "node2_id": 1,
      "type": "sat",
      "active": true,
      "distance_m": 2000000.0,
      "delay_ns": 8000000,
      "link_bandwidth_bps": 10000000000
    }
  ]
}
```

链路文件始终包含全部固定候选。`active=false` 的候选仍保留其两端 ID、当前距离、
当前时延和配置带宽，因此故障生成器或可视化前端可以观察完整候选身份，而不是把
暂时越过距离门限误判为永久删除。

## 与正式仿真的关系

切片面向可视化和未来故障生成，不作为正式网络仿真的 replay 输入。只要星座、
拓扑参数、seed/run 和采样时间相同，topology-only 与正式仿真会调用同一轨道和
候选链路策略得到相同状态；正式仿真仍在线计算。

对应测试为 `tests/integration/smoke/run-topology-smoke.sh`，覆盖终点采样、字段集合、
坐标演化、inactive 候选保留和两次运行逐字节一致。
