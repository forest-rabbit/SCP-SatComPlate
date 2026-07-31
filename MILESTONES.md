# SatCompute 里程碑

本文件只记录已经冻结或正式启动的阶段成果，不作为逐日开发日志。每个完成阶段都应
给出明确的提交、Git 标记、验证证据和功能边界。

## 阶段状态

| 阶段 | 状态 | 冻结点 | 日期 |
| --- | --- | --- | --- |
| N0：初始网络平台 | 已完成 | `n0-complete` / `d67ca0a` | 2026-07-26 |
| N1：最小任务计算闭环 | 已完成 | `n1-complete` | 2026-07-28 |
| N1 ECMP：路由与平台收尾 | 已完成 | `n1-ecmp-complete` | 2026-07-28 |
| N2A：动态星座场景与快照间隔审计 | 已完成 | PR #16 / `cb0cd733` | 2026-07-31 |

## N0：初始网络平台

N0 建立了后续星上计算实验所需的网络底座：

- SatCompute 作为独立的 `contrib/satcompute` 模块构建，不依赖 ns-3 examples
  或 tests。
- 仿真只创建卫星节点和星间链路，使用节点、链路分离的 JSON 全量快照。
- 默认输入为 xw 66 星，覆盖 0–110 秒，每 10 秒一个快照。
- 路由表和最短路继续由 ns-3 `GlobalRouteManager` 生成。
- 在原生全局路由结果上实现确定性的五元组逐流 ECMP，不进行逐包喷洒。
- NetworkTransfer JSON 描述逻辑传输，平台负责分包、首跳线速 pacing 和 UDP
  收发。
- 支持 `fixed` 与 `size-aware` 两种分包模式，以及按字节配置的 ISL DropTail
  队列。
- 提供逐流指标、传输摘要和每个 route epoch 的 ECMP 选路证据。

### 验证与冻结

