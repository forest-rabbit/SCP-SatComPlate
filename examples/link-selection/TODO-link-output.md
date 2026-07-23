# Link Output JSON 适配规格与 TODO

状态：已实现并完成新旧格式、跨日窗口及 1440 文件扫描验证。

## 1. 目标

让 `link-test` 在保留现有 JsonTopo 输入协议的同时，原生读取甲方
`link_output/` 目录中的时间序列 JSON。

甲方格式的核心特征：

- 文件名为 `YYYY-MM-DD_HH-MM-SS.json`，目前约每分钟一个；
- 最长可能覆盖 24 小时，即约 `24 * 60 = 1440` 个快照；
- 单个顶层数组同时包含链路项和卫星簇项；
- 链路项当前只提供端点、类型、时延和可选 `hold_time`；
- 卫星簇项当前只提供 `sat_id` 和 `clusterId`；
- 地面站不单独声明，需要从 `feeder` 链路端点推导；
- 后续数据集可能扩大卫星规模，但仍使用相同格式。

成功标准是：程序不写死节点规模或文件数量，能够从用户选择的起始快照创建全部节点和
初始链路，并按文件名中的实际时间差依次应用窗口内的后续完整快照。

## 2. 已完成的样例分析

样例目录：

```text
examples/link-selection/input/topology/json/examples/link_output/
```

当前共有 8 个文件，时间从 `2024-01-02_00-00-00.json` 到
`2024-01-02_00-07-00.json`，间隔 60 秒。

每个快照包含：

- 24 个 `sat_id/clusterId` 卫星项；
- 48 条 `sat` 链路；
- 前 5 个快照有 4 条 `feeder` 链路，后 3 个快照有 2 条；
- 首个快照可推导出 24 颗卫星和 4 个地面站，共 28 个节点。

甲方已确认的单位：

- `delay` 按毫秒解释。星间链路典型值为 `32.648`，若按原协议的微秒解释，
  不符合该星间距离量级。
- `hold_time` 按秒解释，继续使用现有 JsonTopo 的 feeder 剩余保持时间语义。
- `clusterId` 是卫星簇编号。

数据质量问题：

- 8 个文件中有 5 个在最后一条链路项和第一条卫星项之间缺少逗号，不是合法 JSON；
- 样例目录旁有一个损坏的 `lib -> usr/lib` 符号链接，与拓扑数据无关；
- 新文件当前权限为可执行，纳入仓库前应规范为普通数据文件权限。

生产解析器不会猜测或修补非法 JSON。样例中的缺失逗号应做纯语法修复，同时应让
甲方修正生成器。

## 3. 兼容性和数据模型

现有结构体中的字段全部保留，包括当前样例尚未提供的：

```cpp
struct LinkInfo
{
  uint32_t source;
  uint32_t destination;
  std::string type;
  uint32_t delay_us;
  uint32_t link_bandwidth_kbps;
  uint32_t link_load_up_kbps;
  uint32_t link_load_down_kbps;
  // 其他现有字段继续保留
};
```

甲方以后在同一链路项中增加 `link_bandwidth`、`bandwidth_gbps`、
`link_load_up`、`link_load_down` 等现有字段时，继续走已有解析逻辑。
不因当前输入稀疏而删除结构体成员、默认值或 `has_*` 标记。

节点推导规则：

1. 用户选择的起始快照中所有 `sat_id` 的集合是卫星集合。
2. `feeder` 链路必须恰好有一个端点属于卫星集合。
3. feeder 的另一个端点作为地面站节点编号。
4. 不根据编号大小或固定范围判断地面站。
5. 卫星 `clusterId` 映射到现有 `cluster_id`，所有卫星参与分簇。
6. 地面站默认不参与分簇；OSPF 路径仍可正常使用其 feeder 链路。
7. 不允许在后续快照中新增首个快照未声明的卫星或地面站。

“卫星规模扩展”指不同数据集可在最早快照声明任意数量卫星。运行期动态创建
ns-3 节点不在本次范围内。

## 4. 时间序列和内存边界

新增显式命令行入口：

```text
--linkOutputDir=<directory>
--linkOutputStartTime=<YYYY-MM-DD_HH-MM-SS>
--simulationDuration=<seconds>
```

启用后：

1. 扫描目录中严格匹配 `YYYY-MM-DD_HH-MM-SS.json` 的文件；
2. 起始时间必须精确对应一个已有快照，该文件映射到仿真 `0s`；
3. 只选择绝对时间落在
   `[linkOutputStartTime, linkOutputStartTime + simulationDuration]` 内的快照；
4. 后续文件按相对起始时间的秒数调度，支持跨日和非连续时间；
5. 内存只保存时间戳和文件路径，不预加载 1440 个完整快照；
6. 到达时间片时才解析对应文件，并作为完整快照应用；
7. `simulationDuration` 必须为正数，同时作为 ns-3 停止时间。

