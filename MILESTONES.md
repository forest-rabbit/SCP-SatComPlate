# SatCompute 里程碑

本文件按阶段记录已经完成的主要工作、实验结论和边界，不作为逐日开发日志。
每个步骤给出完成日期及可追溯的 PR、tag 或代表性提交。阶段压力实验记录在实际
执行它的阶段；ECMP 专项 fixture 与算法演进统一记录在文末。

N0–N2 是 ns-3.33 版本的原始里程碑，相关 PR 位于旧 SatCompute 仓库，完整代码
与报告保留在只读分支 `legacy/ns-3.33`。迁移到 ns-3.48 的全部工作统一记为 N3，
不因迁移过程中的内部批次重新编号旧阶段。

## 阶段状态

| 阶段 | 状态 | 集成或冻结点 | 日期 |
| --- | --- | --- | --- |
| N0：初始网络平台 | 已完成 | `n0-complete` / `d67ca0a` | 2026-07-26 |
| N1：最小任务计算闭环 | 子里程碑已完成 | `n1-complete` / `8d6d79f` | 2026-07-28 |
| N1-ECMP：N1 最终收口 | 已完成，整个 N1 在此结束 | `n1-ecmp-complete` / `58e031e` | 2026-07-28 |
| N2A：动态星座场景 | 已完成 | PR #13–#18 / `991211c` | 2026-07-31 |
| N2B：代表性压力验证 | 已完成 | `69e845b` / `db0ea2a` | 2026-08-03 |
| N2：动态星座与压力验证 | 已完成 | `feature/n2-integration` → `main` / `n2-complete` | 2026-08-03 |
| N3：迁移到 ns-3.48 | 已完成 | [PR #72](https://github.com/forest-rabbit/SCP-SatComPlate/pull/72) / `n3-complete` / `b83bf646b` | 2026-08-05 |
| N4A：确定性故障输入与执行基础 | 已完成 | PR #75–#80 / `n4a-complete` | 2026-08-05 |
| N4B：统一故障建模与因果概率预测 | 已完成 | [PR #81](https://github.com/forest-rabbit/SCP-SatComPlate/pull/81) / `n4b-complete` | 2026-08-31 |
| N5 前置：任务增量与压力基线 | 实验基础已完成，尚未实现备份算法 | 平台 PR #86 / TaskModeling PR #5 | 2026-09-06 |

N2 的最终发布链固定为 `feature/n2-integration` 合入旧仓库 `main`，并以
annotated tag `n2-complete` 冻结。N2A 与 N2B 均已完成；该 tag 不移动 N0、N1
或 N1-ECMP 已有的阶段冻结点。N3 使用新仓库的 ns-3.48 主线，但沿用并保留这些
历史 tag。

分支待集成引用（2026-09-09）：N4C G3正式800任务场景与G4旁路解析评估已人工接受，
清理后以`n4c-g4-frozen`冻结，见[G4冻结索引](docs/n4c/reviews/G4-final-freeze.md)。
当前唯一默认为1300 s、66星、1 ms；G4为387 START、382 ON、77/82故障时已保护，
解析net lifecycle saving为94.716%，不代表真实备份性能。
历史`n4c-g3-frozen`不移动；尚未合入main、未运行阶段CI，N5A未开始，
不记作N4C整阶段主线完成。

## N0：初始网络平台

### 2026-07-25：建立确定性的纯星上拓扑

仿真只创建卫星与 ISL，使用节点、链路分离的 JSON 全量快照，并通过稳定节点
编号和 canonical endpoint 保证相同输入得到相同拓扑身份。

- 证据：`eeb5d33`

### 2026-07-25：建立网络传输与初始 ECMP

NetworkTransfer JSON 描述逻辑传输，平台负责 UDP 收发和逐流指标。在 ns-3
`Ipv4GlobalRouting` 生成的等价最短下一跳上加入确定性的五元组 Hash ECMP，
不实现独立路由协议，也不逐包喷洒。

- 证据：`5cfc844`、`cb78f54`、`7134596`

### 2026-07-26：完善可扩展传输模型

平台加入首跳线速 pacing、`fixed`/`size-aware` 分包、按字节配置的 ISL 队列，
并用不同大小 transfer 和混合大流量验证完整字节与 remainder 合同。

- 证据：`ab6f4db`、`1b7926e`、`dace3cb`、`7d692ab`、`62eaf3a`

### 2026-07-26：独立构建并冻结 N0

SatCompute 迁入独立的 `contrib/satcompute` 模块，日常构建不再依赖 ns-3
examples 或 tests；默认输入为覆盖 0–110 秒、每 10 秒一个快照的 xw 66 星。

- 证据：`81d6eb2`、`d67ca0a`
- 阶段 tag：`n0-complete`，指向 `d67ca0a`

N0 不包含任务计算、Hypatia、故障恢复、地面站、cluster、CSV 拓扑或自定义
簇内/簇间路由。

## N1：最小任务计算闭环

### 2026-07-27：定义任务与计算输入

TaskTrace 和 ComputeProfile 使用 closed-world JSON 合同，明确任务到达、输入和
结果字节、计算工作量、入口节点、计算节点与结果节点。

- 证据：[PR #1](https://github.com/forest-rabbit/SatCompute/pull/1) / `34d76b9`

### 2026-07-27：完成任务执行闭环

每个任务依次完成 INPUT 传输、单服务台非抢占 FCFS 计算和 RESULT 传输；指标
记录任务完成率、端到端时延、计算排队与服务时间，并区分 `COMPLETE` 和
`PARTIAL`。

- 证据：[PR #1](https://github.com/forest-rabbit/SatCompute/pull/1) / `34d76b9`

### 2026-07-28：完成 N1 压力矩阵

正式矩阵使用 66 星、66 个计算节点、2 Gbps ISL、固定单向 8 ms、
64 MB/有向设备队列和 1000 秒仿真：

| 工作负载 | 任务 | INPUT | 完成任务 | lost | 结论 |
| --- | ---: | ---: | ---: | ---: | --- |
| 50% | 1000 | 54.5 GB | 1000/1000 | 0 | `RUN_VALID / CONTINUE` |
| 75% | 1500 | 81.75 GB | 1493/1500 | 54 | `RUN_VALID / CONTINUE` |
| 109 GB | 2000 | 109 GB | 1992/2000 | 429 | `RUN_VALID / CONTINUE` |

三组结果都超过冻结的 90% 继续门槛。该实验验证平台能正确报告完成与未完成
任务，不要求压力状态下必须达到 100% 完成率。

- 证据：[PR #2](https://github.com/forest-rabbit/SatCompute/pull/2) / `32d7037`
- 报告：[n1-6-stress-validation-review.md](https://github.com/forest-rabbit/SCP-SatComPlate/blob/legacy/ns-3.33/docs/reviews/n1-6-stress-validation-review.md)

### 2026-07-28：冻结任务闭环子里程碑

审查确认任务合同、计算语义、输出检查器和压力门槛后，创建
`n1-complete`。它表示任务闭环完成，但不是整个 N1 的最终结束点。

- 证据：[PR #3](https://github.com/forest-rabbit/SatCompute/pull/3) / `8d6d79f`
- 子里程碑 tag：`n1-complete`，指向 `8d6d79f`

## N1-ECMP：N1 最终收口

### 2026-07-28：冻结 N1 ECMP 运行合同

补齐 FlowMonitor DropReason 证据，并完成 Stable HRW 与 Size-aware HRW 在 N1
平台中的接入和验证。具体算法变化与专项结果统一记录在文末“ECMP 算法演进”。

- 证据：[PR #4](https://github.com/forest-rabbit/SatCompute/pull/4) /
  `e728b53`、[PR #5](https://github.com/forest-rabbit/SatCompute/pull/5) /
  `99ac19c`、[PR #6](https://github.com/forest-rabbit/SatCompute/pull/6) /
  `fd393bd`

### 2026-07-28：清理 Pre-N2 平台边界

Legacy CSV/TCP offered-load 正式退出 SatCompute；输入、失败诊断和 metrics
职责重新整理。未使用的 ns-3 上游内容继续保留，日常构建仍通过
`--enable-modules=satcompute` 排除无关模块。

- 证据：[PR #7](https://github.com/forest-rabbit/SatCompute/pull/7) / `6d95056`

### 2026-07-28：拆分 CI 并收口引用

验证分成 Fast Smoke 与 Full Regression，随后冻结阶段文档并清理临时分支、
旧引用和中间标签。

- 证据：[PR #8](https://github.com/forest-rabbit/SatCompute/pull/8) /
  `35a6258`、[PR #9](https://github.com/forest-rabbit/SatCompute/pull/9) /
  `f9732aa`、[PR #10](https://github.com/forest-rabbit/SatCompute/pull/10) /
  `a39f728`

### 2026-07-28：冻结拓扑结构并结束 N1

Topology 编排、snapshot 读取/调度和运行期 ISL state 完成分层，最终 tag
创建在拓扑收尾 merge commit 上。

- 证据：[PR #11](https://github.com/forest-rabbit/SatCompute/pull/11) / `58e031e`
- N1 最终 tag：`n1-ecmp-complete`，指向 `58e031e`

至此整个 N1 正式结束。后续 Capacity-aware 等算法迭代不会移动该 tag，也不会
改写 N1 的历史结论。

## N2A：动态星座场景

### 2026-07-29：引入 Hypatia 与星座合同

冻结 Python/uv 环境和窄化的 Hypatia 位置适配器，vendoring 最小轨道工具；
在此基础上生成 Walker 星座、plus-grid ISL 和 SatCompute 成对全量快照。

- 证据：[PR #13](https://github.com/forest-rabbit/SatCompute/pull/13) / `da275a9`、[PR #14](https://github.com/forest-rabbit/SatCompute/pull/14) / `f6dc13b`

### 2026-07-29：统一场景生成器

一份配置同时生成 static/dynamic topology、链路带宽与时延、计算节点放置和
`compute-profile.json`，替代一次性压力拓扑脚本。

- 证据：[PR #15](https://github.com/forest-rabbit/SatCompute/pull/15) / `539f35e`

### 2026-07-31：增加轨道可视化

增加可选交互式轨道查看、地球经纬网、ISL 显示和图片/GIF 导出；可视化
README 展示 66 星与 351 星在 300/600/900 秒的结果。

- 证据：[PR #17](https://github.com/forest-rabbit/SatCompute/pull/17) / `975f708`

### 2026-07-31：完成快照间隔实验

比较 66、351、720 星在 1/2/5/10/20 秒间隔下的拓扑和 ECMP。Python 在每个
参考秒检查所有有序源宿对的可达性、最短跳数和 ECMP 候选；C++ 再对确定性
probe pair 检查实际下一跳，同时统计快照数量与 topology-only 成本。

在 fixed-delay=8 ms、2 Gbps、plus-grid、`seam=false`、无权 hop-count
模型中，全部已测间隔通过，因此选择成本最低的 20 秒。66/351 星覆盖三个轨道
窗口；720 星只覆盖主窗口，所以不能外推到 20 秒以上、其他 ISL 规则或跨相位的
720 星场景。

- 证据：[PR #16](https://github.com/forest-rabbit/SatCompute/pull/16) / `cb0cd73`
- 报告：[N2 快照间隔实验](https://github.com/forest-rabbit/SCP-SatComPlate/blob/legacy/ns-3.33/docs/reviews/n2-snapshot-interval-study/REPORT.md)

PR #18 随后统一生成、分析、可视化测试及 fixture 的位置，N2A 的最终集成基线
为 [PR #18](https://github.com/forest-rabbit/SatCompute/pull/18) / `991211c`。

## N2B：代表性压力验证

N2B 使用 N2A 生成的 66/351/720 星场景验证同一份 75% 逻辑工作负载：1500 个
任务、81.75 GB INPUT、27.513 GB RESULT 和 6,584,966 个派生 UDP 数据报；
链路为 2 Gbps、固定单向 8 ms，快照间隔为 20 秒。

### 2026-08-02：完成三规模运行前门禁

66、351、720 星均通过 scenario checker、preflight 和跨越快照更新的
10-task smoke，确认输入、预计 packet-hop、仿真时长和输出合同有效。

- 证据：`c04f4d9`，随后由 `26edc33` 合入 N2 分支

### 2026-08-02：记录 Size-aware HRW 正式基线

正式运行先完成 66 星和 351 星；720 星当时按决定不启动正式运行。

| 规模 | 计算节点 | 仿真 | 完成任务 | lost | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| 66 | 66 | 1000 s | 1495/1500 | 174 | `PARTIAL / CONTINUE` |
| 351 | 117 | 600 s | 1489/1500 | 1139 | `PARTIAL / CONTINUE` |
| 720 | 240 | 300 s | 未运行 | — | 仅 preflight 与 smoke |

未完成任务直接对应 FqCoDel `QUEUE_DISC` Drop；没有计算合同错误、动态断链或
仿真提前终止。该结果把问题定位为网络竞争，但不改变压力实验允许部分完成的
90% 继续合同。

- 证据：`c04f4d9`、`26edc33`

### 2026-08-02 至 2026-08-03：完成 66 星 Capacity-aware 正式验证

完整路径容量准入的首轮 66 星正式运行完成 1500/1500 个任务并实现零丢包；
动态重准入和算法模块化之后又用同一 workload 复跑一次，结果仍为
1500/1500。

- 证据：`623a3e8`、`889c53e`
- 报告：[66 星 Capacity-aware 压力验证](https://github.com/forest-rabbit/SCP-SatComPlate/blob/legacy/ns-3.33/docs/reviews/n2-capacity-aware-hrw-66-pressure-validation.md)

### 2026-08-03：完成 Capacity-aware 三规模矩阵

在相同逻辑工作负载上完成最终复验：

| 规模 | 完成任务 | transfer | lost | mean/max 任务时延 | elapsed | peak RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 66 | 1500/1500 | 3000/3000 | 0 | 2.024577 / 11.216028 s | 57:38.30 | 117,948 KB |
| 351 | 1500/1500 | 3000/3000 | 0 | 2.141964 / 13.423848 s | 2:22:21 | 413,928 KB |
| 720 | 1500/1500 | 3000/3000 | 0 | 2.206884 / 11.348126 s | 3:50:57 | 1,169,020 KB |

三次运行均为 `COMPLETE`，所有派生数据报完整接收，已采集丢包类别为 0，结束
时 flow、路径、字节与速率预留账本全部归零。

- 代码集成：`69e845b`
- 结果收口：`db0ea2a`
- 完整报告：[N2 75% 压力验证](https://github.com/forest-rabbit/SCP-SatComPlate/blob/legacy/ns-3.33/docs/reviews/n2-pressure-75-validation.md)

N2B 计划内的三规模矩阵已经完成，没有遗漏的正式运行。结论仅适用于本次
单 seed、固定时延、无故障场景，不是普遍零丢包证明；多 seed、动态故障、
checkpoint、backup、recovery 和任务迁移属于后续阶段。

N2 收尾将 `global-capacity-aware-hrw` 设为默认路由模式；历史模式继续保留，
仍可通过 `--routingMode` 显式选择。

- 默认模式证据：`26cda9b`

## N3：迁移到 ns-3.48

### 2026-08-04：建立新仓库和双版本历史

新仓库以官方 ns-3.48 历史为主线，平台继续放在 `contrib/satcompute/`；旧版完整
历史保留为只读 `legacy/ns-3.33` 分支，两条主线不互相 merge。定向 CMake 构建
只启用 SatCompute 及其依赖，不启用 ns-3 上游 examples 和 tests。

- 证据：[PR #1](https://github.com/forest-rabbit/SCP-SatComPlate/pull/1) / `f711d6d9f`

### 2026-08-04 至 2026-08-05：迁移旧平台能力

在 ns-3.48 API 上恢复 topology、routing、traffic、task、metrics、tools、input
和 tests 分层，并迁移以下合同：

- 五种确定性 IPv4 路由：逐流 Hash ECMP、Stable HRW、Size-aware HRW、
  Capacity-aware HRW 和仅单下一跳的 Global First；
- 任务 INPUT 传输、非抢占 FCFS 计算与 RESULT 传输闭环；
- 网络、路由、任务、失败诊断指标，以及 unit、smoke、regression 门禁；
- `para.h/.cc` 的类型化默认参数、独立星座/算力/任务输入和中文模块文档。

迁移过程中曾用于逐项证明行为等价的 scenario、resolved/effective config、拓扑
replay 和独立 transfer 输入，均在长期接口确定后移除，不属于 N3 最终合同。

- 核心能力证据：[PR #8](https://github.com/forest-rabbit/SCP-SatComPlate/pull/8)–[PR #26](https://github.com/forest-rabbit/SCP-SatComPlate/pull/26)
- 旧版结构和合同核对：[PR #27](https://github.com/forest-rabbit/SCP-SatComPlate/pull/27)–[PR #62](https://github.com/forest-rabbit/SCP-SatComPlate/pull/62)

### 2026-08-05：收敛为 ns-3.48 原生在线拓扑

最终平台使用 ns-3.48 `LeoOrbitNodeHelper` 与
`LeoCircularOrbitMobilityModel` 实时计算卫星 ECEF 坐标，形成稳定行为：

- 星座 CSV 只描述原生圆轨道 shell，仿真、链路、路由和输出参数由
  `para.cc` 默认值及同名 CLI 管理；
- 同轨固定连接前后卫星，相邻轨道面在 `t=0` 选择总距离最小的循环一对一匹配，
  后续只更新距离和 active 状态，不更换异轨对端；
- fixed/distance 模式按输入周期更新链路；只有 active 边集合变化时才重算
  hop-based IPv4 路由；
- topology-only 与正式仿真使用同一轨道、候选链路、距离门控和时延规则，正式
  仿真在线计算而不回放 JSON 切片；
- topology-only 可先导出确定性的节点/链路切片，供后续故障生成和前端状态接口
  使用。

- 原生拓扑证据：[PR #63](https://github.com/forest-rabbit/SCP-SatComPlate/pull/63)–[PR #71](https://github.com/forest-rabbit/SCP-SatComPlate/pull/71) / `4403d82f9`

### 2026-08-05：完成 N3 收口与冻结

仓库完成中文总览和各模块 README、单一仓库级 `.gitignore`、统一
`third-party/`，并提供可直接运行的 100 秒、66 星、20 任务示例。最终验收覆盖：

- 定向配置与构建，且 `Examples: OFF`、`Tests: OFF`；
- 1 个 Python generator unit 和 7 个 C++ unit executable；
- 5 个 smoke、2 个 regression，以及 20 任务/40 transfer 完整闭环；
- Markdown 本地链接、shell 语法和正式 input JSON 语法。

- 集成证据：[PR #72](https://github.com/forest-rabbit/SCP-SatComPlate/pull/72) / `b83bf646b`
- 阶段 CI：[run 31004470287](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/31004470287)，56 秒内全部通过
- 阶段 tag：`n3-complete`，指向 `b83bf646b`

N3 完成的是平台迁移和当前基线，不包含故障 JSON 生成/执行、前后端状态接口、
IPv6、SRv6、地面站、馈电链路、SGP4/TLE 或非圆轨道模型。

## N4A：确定性故障输入与执行基础

N4A 只建立故障输入、执行和可验证终态，不生成随机故障原因，也不实现备份策略。
平台消费已经确定的 `compute`/`satellite` trace；`failure_probability` 是预警风险
元数据，不参与是否发生故障的再次抽样。

### 2026-08-05：加固输入并建立故障安全生命周期

在接入故障前，星座 CSV 改为严格六列 closed-world 解析，并依据 ns-3.48 原生圆轨道
地球半径与 80 km 最低射线高度校验 `maxIslDistance`。随后为任务、计算和网络传输
建立统一终止合同：

- transfer 明确区分 `COMPLETED`、`FAILED` 与 `CANCELLED`，首次终止原子取消 sender、
  receiver 回调、pending admission、完整路径、逐跳 assignment 和路由缓存；
- 重复终止幂等，不二次释放容量，迟到包只计 stale，不会复活终态 transfer；
- 任务增加不可恢复的 `FAILED` 终态，ComputeService 可精确移除 queued task 和取消
  running completion event。

- 输入加固证据：[PR #75](https://github.com/forest-rabbit/SCP-SatComPlate/pull/75) / `ca72c4a85`
- 生命周期证据：[PR #76](https://github.com/forest-rabbit/SCP-SatComPlate/pull/76) / `bd73633bb`

### 2026-08-05：完成确定性 trace 与 compute/整星执行

FaultTrace 使用稳定卫星 ID、整数纳秒、可选 notice/probability/duration，并拒绝同一
节点的重叠故障区间。同一时刻按 `NOTICE -> RECOVERY -> START`、再按 `fault_id`
批处理，得到唯一最终状态。

- compute 故障只关闭算力，不修改位置、ISL 或路由；尚未越过计算阶段的任务按当前
  状态失败，有限恢复只接纳后续任务；
- 整星故障在自然距离门控之后叠加通信可用性，在精确时刻关闭关联 ISL、立即重算
  IPv4 并推进 route epoch；一个故障批次最多重算一次；
- 整星作为 transfer 端点时触发 source/destination `FAILED`，未启动的后续 transfer
  进入 `CANCELLED`；只作为中间节点时，Capacity-aware 保留同一 transfer 并在新图
  中重准入；
- 有限整星恢复读取恢复时刻的原生 ECEF 位置，只启用仍满足距离门限的固定候选，
  不恢复旧失败任务。

- trace 证据：[PR #77](https://github.com/forest-rabbit/SCP-SatComPlate/pull/77) / `fe3a74745`
- compute 执行证据：[PR #78](https://github.com/forest-rabbit/SCP-SatComPlate/pull/78) / `0f760176a`
- 整星执行证据：[PR #79](https://github.com/forest-rabbit/SCP-SatComPlate/pull/79) / `f984880be`

### 2026-08-05：完成指标、回归与阶段冻结

正式故障运行新增 `fault-events.csv` 和 `fault-summary.json`；task/transfer summary
保留最终状态、原因和终止时间。只要全部 transfer 已进入终态，即使运行因预期故障
报告 `PARTIAL`，flow、assignment、容量路径、预留速率和 pending admission 也必须
归零。无 `faultTrace` 时不生成故障文件，复用目录会精确清除陈旧故障指标。

最终门禁包含 1 个 Python unit、11 个 C++ unit executable、5 个 smoke 和 3 个
regression。故障回归验证 compute 不重算路由、整星精确重算、概率原值、任务/传输
终态、容量账本归零、重复运行逐字节一致及无故障行为。

- 指标与回归证据：[PR #80](https://github.com/forest-rabbit/SCP-SatComPlate/pull/80)
- 阶段 CI：[SatCompute CI](https://github.com/forest-rabbit/SCP-SatComPlate/actions/workflows/phase_gate.yml)，`phase=n4a`
- 阶段 tag：`n4a-complete`，指向 PR #80 的合并提交

N4A 明确不包含故障原因/轨迹生成模型，也不包含 backup selection、主备切换、
checkpoint、迁移、recovery transfer、RTO/RPO 或 `SUPERSEDED` 运行状态。这些能力
不属于本阶段执行平台；其后续里程碑只在对应实现合入并通过验收后记录。

## N4B：统一故障建模与因果概率预测

N4B 在 N4A 的确定性执行基础上完成 F1/F2/F3 在线故障建模、Unified Fault Trace v2、
`none/generate/replay` 三种模式和任务完成前的因果故障概率预测。F1/F2 保持独立随机
来源，F3 表示永久整星故障；当前模型、事件与执行合同统一记录在
[故障模块说明](contrib/satcompute/fault/README.md)。本阶段不包含备份、checkpoint、
迁移或接管策略。

### 2026-08-31：完成 N4B 主体

F1/F2/F3、确定性 generate/replay、默认关闭的概率审计和 66 星、1000 秒、100 任务
联合场景均完成验收。F1/F2 参数与复现实验分别保存在标定文档中，联合场景 README
负责记录任务构成、运行参数和逐项断言。

- F1 标定：[N4B F1 参数标定](docs/calibration/n4b-f1/README.md)
- 联合场景：[N4B 100 任务联合验收](contrib/satcompute/input/examples/leo-66-1000s-n4b-joint/README.md)
- 集成证据：[PR #81](https://github.com/forest-rabbit/SCP-SatComPlate/pull/81) / `38025d964`
- 阶段 CI：[run 33353263977](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/33353263977)
- 阶段 tag：`n4b-complete`

### 2026-09-01：完成 F2 东西向非对称空间风险修订

F2 改用由实时卫星位置驱动、热点西短东长的空间风险场，并重新完成 66/351/720 星
窗口标定、66 星百万秒空间验证以及 F1/F2 联合预测和 generate/replay 回归。

当前 100 任务联合场景中，93 个任务完成、7 个按故障合同失败；发生 6 次可恢复
compute START、6 个 risk-only episode 和 1 次永久 F3 整星故障。200 个 transfer 中
192 个完成、8 个取消，只有 F3 引起一次路由重算，资源账本最终归零。

- 标定与论文图：[N4B F2 空间辐射风险标定](docs/calibration/n4b-f2/README.md)
- 集成证据：[PR #83](https://github.com/forest-rabbit/SCP-SatComPlate/pull/83) / `65bd39a1f`
- 阶段 CI：[run 33459723117](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/33459723117)，2 分 4 秒通过

## N5 前置：任务增量与压力基线

SCP-TaskModeling 已完成三类图像任务的真实增量计量、5% / 10% / 20% checkpoint
粒度与恢复验证。参考 `rho_variable` 分别约为 1.0000076294（稠密图像）、
0.001869064（稀疏推理）和 0.5424813080（压缩编码），固定头 `H` 单独计量。

2026-09-06，本平台完成 10 Gbps、66/351/720 星压力基线：每组 1500 个任务、
3000 个 transfer 全部完成，零丢包，预留归零，并可输出吞吐量、物理链路利用率和
容量等待指标。本轮未接入新任务增量模型，也不包含故障与备份。

- 任务证据：[TaskModeling PR #5](https://github.com/forest-rabbit/SCP-TaskModeling/pull/5) 合入后的 `0dbc0c7` 快照
- 压力证据：[PR #86](https://github.com/forest-rabbit/SCP-SatComPlate/pull/86) / `a9bf4ad16`；[阶段 CI](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/34014479346) 通过
- 参数口径、实验边界及待确定项：[N5 前置基础](docs/n5-prerequisites.md)；详细压力结果：[10 Gbps 基线](docs/pressure-10g-baseline.md)

以上为当时的前置实验记录；后续WU映射、sigma/H、LLM预算与平台接入已在
[N4C](docs/n4c/README.md)完成，真实备份执行仍未实现。

## ECMP 算法演进

N0 的 Hash ECMP 属于平台底座，已记录在 N0。本节只记录其后的算法演进与专项
fixture，不重复 N1/N2B 的阶段压力矩阵。

### 2026-07-28：Stable HRW

`global-hrw-per-flow` 使用 canonical flow identity 和 Rendezvous Hash 对
ns-3 给出的等价最短下一跳排序；候选不变时保持确定性，候选增删时实现最小
迁移。

- 证据：[PR #5](https://github.com/forest-rabbit/SatCompute/pull/5) / `99ac19c`

### 2026-07-28：Size-aware HRW

`global-size-aware-hrw` 在 HRW 前两名之间比较活动 transfer 的声明字节预留，
并维护 sticky assignment。N1 的同一 75% workload 重放由 Hash 基线
1493/1500、lost=54 改善为 1500/1500、lost=0；该结果只证明冻结场景。

- 证据：[PR #6](https://github.com/forest-rabbit/SatCompute/pull/6) / `fd393bd`
- 报告：[Pre-N2 Size-aware HRW 验证](https://github.com/forest-rabbit/SCP-SatComPlate/blob/legacy/ns-3.33/docs/reviews/pre-n2-size-aware-hrw-validation.md)

### 2026-08-02：Capacity-aware HRW

`global-capacity-aware-hrw` 扩展为完整 ECMP 最短路径准入：优先选择剩余瓶颈
容量最大的路径，容量相同时使用 HRW 稳定排序，并按路径瓶颈速率 pacing。

在汇聚瓶颈 fixture 中，Size-aware 为 0/2 transfer、9,075 个 QueueDisc Drop；
Capacity-aware 两次重放均为 2/2、17,144/17,144 个包、零丢包。独立 diamond
同时使用两条空闲 ECMP 路径，排除了全局串行化。

- 证据：`3bf150f`、`83fc612`
- 报告：[Capacity-aware HRW 定向验证](https://github.com/forest-rabbit/SCP-SatComPlate/blob/legacy/ns-3.33/docs/reviews/n2-capacity-aware-hrw-targeted-validation.md)

### 2026-08-02：动态路径重准入

拓扑更新后，失效路径会暂停、原子释放并在当前 route epoch 重新准入；暂时
无路或无容量时等待后续事件。动态五节点 fixture 两次重放均为 4/4 transfer、
50/50 个包、零丢包，覆盖断链、恢复、在途包和同 epoch 重试。

- 证据：`0d3d542`、`2fb3256`
- 报告：[Capacity-aware 动态路由恢复验证](https://github.com/forest-rabbit/SCP-SatComPlate/blob/legacy/ns-3.33/docs/reviews/n2-capacity-aware-dynamic-route-recovery-validation.md)

### 2026-08-03：模块化并合入 N2

路由职责拆分到 `routing/algorithm/`、`common/`、`state/` 和 `ns3/`；算法层
不承担 sender、拓扑回调或 metrics 输出。Capacity-aware 与模块化结果随后合入
`feature/n2-integration`。

- 证据：`889c53e`、`69e845b`

所有模式仍以 ns-3 全局路由生成的最短路图为边界，不提供 KSP、非最短绕行、
可靠重传、max-min fairness 或故障后的任务迁移。N1 的最终 tag
`n1-ecmp-complete` 保持不变。

## 更新约定

阶段完成时补充日期、PR/tag/提交、阶段 CI、主要交付、实验结论和明确边界。
里程碑只记录已经合入对应主线且验收通过的事实；计划、设想和实验草稿不写成
已完成内容。大型输入与运行输出保存在 Git 忽略的输出目录、`/tmp` 或外部归档，
仓库只提交可复查的合同、来源与提交引用和紧凑结果。
