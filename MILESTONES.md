# SatCompute 里程碑

本文件按阶段记录已经完成的主要工作、实验结论和边界，不作为逐日开发日志。
每个步骤给出完成日期及可追溯的 PR、tag 或代表性提交。阶段压力实验记录在实际
执行它的阶段；ECMP 专项 fixture 与算法演进统一记录在文末。

## 阶段状态

| 阶段 | 状态 | 集成或冻结点 | 日期 |
| --- | --- | --- | --- |
| N0：初始网络平台 | 已完成 | `n0-complete` / `d67ca0a` | 2026-07-26 |
| N1：最小任务计算闭环 | 子里程碑已完成 | `n1-complete` / `8d6d79f` | 2026-07-28 |
| N1-ECMP：N1 最终收口 | 已完成，整个 N1 在此结束 | `n1-ecmp-complete` / `58e031e` | 2026-07-28 |
| N2A：动态星座场景 | 已完成 | PR #13–#18 / `991211c` | 2026-07-31 |
| N2B：代表性压力验证 | 实现与实验已完成，待阶段审查 | `69e845b` / `db0ea2a` | 2026-08-03 |

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
- 报告：[`docs/reviews/n1-6-stress-validation-review.md`](docs/reviews/n1-6-stress-validation-review.md)

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
- 报告：[`docs/reviews/n2-snapshot-interval-study/REPORT.md`](docs/reviews/n2-snapshot-interval-study/REPORT.md)

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
- 报告：[`docs/reviews/n2-capacity-aware-hrw-66-pressure-validation.md`](docs/reviews/n2-capacity-aware-hrw-66-pressure-validation.md)

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
- 完整报告：[`docs/reviews/n2-pressure-75-validation.md`](docs/reviews/n2-pressure-75-validation.md)

N2B 计划内的三规模矩阵已经完成，没有遗漏的正式运行。结论仅适用于本次
单 seed、固定时延、无故障场景，不是普遍零丢包证明；多 seed、动态故障、
checkpoint、backup、recovery 和任务迁移属于后续阶段。

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
- 报告：[`docs/reviews/pre-n2-size-aware-hrw-validation.md`](docs/reviews/pre-n2-size-aware-hrw-validation.md)

### 2026-08-02：Capacity-aware HRW

`global-capacity-aware-hrw` 扩展为完整 ECMP 最短路径准入：优先选择剩余瓶颈
容量最大的路径，容量相同时使用 HRW 稳定排序，并按路径瓶颈速率 pacing。

在汇聚瓶颈 fixture 中，Size-aware 为 0/2 transfer、9,075 个 QueueDisc Drop；
Capacity-aware 两次重放均为 2/2、17,144/17,144 个包、零丢包。独立 diamond
同时使用两条空闲 ECMP 路径，排除了全局串行化。

- 证据：`3bf150f`、`83fc612`
- 报告：[`docs/reviews/n2-capacity-aware-hrw-targeted-validation.md`](docs/reviews/n2-capacity-aware-hrw-targeted-validation.md)

### 2026-08-02：动态路径重准入

拓扑更新后，失效路径会暂停、原子释放并在当前 route epoch 重新准入；暂时
无路或无容量时等待后续事件。动态五节点 fixture 两次重放均为 4/4 transfer、
50/50 个包、零丢包，覆盖断链、恢复、在途包和同 epoch 重试。

- 证据：`0d3d542`、`2fb3256`
- 报告：[`docs/reviews/n2-capacity-aware-dynamic-route-recovery-validation.md`](docs/reviews/n2-capacity-aware-dynamic-route-recovery-validation.md)

### 2026-08-03：模块化并合入 N2

路由职责拆分到 `routing/algorithm/`、`common/`、`state/` 和 `ns3/`；算法层
不承担 sender、拓扑回调或 metrics 输出。Capacity-aware 与模块化结果随后合入
`feature/n2-integration`。

- 证据：`889c53e`、`69e845b`

所有模式仍以 ns-3 全局路由生成的最短路图为边界，不提供 KSP、非最短绕行、
可靠重传、max-min fairness 或故障后的任务迁移。N1 的最终 tag
`n1-ecmp-complete` 保持不变。

## 更新约定

阶段完成时补充日期、PR/tag/提交、主要交付、实验结论和明确边界。大型输入与
运行输出只保存在 `/tmp`，仓库只提交可复查的合同、哈希和紧凑结果。
