# SatCompute ns-3.48 迁移状态

状态：v0.4 已作为 `main` 的长期开发基线。本文只描述当前实现；v0.2/v0.3
迁移资料保留在 `docs/specs/` 和 `docs/plans/` 中作为历史审计记录。

## 仓库基线

- `main` 基于官方 ns-3.48，是当前开发主线；
- `legacy/ns-3.33` 只读保留旧实现，不与 `main` 合并；
- 项目代码位于 `contrib/satcompute/`，不修改上游 `src/`；
- nlohmann/json 统一位于根目录 `third-party/`；
- `.vscode/` 不受 Git 跟踪；
- 日常配置不启用 ns-3 全局 examples/tests，也不运行 `test.py`。

## 当前运行模型

平台只有一个 `satcompute` 入口和一个 `SatComputeConfig` 参数对象。`para.cc`
仅定义带中文解释的默认值；命令行注册与校验位于入口。仿真时长、网络更新间隔和
拓扑切片间隔以秒输入，在 C++ 使用边界转换为 ns-3 `Time` 或整数纳秒。

星座输入采用 ns-3.48 `LeoOrbitalShell` 六列 CSV。卫星由
`LeoOrbitNodeHelper` 创建并使用 `LeoCircularOrbitMobilityModel` 实时计算位置。
稳定卫星 ID 按 plane-major 顺序映射，不直接暴露 `Node::GetId()`。

同轨链路固定连接环形前后邻居；相邻轨道面在 `t=0` 选择总距离最小的循环一对一
匹配，此后固定卫星 ID 对。每个网络更新时间点只根据当前坐标、最大距离和时延
模式刷新候选状态：

- `fixed` 直接使用配置的固定时延；
- `distance` 使用当前距离计算传播时延；
- 活动链路集合变化时重算 IPv4 路由；
- 只有距离时延变化而链路集合不变时，不重复计算 hop-based 路由。

## 两阶段工作流

拓扑预处理使用 `--topologyOnly=1`。它按配置的切片间隔推进同一套 ns-3.48
轨道模型，输出 `nodes_<time>.json` 与 `links_<time>.json`，但不安装协议栈、
NetDevice、路由、FlowMonitor 或任务应用。节点切片包含稳定 ID 和 ECEF `x/y/z`；
链路切片包含全部固定候选的 active、distance、delay 与 bandwidth 状态。

未来的故障生成器将读取这些切片并产生故障 JSON。正式仿真不会回放切片，而是用
同一个星座和参数在线生成相同的确定性拓扑，再在故障发生的精确仿真时刻应用覆盖。
故障生成与执行尚未在 v0.4 实现。

## 任务与路由

正式业务输入只有 TaskTrace 与 ComputeProfile。每个任务明确给出
`input_bytes`、`compute_work_units` 和 `output_bytes`；计算结束后的结果传输大小
严格取 `output_bytes`。独立 NetworkTransfer workload 已删除，`traffic/` 中的
UDP 传输引擎仅作为任务输入和结果传输的内部机制。

当前保留五种确定性的 IPv4 路由模式：

- `global-first`；
- `global-hash-per-flow`；
- `global-hrw-per-flow`；
- `global-size-aware-hrw`；
- `global-capacity-aware-hrw`。

固定星座、参数、任务、seed/run 与 canonical 同时事件顺序时，所有模式均可复现。
IPv6 与 SRv6 延期到独立阶段，不在本次迁移中预建半成品接口。

## 已移除的迁移层

当前平台不再提供完整 scenario JSON、resolved/effective config、软件/格式版本字段、
SHA-256 manifest、拓扑 replay、独立 scenario/topology/transfer 生成程序或
`transferTrace` 命令行入口。星座结构位于 CSV，算力与任务分别位于 JSON，运行参数
统一由 `para.cc` 默认值和命令行覆盖。

## 验证入口

仓库保留一个 Python 任务生成器测试、7 个聚焦 C++ 可执行测试、5 个 smoke 和
2 个 regression：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

GitHub 只保留手动触发的 `SatCompute CI` 阶段门禁。它构建 SatCompute 并运行上述
项目测试，不运行上游 examples/tests。

## 延期范围

- 基于拓扑切片生成并执行卫星/链路故障；
- 后端到前端的卫星状态输出接口；
- IPv6、SRv6；
- 地面站、馈电链路及非圆轨道模型。

当前合同与完成标准见
[v0.4 规格](specs/platform-v0.4.md)和
[v0.4 实施计划](plans/platform-v0.4-simplification.md)。
