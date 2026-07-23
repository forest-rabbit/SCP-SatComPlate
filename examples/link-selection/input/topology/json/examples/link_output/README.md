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

## 节点和地面站推导

- 起始快照中的全部 `sat_id` 构成卫星集合。
- feeder 链路必须恰好有一个端点属于卫星集合。
- feeder 的另一个端点作为地面站节点编号。
- 程序不根据编号大小判断节点类型。
- 后续快照只能更新起始快照中已经存在的节点。

## 数据质量要求

- 每个文件必须是合法 JSON。
- 同一文件中 `sat_id` 不能重复。
- 链路端点不能相同。
- feeder 不能连接两个卫星或两个非卫星节点。
- 文件名时间不能重复。

生产解析器不会自动修补缺失逗号等 JSON 语法错误。
