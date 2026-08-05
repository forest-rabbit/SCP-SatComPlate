# SatCompute 输入

SatCompute 不使用一个完整 JSON 同时控制星座、运行参数和任务。不同输入按职责
拆分，重复参数只保留在 `para.cc`/CLI 中：

| 输入 | 位置 | 负责内容 | 不负责内容 |
|---|---|---|---|
| 平台参数 | `para.h/.cc` 与同名 CLI | 仿真时间、更新周期、链路、路由、随机数和输出 | 星座结构、算力、任务和故障数据 |
| Constellation | `topology/constellations/*.csv` | 轨道高度、倾角、轨道面和每面卫星数 | 仿真时长、链路策略和任务 |
| ComputeProfile | `topology/resources/workload/*.json` | 稳定卫星 ID 对应的静态算力 | 轨道位置和任务到达 |
| TaskTrace | `traffic/workload/*.json` | 任务端点、输入/输出大小、计算量和到达时刻 | 网络与星座参数 |
| FaultTrace | `fault/*.json` | 精确 notice/start/duration 与风险元数据 | 拓扑切片回放和运行时概率抽样 |

`topologyOnly` 生成的节点和链路 JSON 是输出，不是正式仿真的拓扑输入。正式仿真
使用相同星座和参数在线计算确定性拓扑，正式故障运行只额外叠加确定性事件。字段
合同见 [`fault/README.md`](fault/README.md)。

可直接运行的组合示例见
[`examples/leo-66-100s-20tasks/`](examples/leo-66-100s-20tasks/README.md)：
100 秒、66 颗卫星、22 个计算节点和 20 个任务。
