# SatCompute 输入

SatCompute 不使用一个完整 JSON 同时控制星座、运行参数、任务和故障模型。平台
运行参数保留在 `para.cc`/CLI，故障内部参数保留在 `fault-para.cc`，其余结构化
数据按职责拆分：

| 输入 | 位置 | 负责内容 | 不负责内容 |
|---|---|---|---|
| 平台参数 | `para.h/.cc` 与同名 CLI | 仿真时间、更新周期、链路、路由、随机数、运行模式和输出 | 星座结构、算力、任务和故障内部参数 |
| 故障模型参数 | `../fault/fault-para.h/.cc` | common、F1、F2、F3 的内部模型参数 | 星座、任务和已发生事件 |
| Constellation | `topology/constellations/*.csv` | 轨道高度、倾角、轨道面和每面卫星数 | 仿真时长、链路策略和任务 |
| ComputeProfile | `topology/resources/workload/*.json` | 稳定卫星 ID 对应的静态算力 | 轨道位置和任务到达 |
| TaskTrace | `traffic/workload/*.json` | 任务端点、输入/输出大小、计算量和到达时刻 | 网络与星座参数 |
| FaultTrace | generate 输出 | 已确定的风险/故障 episode | 生产输入和拓扑回放 |

`topologyOnly` 生成的节点和链路 JSON 是输出，不是正式仿真的拓扑输入。正式仿真
使用相同星座和参数在线计算确定性拓扑。`faultMode=generate` 根据实时平台状态生成
并执行故障，同时写出 v2 trace；不再提供故障文件输入。字段合同
见 [`fault/README.md`](fault/README.md)。

可直接运行的组合示例见
[`examples/leo-66-100s-20tasks/`](examples/leo-66-100s-20tasks/README.md)：
100 秒、66 颗卫星、22 个计算节点和 20 个无故障任务；F1 generate 闭环见
[`examples/leo-66-120s-f1/`](examples/leo-66-120s-f1/README.md)，F2 的 1000 秒
小任务闭环见
[`examples/leo-66-1000s-f2/`](examples/leo-66-1000s-f2/README.md)，无需任务输入的
F3 永久整星闭环见
[`examples/leo-66-1000s-f3/`](examples/leo-66-1000s-f3/README.md)。N4B 最终的
66 星、1000 秒、100 任务修订前联合验收归档见
[`examples/leo-66-1000s-n4b-joint/`](examples/leo-66-1000s-n4b-joint/README.md)。
