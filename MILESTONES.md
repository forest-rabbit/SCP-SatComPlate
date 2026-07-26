# SatCompute 里程碑

本文件只记录已经冻结或正式启动的阶段成果，不作为逐日开发日志。每个完成阶段都应
给出明确的提交、Git 标记、验证证据和功能边界。

## 阶段状态

| 阶段 | 状态 | 冻结点 | 日期 |
| --- | --- | --- | --- |
| N0：初始网络平台 | 已完成 | `n0-complete` / `d67ca0a` | 2026-07-26 |
| N1：最小任务计算闭环 | 计划中 | 尚未冻结 | — |

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
SDN/OpenFlow。CSV 背景流量仅作为临时兼容输入保留。

### 后续关注项

- 只有外部程序或其他 ns-3 模块需要复用时，才选择性扩大 SatCompute 公开 API。
- 若重构 route epoch 或 flow cache，应补充同一五元组跨 epoch 的专门测试。
- 只有修改 ns-3 核心模块时，才在 CI 中重新启用对应的上游测试套件。
- 完整 1 GiB 压力场景继续作为本地测试，不进入常规 CI。

## N1：最小任务计算闭环

N1 将从 `n0-complete` 的稳定网络底座出发。其目标是建立最小、可验证的任务计算
闭环；具体输入合同、计算语义和验收标准应在开发前单独确定。故障、checkpoint、
backup 和恢复仍不自动进入 N1 范围。

## 更新约定

阶段完成时更新本文件，并至少记录：

1. 阶段状态与完成日期；
2. 冻结提交和 annotated tag；
3. 审查结论与可复查的 CI 证据；
4. 本阶段交付内容、明确边界和延期事项。
