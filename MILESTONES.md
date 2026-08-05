# SCP-SatComPlate 里程碑

本文件参考 ns-3.33 分支的记录方式，按阶段保存已经完成的主要工作、验证结论和
明确边界，不作为逐日开发日志。每个阶段只引用可追溯的 PR、提交和阶段 CI；平台
当前使用方法仍以 [项目总览](README.md)、
[SatCompute 运行手册](contrib/satcompute/README.md)和各模块 README 为准。

早期阶段中出现、但被后续阶段替代的设计只代表当时的可运行基线，不再构成当前
接口。例如阶段 1 曾包含完整 scenario 和拓扑 replay，阶段 2 已将它们收敛为
`para.cc`/CLI、原生在线拓扑和 topology-only 切片。

## 阶段状态

| 阶段 | 状态 | 集成或冻结点 | 日期 |
|---|---|---|---|
| 阶段 1：ns-3.48 初始平台闭环 | 已完成 | PR #1–#26 / `1d8145a9a` | 2026-08-05 |
| 阶段 2：原生轨道与长期平台基线 | 已完成 | PR #27–#71 / `4403d82f9` | 2026-08-05 |
| 阶段 3：仓库、文档与完整示例收口 | 已完成 | [PR #72](https://github.com/forest-rabbit/SCP-SatComPlate/pull/72) / `b83bf646b` | 2026-08-05 |

## 阶段 1：ns-3.48 初始平台闭环

### 2026-08-04：建立新仓库和双主线

`main` 从官方 ns-3.48 历史开始，旧 SatCompute 作为只读
`legacy/ns-3.33` 分支保留，两条历史不互相 merge。项目代码放在
`contrib/satcompute/`，上游 `src/` 不承载平台代码；定向 CMake 构建只启用
SatCompute 及其依赖。

- 证据：[PR #1](https://github.com/forest-rabbit/SCP-SatComPlate/pull/1) /
  `f711d6d9f`

### 2026-08-04 至 2026-08-05：完成首个网络与任务闭环

在 ns-3.48 上依次建立静态/动态卫星拓扑、IPv4 地址与链路状态、五种确定性路由、
UDP 传输、任务输入传输、非抢占 FCFS 计算、结果传输和结构化指标。原生圆轨道、
在线拓扑更新、拓扑导出与 replay 也在这一阶段形成首个端到端版本。

阶段 1 已经能够验证：

- 固定输入下的逐流 hash、HRW、size-aware 和 capacity-aware 选择可以复现；
- 任务按照 INPUT、COMPUTE、RESULT 顺序闭环，并显式使用 `output_bytes`；
- fixed/distance 时延按配置周期更新，活动链路集合变化时重算路由；
- 项目自有 unit、smoke 和 regression 不依赖 ns-3 上游 examples/tests。

- 路由闭环证据：[PR #8](https://github.com/forest-rabbit/SCP-SatComPlate/pull/8)–[PR #11](https://github.com/forest-rabbit/SCP-SatComPlate/pull/11)
- 任务闭环证据：[PR #12](https://github.com/forest-rabbit/SCP-SatComPlate/pull/12)–[PR #16](https://github.com/forest-rabbit/SCP-SatComPlate/pull/16)
- 在线拓扑证据：[PR #18](https://github.com/forest-rabbit/SCP-SatComPlate/pull/18)–[PR #23](https://github.com/forest-rabbit/SCP-SatComPlate/pull/23) / `7225140d3`

### 2026-08-05：冻结阶段 1 边界

仓库补齐 `contrib`/SatCompute 在编辑器中的可见性规则，并明确后续工作以
ns-3.33 的目录和行为合同为只读参考。第一次手动阶段门禁在 `1d8145a9a` 上通过。

- 收口证据：[PR #24](https://github.com/forest-rabbit/SCP-SatComPlate/pull/24)–[PR #26](https://github.com/forest-rabbit/SCP-SatComPlate/pull/26)
- 阶段 CI：[run 30967105240](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/30967105240)

阶段 1 证明核心功能能够在 ns-3.48 上闭环，但当时仍保留完整 scenario、
resolved/effective 配置、拓扑 replay 和独立传输输入。这些是阶段 1 的历史实现，
不是当前平台接口。

## 阶段 2：原生轨道与长期平台基线

### 2026-08-05：恢复与 ns-3.33 对应的代码外形

平台入口回到 `contrib/satcompute/satcompute.cc`，恢复轻量 `para.h/.cc`、中文说明
和 legacy 对应的 topology、routing、traffic、task、metrics、tools、input、tests
分层。随后逐项核对旧版公共入口、输入、指标和测试，并在 ns-3.48 API 上完成适配。

- 入口、参数与说明：[PR #27](https://github.com/forest-rabbit/SCP-SatComPlate/pull/27)–[PR #31](https://github.com/forest-rabbit/SCP-SatComPlate/pull/31)
- 拓扑、工具、业务与指标恢复：[PR #32](https://github.com/forest-rabbit/SCP-SatComPlate/pull/32)–[PR #62](https://github.com/forest-rabbit/SCP-SatComPlate/pull/62) / `62c1b868a`

这一过程的目标是先证明旧版能力没有在迁移中丢失，再决定哪些中间层适合成为长期
接口；因此阶段中间曾短暂恢复的文件不一定保留到阶段终点。

### 2026-08-05：收敛为原生在线卫星平台

长期基线最终采用 ns-3.48 `LeoOrbitNodeHelper` 与
`LeoCircularOrbitMobilityModel` 实时计算卫星 ECEF 坐标，并形成以下稳定合同：

- 星座只使用原生六列 CSV，仿真、链路、路由和输出参数由 `para.cc` 默认值及
  同名 CLI 管理；
- 同轨固定连接前后卫星，相邻轨道面在 `t=0` 选择总距离最小的循环一对一匹配，
  此后固定对端；
- 每个 tick 更新距离、active 状态和 distance 时延，只有 active 边集合改变时才
  重算 hop-based IPv4 路由；
- topology-only 与正式仿真共享轨道、候选链路、距离门控和时延计算，但正式仿真
  始终在线计算，不回放切片；
- 正式业务输入只保留 ComputeProfile 与 TaskTrace，UDP transfer 作为任务内部机制；
- 五种 IPv4 路由、任务闭环、指标和失败诊断继续保留。

- 原生星座与 topology-only：[PR #63](https://github.com/forest-rabbit/SCP-SatComPlate/pull/63)–[PR #70](https://github.com/forest-rabbit/SCP-SatComPlate/pull/70) / `351b9873f`
- 固定初始最近异轨匹配与默认参数：[PR #71](https://github.com/forest-rabbit/SCP-SatComPlate/pull/71) / `4403d82f9`
- 阶段 CI：[run 31000455676](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/31000455676)

阶段 2 是后续功能开发的代码基线。它不包含故障 JSON 生成与执行、前后端状态
接口、IPv6、SRv6、地面站、馈电链路或非圆轨道模型。

## 阶段 3：仓库、文档与完整示例收口

### 2026-08-05：建立当前中文文档体系

根目录总览和 SatCompute 运行手册重新按当前代码编写；topology、routing、task、
traffic、metrics、tools、input、tests 均拥有独立中文 README。模块文档直接列出
代码文件及职责，路由文档记录五种策略的核心公式、canonical tie-break 和状态
释放规则，指标文档说明每个 CSV/JSON 的出现条件和一致性约束。

当前行为只在总览、运行手册和代码旁的模块 README 中维护，不再保留一套会与实现
漂移的版本规格、迁移计划或审计清单。

### 2026-08-05：精简仓库并补齐完整示例

仓库级 `.gitignore` 成为唯一被跟踪的忽略文件；`.claude`、上游贡献模板和无关的
根目录元数据被移除，ns-3.48 作者、变更与发行资料归档到
`docs/upstream/ns-3.48/`。根目录 `third-party/` 继续作为统一第三方依赖位置。

新增的[100 秒、66 星、20 任务示例](contrib/satcompute/input/examples/leo-66-100s-20tasks/README.md)
复用正式星座和算力输入，TaskTrace 固定包含 20 个任务；完整 workload regression
验证 20 个任务和 40 个输入/结果 transfer 全部完成。示例同时给出同周期
topology-only 切片命令，作为未来故障建模和前端状态数据的准备入口。

### 2026-08-05：完成阶段 3 门禁

本地验收结果：

- 定向配置与构建通过，`Examples: OFF`、`Tests: OFF`；
- 1 个 Python generator unit、7 个 C++ unit executable 全部通过；
- 5 个 smoke 和 2 个 regression 全部通过；
- 20 份项目 Markdown 的本地链接全部有效；
- SatCompute shell 脚本与正式 input JSON 语法检查通过；
- 未运行 ns-3 上游 examples、全局 tests 或根目录 `test.py`。

- 集成证据：[PR #72](https://github.com/forest-rabbit/SCP-SatComPlate/pull/72) / `b83bf646b`
- 阶段 CI：[run 31004470287](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/31004470287)，56 秒内全部通过

阶段 3 只收口仓库可维护性、说明和可执行示例，没有改变五种路由算法、任务计算
语义或链路更新规则。故障、前端接口、IPv6 和 SRv6 仍属于后续阶段。

## 后续阶段边界

以下工作尚未计入已完成里程碑：

- 根据 topology-only 节点/链路切片生成确定性故障 JSON；
- 在精确仿真事件时刻应用卫星/链路故障并立即更新路由；
- 向后端输出稳定卫星 ID、ECEF `x/y/z` 和链路状态，再供前端可视化；
- IPv6 路由与后续 SRv6；
- 地面站、馈电链路、SGP4/TLE 和非圆轨道。

后续阶段不得仅因为已有拓扑切片就声称故障执行或前端接口已经完成。

## 更新约定

阶段完成时补充日期、PR、集成提交、阶段 CI、主要交付、验证结论和明确边界。
里程碑只记录已经合入 `main` 且验收通过的事实；计划、设想和实验草稿不写成已完成
内容。大型输入和运行输出保存在 `/tmp` 或外部归档，仓库只提交可复查的小型示例、
合同和必要结果摘要。