复杂度目标：

- 目录扫描内存：`O(snapshot_count)`，每项仅包含时间和路径；
- 运行期解析峰值内存：`O(node_count + link_count)`；
- 不设置 24 星、28 节点或 1440 文件的硬编码上限。

该目标只约束输入读取。大规模 OSPF 重算、全连接业务应用和 FlowMonitor 的运行时间
仍取决于实际节点、链路和业务规模，不承诺 24 小时仿真能够实时完成。

## 5. 技术栈与项目位置

- ns-3.33、C++；
- JSON：现有 `nlohmann::json`；
- 构建：waf；
- 现有规范解析：`jsontopo/topo-json.{h,cc}`；
- 节点状态：`jsontopo/topo-node-state.cc`；
- 链路状态：`jsontopo/topo-link-state.cc`；
- 时间片调度：`jsontopo/topo-runtime.cc`；
- CLI 和仿真入口：`link-test.cc`；
- 样例与说明：`input/topology/json/`。

继续遵循现有 ns-3 风格：

```cpp
if (snapshotFiles.empty())
{
  NS_FATAL_ERROR("link_output目录中没有可用JSON快照");
}
```

两空格缩进、GNU 花括号，不引入新依赖。

## 6. 构建和验证命令

构建：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf build
```

样例 JSON 语法检查：

```bash
for file in examples/link-selection/input/topology/json/examples/link_output/*.json; do
  jq empty "$file"
done
```

新格式拓扑验证：

```bash
./waf --run "link-test \
  --linkOutputDir=examples/link-selection/input/topology/json/examples/link_output \
  --linkOutputStartTime=2024-01-02_00-02-00 \
  --simulationDuration=180 \
  --offeredload=0 \
  --outputDir=/tmp/link-output-smoke"
```

旧格式回归：

```bash
./waf --run "link-test \
  --offeredload=0 \
  --nodesJson=examples/link-selection/input/topology/json/examples/customer-73sat-6gs/nodes_0s.json \
  --topologyJson=examples/link-selection/input/topology/json/examples/customer-73sat-6gs/topology_0s.json \
  --trafficMatrix=examples/link-selection/input/traffic/traffic_matrix(73).csv \
  --outputDir=/tmp/jsontopo-regression"
```

## 7. 测试策略

每个实现任务完成后先构建，再提交和推送。最终至少验证：

- 8 个样例均为合法 JSON；
- 文件名解析得到 `0, 60, 120, ..., 420s`；
- 首个快照创建 24 颗卫星、4 个地面站和 52 条链路；
- 地面站由 feeder 端点推导，不依赖 `10000` 编号范围；
- `delay=32.648` 最终配置为 `32.648ms`；
- `hold_time=370065` 最终调度为 `370065s`；
- 300 秒快照识别到卫星 602 的 `clusterId` 从 0 变为 1；
- 后续 feeder 缺失时按完整快照关闭链路；
- 起始时间 `00-02-00`、仿真时长 `180s` 时，仅加载 `00-02-00`
  至 `00-05-00` 的快照；
- 原 `nodes_*.json/topology_*.json` 示例无回归；
- `offeredload=0` 的拓扑验证不读取或分配无用的大型业务矩阵；
- `git diff --check` 和构建均通过。

## 8. 边界

始终执行：

- 保留现有结构体字段和旧格式兼容性；
- 严格校验重复节点、未知端点、重复时间戳和非法 feeder；
- 从数据推导规模；
- 每个小步测试、提交并推送。

修改前需重新确认：

- 允许在运行中途新增节点；
- 接受非 JSON 的自动修补逻辑；
- 改变甲方字段的单位定义；
- 引入新的第三方依赖。

绝不执行：

- 根据节点编号范围判断卫星或地面站；
- 将卫星数量、地面站数量或快照数量写死；
- 删除当前样例未使用的结构体字段；
- 提交损坏的 `lib` 符号链接、构建产物或仿真输出。

## 9. 实现任务

### Task 1：规范样例数据（S）

- [x] 修复 5 个文件缺失的分隔逗号。
- [x] 将 JSON 文件权限规范为普通数据文件。
- [x] 增加新格式 README，记录字段、单位和地面站推导规则。
- 验证：8 个文件逐一通过 `jq empty`。
- 依赖：无。

### Task 2：支持新格式单位且保留旧字段（M）

- [x] 新格式 `delay` 转换为微秒保存，保留三位毫秒精度。
- [x] 新旧格式的 `hold_time` 均按秒解析。
- [x] 旧格式的 `delay` 微秒语义保持不变。
- 验证：构建通过，旧 73 星样例结果无回归。
- 依赖：Task 1。

### Task 3：解析混合数组并推导节点（M）

- [x] 区分链路项和 `sat_id/clusterId` 项。
- [x] 从起始快照建立卫星集合。
- [x] 从 feeder 的非卫星端点建立地面站集合。
- [x] 拒绝未知对象形状、重复卫星和非法 feeder。
- 验证：样例推导出 24 星、4 地面站、52 条链路。
- 依赖：Task 2。

### Task 4：扫描时间戳文件并按需加载（M）

- [x] 严格解析完整日期时间文件名。
- [x] 支持跨日、缺失分钟和重复时间检测。
- [x] 要求起始时间精确命中快照，并按仿真时长裁剪文件窗口。
- [x] 仅保存路径和相对时间，到点后读取快照。
- 验证：起始 `00-02-00`、时长 `180s` 时得到 3 个运行期事件，
  时间为 60、120、180 秒。
- 依赖：Task 3。

### Task 5：接入初始化、CLI 和运行时更新（M）

- [x] 新增 `--linkOutputDir`、`--linkOutputStartTime` 和
  `--simulationDuration`。
- [x] 新模式从起始快照创建节点和初始链路。
- [x] 后续快照更新卫星簇和完整链路状态。
- [x] 未启用新模式时保持现有入口行为。
- 验证：新格式端到端运行完成，旧格式回归通过。
- 依赖：Task 4。

### Task 6：收敛 24 小时拓扑验证的内存占用（M）

- [x] `offeredload=0` 时跳过流量矩阵读取和客户端数据分配。
- [x] 移除当前与 `totalTimeStep` 成比例但实际未使用的业务矩阵维度。
- [x] 使用 `simulationDuration` 控制应用和 ns-3 停止时间。
- 验证：1440 个文件的调度清单可建立，内存不随快照内容总量线性增长。
- 依赖：Task 5。

### Task 7：文档、全量回归与交付清单（S）

- [x] 更新主 README 和 JsonTopo 格式说明。
- [x] 运行新旧两套端到端命令。
- [x] 列出本次新增、修改和甲方需替换的文件。
- [x] 确认所有任务以独立提交推送。
- 依赖：Task 6。

## 10. 风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| 甲方生成器继续产生非法 JSON | 运行到中途失败 | 样例先校验，并要求生成端修复 |
| 新旧格式的 `delay` 单位不同 | 时延错三个数量级 | 仅在 link_output 模式将 `delay` 解释为毫秒 |
| 后续文件出现新节点 | ns-3 运行期无法安全创建完整协议栈 | 首快照声明全集，后续严格拒绝 |
| 1440 次 OSPF 重算成本高 | 24 小时仿真耗时长 | 读取层按需加载；路由性能另行压测 |
| 大规模全连接业务为 `O(N²)` | 内存和应用数量过高 | 拓扑验证用零负载；业务扩展另立任务 |

## 11. 已确认决策

- `delay` 的单位为毫秒；
- `hold_time` 的单位为秒；
- `clusterId` 是卫星簇编号；
- 甲方填写精确起始时间和正数仿真时长；
- 只读取指定绝对时间窗口内的快照；
- 非法 JSON 由数据生成端修复，程序不做文本级猜测修补。

## 12. 实施与验证结果

- 新格式样例：从 `2024-01-02_00-02-00` 开始运行 `180s`，识别 24 颗卫星、
  4 个地面站、52 条初始链路，并在 `60/120/180s` 应用三个更新；
- 簇变化：`180s` 对应 `00-05-00`，卫星 602 从簇 0 进入簇 1；
- feeder 变化：同一时间点断开旧 feeder 4 条并安装新 feeder 2 条；
- 跨日：`23:59` 起始时，次日 `00:00/00:01` 正确映射到 `60/120s`；
- 精确起点：不存在的 `00-02-30` 快照在初始化前被拒绝；
- 规模：临时目录中的 1440 个文件全部被发现，`1s` 窗口只选择首文件且不读取其余内容；
- 旧格式：73 星、6 地面站、164 条初始链路以及 `5/10s` 时间片回归通过；
- 非零负载：`offeredload=0.001` 回归得到 21362 个收发包、0 丢包和
  `2.2447Mbps` 吞吐量，与修改前确认结果一致；
- 构建与格式：`./waf build`、`git diff --check` 和 8 个样例的 `jq empty` 均通过。

甲方部署时需要同步整个 `examples/link-selection/` 中的源码和说明变更；如果只做最小
代码替换，至少需要更新 `link-test.cc`、`para.{h,cc}`、`topo.{h,cc}` 和
`jsontopo/topo-json.{h,cc}`、`jsontopo/topo-runtime.{h,cc}`，并放入合法的
`YYYY-MM-DD_HH-MM-SS.json` 数据目录。
