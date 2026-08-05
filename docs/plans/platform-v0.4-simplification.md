# 实施计划：SCP-SatComPlate v0.4 简化

## 概览

本计划把已完成的 v0.3 迁移基线收敛为长期开发平台。实现按照依赖顺序拆成可独立
回滚的 PR；每个 PR 做聚焦本地验证，全部合并后只运行一次阶段 CI。

## 架构决定

- ns-3.48 `LeoOrbitNodeHelper` 是唯一节点与轨道 mobility 创建入口；
- `satcompute` 的 topology-only 模式取代独立拓扑生成可执行程序；
- 正式仿真在线计算拓扑，切片只供未来离线故障生成使用；
- `SatComputeConfig` 是唯一运行参数对象，不再生成 resolved/effective 副本；
- 正式 workload 只有 TaskTrace + ComputeProfile，NetworkTransfer 是内部执行机制；
- 测试恢复为少量入口级回归，删除迁移过程门禁和重复细粒度可执行测试。

## 任务清单

### 阶段一：仓库与依赖卫生

#### 任务 1：固化 v0.4 规格

**验收：**

- `AGENTS.md` 指向 v0.4；
- 规格覆盖命令、结构、代码风格、测试、边界和可测试完成标准；
- 实施任务按依赖排序。

**验证：** `git diff --check`。

**依赖：** 无。

#### 任务 2：统一第三方目录并移除 GitHub IDE 文件

**验收：**

- nlohmann 只存在于根目录 `third-party/`；
- SatCompute 使用统一 include 路径且可构建；
- `.vscode` 不再被 Git 跟踪但根 `.gitignore` 继续忽略本地配置。

**验证：** SatCompute targeted build；JSON 解析 smoke。

**依赖：** 任务 1。

### 阶段二：原生星座和共享拓扑

#### 任务 3：切换原生 LeoOrbitalShell CSV

**验收：**

- 输入目录包含单 shell 六列 CSV；
- `LeoOrbitNodeHelper` 创建卫星和 mobility；
- 稳定外部 ID 仍为 plane-major，候选链路结果确定。

**验证：** 66 星快速测试和重复运行坐标比较。

**依赖：** 任务 2。

#### 任务 4：收敛拓扑配置接口

**验收：**

- topology policy/controller 只接收其实际需要的参数；
- fixed/distance 更新间隔来自 `para.cc`；
- 时延每 tick 刷新，链路集合不变时不重算路由。

**验证：** online topology focused regression。

**依赖：** 任务 3。

#### 任务 5：实现平台 topology-only 切片

**验收：**

- 同一个 `satcompute` 入口输出 `nodes_*` 和 `links_*`；
- node 含 XYZ；link 含所有候选、active、distance 和 delay；
- topology-only 不安装 InternetStack、NetDevice、routing、FlowMonitor 或 workload；
- 删除独立拓扑生成 executable、manifest、SHA 和 schema。

**验证：** 0/1/2 s 切片合同测试；相同运行输出逐字节一致。

**依赖：** 任务 4。

### 阶段三：运行配置与 workload 简化

#### 任务 6：恢复轻量 para/入口

**验收：**

- `para.cc` 只赋默认值并写中文解释；
- CLI、归一化、校验位于 `satcompute.cc`；
- 时间以秒输入，在使用边界转换；
- 删除无用参数和 `resolved-config.*`。

**验证：** CLI 错误用例和 targeted build。

**依赖：** 任务 5。

#### 任务 7：删除运行证据过度设计

**验收：**

- 删除 `effective-config.*`、`satcompute-version.*`、`sha256.*`；
- run summary 不再依赖 effective config、hash 或 schema/software version；
- 正式任务运行仍能完整写出指标。

**验证：** task smoke 和 metrics output checker。

**依赖：** 任务 6。

#### 任务 8：收敛业务输入

**验收：**

- 删除平台 `transferTrace` 和纯传输生成器/正式输入；
- TaskTrace/ComputeProfile 不含 `schema_version`，字段与范围校验仍存在；
- `output_bytes` 原样驱动结果传输；
- 删除 scenario 生成器，保留任务生成器并移除版本/hash 元数据。

**验证：** 单任务、FCFS、异构算力、结果大小和五种路由回归。

**依赖：** 任务 7。

### 阶段四：测试和文档收口

#### 任务 9：精简测试资产

**验收：**

- 保留 4 smoke + 2 regression + 原生 topology-only smoke；
- 删除迁移审计、CI 策略、schema/config/hash/version 和重复 C++ tests；
- 保留的脚本不引用已删除文件或纯传输正式输入。

**验证：** 所有保留的 unit/smoke/regression 本地通过。

**依赖：** 任务 8。

#### 任务 10：恢复中文 README 并审计目录

**验收：**

- README 以 legacy 写法说明执行模型、源码布局、参数、任务和测试；
- 文档不再出现被删除的入口或合同；
- CMake、tracked tree 和分支符合规格。

**验证：** `rg` 负向审计、`git diff --check`、完整本地门禁。

**依赖：** 任务 9。

### 阶段五：唯一 CI 与清理

#### 任务 11：运行阶段 CI

**验收：**

- 所有实现 PR 已合并到 `main`；
- 手动 `SatCompute CI` 成功且未执行上游 examples/tests；
- 所有临时本地/远端分支已删除；
- `main` 与 `origin/main` 同步且工作树干净。

**验证：** GitHub run 结论、分支列表和 `git status`。

**依赖：** 任务 10。

## 检查点

- 仓库检查点：任务 1-2 后 targeted build 通过；
- 拓扑检查点：任务 3-5 后 topology-only 端到端通过；
- 平台检查点：任务 6-8 后任务闭环和五种路由通过；
- 完成检查点：任务 9-11 后本地门禁与唯一 CI 通过。

## 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| native helper 的 CSV 顺序与旧 ID 不一致 | 候选和任务端点变化 | 明确单 shell、plane-major，并用 66 星黄金断言 |
| 删除 resolved config 导致组件参数遗漏 | 构建或运行错误 | 先收窄 topology 参数，再移动 CLI/校验 |
| 删除纯 transfer 模式降低路由覆盖 | 路由回归变弱 | 用 TaskTrace 的输入/结果流覆盖五种模式，保留必要内部 fixture |
| topology-only 意外创建网络对象 | 预处理变慢或语义污染 | 独立入口分支并以对象/输出 smoke 检查 |
| 测试精简误删行为门禁 | 回归未被发现 | 先建立入口级覆盖矩阵，再删除重复测试 |

## 开放问题

无。故障 JSON 字段、前端接口和 IPv6/SRv6 均明确延期到独立规格。
