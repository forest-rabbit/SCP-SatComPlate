# 卫星 JSON 全量快照

## 文件命名与时间

每个文件名必须为：

```text
YYYY-MM-DD_HH-MM-SS.json
```

目录中最早的文件映射为仿真 `0s`，其他文件按与最早文件的真实时间差调度。
程序只选择 `simulationDuration` 闭区间内的文件。每个文件都是完整 ISL 快照：
上一快照存在但本快照缺失的链路会被关闭，再次出现时会恢复。

所有快照必须列出完全相同的卫星集合，运行期间不新增或删除卫星节点。

## 卫星记录

```json
{
  "sat_id": 101
}
```

`sat_id` 是非负且唯一的外部卫星 ID。

## 星间链路记录

```json
{
  "node1_id": 101,
  "node2_id": 102,
  "delay": 32.648,
  "bandwidth_bps": 10000000000
}
```

- `node1_id`、`node2_id`：卫星 ID，不能相同；
- `delay`：单向传播时延，单位 ms，支持小数；
- `bandwidth_bps`：可选，单位 bps；省略时使用 `--linkBandwidth`。

同一快照中不能重复声明同一无向 ISL。

## 完整文件

根节点是数组，卫星记录和 ISL 记录可混排：

```json
[
  {"node1_id": 101, "node2_id": 102, "delay": 10.5},
  {"sat_id": 101},
  {"sat_id": 102}
]
```

仓库样例位于 [`examples/link_output/`](examples/link_output/)。
