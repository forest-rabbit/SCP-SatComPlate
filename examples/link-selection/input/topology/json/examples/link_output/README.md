# link_output 时间序列格式

本目录保存甲方按绝对时间输出的完整拓扑快照。文件名必须使用：

```text
YYYY-MM-DD_HH-MM-SS.json
```

例如：

```text
2024-01-02_00-00-00.json
2024-01-02_00-01-00.json
```

文件可约每分钟生成一次，也允许存在时间间隔。程序按文件名中的完整日期时间排序，
并根据运行参数选择起始快照和仿真时间窗口。

## 运行方式

在仓库根目录执行：

```bash
./waf --run "link-test \
  --routingMode=0 \
  --offeredload=0 \
  --linkOutputDir=examples/link-selection/input/topology/json/examples/link_output \
  --linkOutputStartTime=2024-01-02_00-02-00 \
  --simulationDuration=180 \
  --outputDir=/tmp/link-output-smoke"
```

规则：

- 起始时间必须精确对应一个文件，该文件映射为仿真 `0s`；
- 只选择 `[起始时间, 起始时间 + 仿真时长]` 闭区间内的快照；
- 支持跨日、不连续分钟和最多一天约 1440 个文件；
- 内存仅保存时间与路径，每个 JSON 到对应仿真时间才读取；
- `simulationDuration` 同时是 ns-3 的停止时间，必须为正数。

## 顶层数组

每个文件是一个 JSON 数组，可同时包含链路项和卫星簇项。

链路项：

```json
{
  "node1_id": 101,
  "node2_id": 102,
  "type": "sat",
  "delay": 32.648
}
```

卫星簇项：

```json
{
  "sat_id": 101,
  "clusterId": 0
}
```

## 字段与单位

```text
node1_id/node2_id  链路两端的外部节点编号
type               sat 或 feeder
delay              毫秒 ms，可带小数
hold_time          秒 s，仅 feeder 使用
sat_id             卫星外部节点编号
clusterId          卫星簇编号
```

现有 JsonTopo 支持的可选链路字段继续保留。甲方以后可在链路项中增加
`link_bandwidth`、`bandwidth_gbps`、`link_load_up` 或 `link_load_down`，
无需改变数组结构。

现有 `LinkInfo` 和 `TopologyNodeInfo` 的其他成员、默认值及 `has_*` 标记均继续保留，
不会因为当前样例字段少而删除。不同数据集可以在起始快照声明更多卫星，程序不会写死
24 星或 28 个总节点。

## 节点和地面站推导

- 起始快照中的全部 `sat_id` 构成卫星集合。
- feeder 链路必须恰好有一个端点属于卫星集合。
- feeder 的另一个端点作为地面站节点编号。
- 程序不根据编号大小判断节点类型。
- 后续快照必须包含与起始快照相同的卫星集合，只能更新其 `clusterId` 等属性。
- 后续 feeder 只能引用起始快照已经推导出的地面站；运行中不动态创建节点。
- 每个后续文件都是完整链路快照，未出现的上一时刻链路会被断开。

当前 8 个样例从 `00-00-00` 到 `00-07-00`，每分钟一个。起始样例可推导出
24 颗卫星、4 个地面站和 52 条链路；`00-05-00` 起卫星 602 的 `clusterId`
由 0 变为 1。

## 数据质量要求

- 每个文件必须是合法 JSON。
- 同一文件中 `sat_id` 不能重复。
- 链路端点不能相同。
- feeder 不能连接两个卫星或两个非卫星节点。
- 起始时间必须有精确匹配的文件。
- 日期和时间必须有效，不能用近似时间或非法日期代替。

生产解析器不会自动修补缺失逗号等 JSON 语法错误。