- Git 标记：`n0-complete`
- 冻结提交：`d67ca0a164db5b1eacca04d4fcca3a1a295ca8cf`
- 最终审查：`APPROVE WITH NON-BLOCKING NOTES`
- GitHub Actions：
  [SatCompute deterministic smoke #30186315524](https://github.com/forest-rabbit/SatCompute/actions/runs/30186315524)
- CI 结论：`status=completed`，`conclusion=success`

CI 覆盖全新构建、静态与动态 Diamond、canonical endpoint、remainder、5000 条
不同大小的传输，以及至少 10 条混合大流量传输。

### N0 边界

N0 不包含任务计算、计算服务时间、任务调度、故障、checkpoint、backup 或恢复。
平台也不包含地面站、星地链路、cluster、CSV 拓扑构建、簇内/簇间路由和
SDN/OpenFlow。CSV 背景流量在 N0 冻结时仅作为临时兼容输入保留，后于
Pre-N2 清理中经批准退出项目范围，且没有迁移到 JSON。

### 后续关注项

- 只有外部程序或其他 ns-3 模块需要复用时，才选择性扩大 SatCompute 公开 API。
- 若重构 route epoch 或 flow cache，应补充同一五元组跨 epoch 的专门测试。
- 只有修改 ns-3 核心模块时，才在 CI 中重新启用对应的上游测试套件。
- 完整 1 GiB 压力场景继续作为本地测试，不进入常规 CI。

## N1：最小任务计算闭环

N1 已完成最小任务计算闭环和正式压力矩阵的实现验证：

- TaskTrace 与 ComputeProfile 使用 closed-world JSON 合同；
- 每个任务依次完成输入传输、单服务台非抢占 FCFS、计算和结果传输；
- `strict` 与 `report` 策略只改变未全部完成时的退出码，不改变任务执行语义；
- metrics 可区分 `COMPLETE` 与 `PARTIAL`，并枚举全部未完成任务和传输；
- 外部检查器验证状态前缀、时间戳、字节、包、端口、FCFS、FlowMonitor 和聚合
  指标的一致性。

PR1 已通过 CI、标记 `n1-pr1-final` 并合并为
`34d76b97ad2e0027c882452bcc2f641758a1522e`。PR2 在 66 星、66 个计算节点、
2 Gbit/s ISL、64 MB/有向设备队列、1000 s 仿真下完成 50%、75% 和 109 GB
压力矩阵；三者均为 `RUN_VALID`，任务完成率分别为 100%、99.5333% 和
99.6%，均达到冻结的 90% 门槛。

`n1-pr2-review-final` 冻结了 PR2 的最终审查候选。PR2 已于 2026-07-28
合并为 `32d70374cf0f93396845e5f067fb9beb560fc6d9`，N1 随后以 annotated tag
`n1-complete` 正式关闭。大型输入和运行输出只保留在 `/tmp`，仓库只保存
小型 66 节点计算配置与 `docs/reviews/n1-6-stress-validation-review.md` 中的哈希
和结果。

### 关键压力测试时间线

| 日期 | 场景与配置 | 路由阶段 | 结果 |
| --- | --- | --- | --- |
| 2026-07-27 | 100%：2000 个任务、109 GB INPUT；`transferChunkMode=size-aware` | `global-hash-per-flow`，当时尚未实现声明字节预留的大小感知路由 | 完成 1992/2000（99.6%），FlowMonitor lost=429 |
| 2026-07-28 | 75%：1500 个任务、81.75 GB INPUT；`transferChunkMode=size-aware` | 冻结旧 Hash 基线为 1493/1500、lost=54；随后使用 `global-size-aware-hrw` | 完成 1500/1500（100%），FlowMonitor lost=0 |

两次测试都使用 size-aware 分包；只有 2026-07-28 的后一次运行使用
`global-size-aware-hrw` 的声明字节预留选路。分包模式负责把逻辑 transfer
切成 UDP payload，路由模式负责选择等价物理下一跳，二者不是同一机制。

### 验证与冻结

- PR2：[#2 feat: close N1 with report-mode stress validation](https://github.com/forest-rabbit/SatCompute/pull/2)
- PR2 合并提交：`32d70374cf0f93396845e5f067fb9beb560fc6d9`
- Git 标记：`n1-complete`
- GitHub Actions：
  [SatCompute deterministic smoke #30275220956](https://github.com/forest-rabbit/SatCompute/actions/runs/30275220956)
- CI 结论：`status=completed`，`conclusion=success`
- 压力验收：三组正式场景均为 `RUN_VALID`，并达到 90% 继续门槛。

N1 不包含可靠重传、拥塞控制、竞争感知 pacing、故障、checkpoint、backup
或恢复；351/720 星及 Hypatia 动态拓扑属于后续独立阶段。

## N1 ECMP：路由与平台收尾

N1 ECMP 收尾在 N1 任务闭环之上完成了路由、输入、诊断和验证边界的正式冻结：

- 保留 `global-first`、`global-hash-per-flow`、`global-hrw-per-flow` 和
  `global-size-aware-hrw` 四种模式；四者都只在 ns-3
  `Ipv4GlobalRouting` 生成的等价下一跳中选择，不实现独立路由协议。
- 稳定 HRW 使用 canonical flow identity，确保同一流跨 route epoch 保持确定性；
  size-aware HRW 以逻辑 transfer 的声明字节维护逐候选路径预留，并在完成或失败
  时释放。
- N1 的 75% 压力重放验证了大小感知 HRW 可把旧 Hash 基线的
  1493/1500、lost=54 改善为 1500/1500、lost=0；该结果只证明这一固定场景，
  不外推为所有星座和负载下的完成率保证。
- SatCompute 输入扁平为 `topology/{examples,tests,resources}` 与
  `traffic/{workload,test,task}`，删除迁移兼容别名；拓扑快照仍严格使用成对的
  `nodes_<time>s.json` 和 `topology_<time>s.json`。
- Legacy CSV/TCP offered-load 运行模式已按批准的范围收缩退出；上游 ns-3
  内容继续保留，并通过 `--enable-modules=satcompute` 从日常构建中排除。
- 失败诊断按 task/transfer-only 合同拆分，成功运行或
  `diagnosticMode=off` 不遗留旧的 `diagnostics/failure/`。
- CI 拆为 pull request 的 Fast Smoke 与 `main`/`workflow_dispatch` 的 Full
  Regression；Full 顺序复用全部 Fast 脚本，再执行规模、顺序、generator 和
  preflight 回归。
- Topology 子系统结构冻结到 `contrib/satcompute/topology/`：
  `SatelliteTopology` 编排位于根层，JSON 快照类型、读取和目录调度位于
  `snapshot/`，运行期 ISL 状态位于 `link/`；旧根层 `topo.cc/.h` 与
  `jsontopo/` 路径退出。
- 任务、流量和拓扑共用的 nlohmann JSON 3.11.3 依赖迁入
  `contrib/satcompute/third-party/nlohmann/`，MIT 文件内容保持不变。

### 验证与冻结

- 功能冻结提交：`35a6258de3ac9438551e3dba254a4129089cf5d0`
- Git 引用、CI 与文档收尾提交：
  `a39f728b8e161e6c9f78ddaa1c8ab803017210c8`
- 最终 annotated tag：`n1-ecmp-complete`（指向拓扑收尾的最终 merge
  commit；精确 SHA 与 CI URL 记录在 tag message 和收尾 PR）
- 输入、文档与平台清理：
  [PR #7](https://github.com/forest-rabbit/SatCompute/pull/7)
- CI 分级：
  [PR #8](https://github.com/forest-rabbit/SatCompute/pull/8)
- PR #7 Fast Smoke：
  [run #30352000552](https://github.com/forest-rabbit/SatCompute/actions/runs/30352000552)，
  `conclusion=success`
- PR #8 Fast Smoke：
  [run #30353113556](https://github.com/forest-rabbit/SatCompute/actions/runs/30353113556)，
  `conclusion=success`
- PR #8 合并后的 Full Regression：
  [run #30353450349](https://github.com/forest-rabbit/SatCompute/actions/runs/30353450349)，
  `conclusion=success`
- 拓扑目录重组、确定性对照与临时标签替换：
  [`docs/reviews/n1-ecmp-topology-closeout.md`](docs/reviews/n1-ecmp-topology-closeout.md)

最终标签只在收尾提交的自动 Full Regression 和独立
`workflow_dispatch` Full Regression 都成功后创建。历史开发分支与中间标签的
逐项分类、替代证据和删除集合见
`docs/reviews/n1-ecmp-ref-cleanup-audit.md`；长期保留的远端分支只有 `main`，
阶段标签只有 `n0-complete`、`n1-complete` 和 `n1-ecmp-complete`。

N1 ECMP 不引入可靠传输、动态流量工程、任务迁移、故障恢复或 Hypatia；这些能力
必须作为后续阶段独立设计和验证。

## N2A：动态星座场景与快照间隔审计

N2A 在 `feature/n2-integration` 上完成动态星座生成工具链和快照间隔审计，
冻结点为 [PR #16](https://github.com/forest-rabbit/SatCompute/pull/16) 的 merge
commit `cb0cd7332cb48473ef1a388bee8d4b45b38c8da9`。这里的“已完成”不表示已经
合并到 `main`，也没有创建 N2 标签。

- [PR #14](https://github.com/forest-rabbit/SatCompute/pull/14) 引入窄化的
  Hypatia 轨道后端、66 星 Walker Star、plus-grid ISL，以及 static/dynamic
  canonical topology export。
- [PR #15](https://github.com/forest-rabbit/SatCompute/pull/15) 统一 scenario
  generator，支持 static/dynamic、fixed/distance delay、链路带宽配置、
  even-plane-slot 计算节点放置和 `compute-profile.json` 生成。
- [PR #16](https://github.com/forest-rabbit/SatCompute/pull/16) 增加 66/351/720
  场景、1/2/5/10/20 秒降采样、边状态与连通性审计、全量 Python ECMP 候选
  对照、三种模式的 C++ 实际下一跳 probe，以及 topology-only 成本测量。

### 快照间隔结论

- 66 星三个轨道窗口均为唯一边集合、0 次 transition 和唯一 ECMP 指纹；
  351 星三个轨道窗口得到相同结论。
- 720 星只完成 `offset=0` 主窗口，因此不能声称具备跨轨道相位稳健性。
- 全部 105 行结果通过冻结 gate：Python 在每个已测参考秒比较所有有序源宿
  节点对的可达性、最短跳数和 ECMP 候选下一跳集合；C++ 对每个星座 64 个
  确定性分层 probe pair，在 `global-first`、`global-hash-per-flow` 和
  `global-hrw-per-flow` 下比较实际选中下一跳。
- `global-size-aware-hrw` 依赖活动流声明字节预留状态，不属于这次纯拓扑审计。

当前模型统一建议 20 秒。20 秒只是 fixed-delay=8000 µs、2 Gbps、plus-grid、
`seam=false`、无权 hop-count 模型下最大已测试且成本最低的通过值；
`upper_bound_identified=false`，不能外推到 20 秒以上，也不能作为按距离、
视距、极区或天线规则动态断链模型的普适建议。

### 验证与边界

- 完整报告：
  [`docs/reviews/n2-snapshot-interval-study/REPORT.md`](docs/reviews/n2-snapshot-interval-study/REPORT.md)
- PR #16 final-head Full Regression：
  [run #30622204539](https://github.com/forest-rabbit/SatCompute/actions/runs/30622204539)，
  checkout `4759e956f6ca82fba4ede42585ae5ac9a0f4d448`，`conclusion=success`
- 合并后 Full Regression：
  [run #30622570535](https://github.com/forest-rabbit/SatCompute/actions/runs/30622570535)，
  checkout `cb0cd7332cb48473ef1a388bee8d4b45b38c8da9`，`conclusion=success`

N2A 不包含任务压力间隔研究、动态链路断开模型、ground station/GSL、故障、
checkpoint、backup、recovery、`main` 合并或 N2 标签。

## 更新约定

阶段完成时更新本文件，并至少记录：

1. 阶段状态与完成日期；
2. 冻结提交和 annotated tag；
3. 审查结论与可复查的 CI 证据；
4. 本阶段交付内容、明确边界和延期事项。
