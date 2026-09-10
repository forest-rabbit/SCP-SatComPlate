# 正式实验输入

本目录只保存正式实验输入，当前唯一入口为 [LEO-66 Final Experiment Scene](experiments/leo-66/README.md)。
平台参数仍在 `para.cc`/CLI，故障内部参数仍在 `fault/fault-para.cc`，不使用完整配置 JSON。

| 输入 | 正式位置 | 职责 |
|---|---|---|
| 星座 | `experiments/leo-66/topology/constellation.csv` | 原生轨道结构，不包含运行时间/链路策略 |
| 算力 | `experiments/leo-66/compute/compute-profile.json` | 每星稳定 ID 和 WU/s |
| 任务 | `experiments/leo-66/workload/task-trace.json` | 已冻结的到达时刻、端点、INPUT/RESULT/WU |
| 放置记录 | `experiments/leo-66/placement/placement-manifest.json` | 生成来源，不是实时拓扑输入 |
| F3 输入 | `experiments/leo-66/fault/f3-manifest.json` | 正式 runner 使用的受控 F3 节点/时刻 |

测试专用数据位于 [tests/fixtures](../tests/fixtures/README.md)，标定专用数据在对应验证工具的 fixture 目录。
原生位置/链路切片、FaultTrace、概率审计和业务指标均为运行输出，不属于正式输入。
平台在线计算轨道并执行 generate 故障；FaultTrace 不作为生产 replay 输入。

字段合同见[任务模块](../task/README.md#输入-json-合同)、
[拓扑模块](../topology/README.md#星座-csv-合同)和[故障模块](../fault/README.md#故障事件-json-输出合同)。
