# JsonTopo 甲方交付 TODO

本文件跟踪 `customer/jsontopo` 的下一次甲方交付。内部开发继续在
`develop/link-selection` 进行；本分支不合并内部结构重构。

## 1. 版本与分支

- [x] 在上一交付提交 `1d207ac` 创建 `customer-jsontopo-v1` 标签。
- [x] 将内部开发分支命名为 `develop/link-selection`。
- [x] 将甲方维护分支命名为 `customer/jsontopo`。
- [ ] 甲方切换到新分支名后，删除旧远程分支名。

## 2. 输入目录

- [x] 将 `Topodata/` 迁移到 `input/topology/`。
- [x] 将 `Trafficdata/` 迁移到 `input/traffic/`。
- [x] 更新 JsonTopo、CSV 拓扑和流量矩阵的运行时默认路径。
- [x] 提供最小的 `--trafficMatrix` 参数以选择匹配节点规模的流量矩阵。
- [x] 更新命令行帮助、README 和示例命令中的路径。
- [x] 确认不存在仍然生效的 `Topodata/` 或 `Trafficdata/` 引用。

## 3. JsonTopo 输出

- [x] 整理启动参数、拓扑初始化和时间片更新输出。
- [x] 区分 JsonTopo 状态、传统拓扑状态和聚类状态。
- [x] 使用外部 `node_id` 打印节点和链路，避免暴露内部下标。
- [x] 默认关闭非交付必需的调试文件输出。

## 4. Feeder Hold Time

- [x] 仅对带正数 `hold_time` 的 `feeder` 链路启动计时。
- [x] 新增、恢复或更新 hold time 时正确重新调度。
- [x] 链路被时间片删除或覆盖时，使旧定时事件失效。
- [x] 到期后断开链路并重新计算 OSPF 路由。
- [x] 合并同一仿真时刻到期链路的日志输出。
- [x] 更新字段说明和 snapshot/patch 示例。

## 5. 甲方输入示例

- [x] 纳入 `customer-73sat-6gs` JsonTopo 示例。
- [x] 纳入与 73 节点示例匹配的流量矩阵。
- [x] 保留最小 snapshot 和 patch 示例。
- [x] 确认示例数据不会被默认目录扫描误加载。

## 6. 验证与交付

- [x] `./waf build` 编译通过。
- [x] 上一版 JsonTopo OSPF 用例无回归。
- [x] 73 节点甲方示例使用显式 input 路径运行通过。
- [x] snapshot 和 patch 更新模式运行通过。
- [x] hold time 到期、取消和重新建立场景运行通过。
- [x] 工作区不包含待提交的构建产物、跟踪文件或仿真输出。
- [ ] 每项改动以独立、可回退的提交保存。
- [ ] 创建并推送 `customer-jsontopo-v2` 标签和交付说明。

## 不在本次范围内

- `config/`、`routing/`、`clustering/`、`traffic/`、`metrics/` 的目录重构。
- 内部 ECMP 和自定义聚类路由重构。
- `src/internet` 中仅服务于内部聚类路由的修改。
- 将 `develop/link-selection` 整体合并到甲方分支。
