# 业务流量输入

CSV 仅用于业务需求，不参与拓扑构建。`traffic_matrix(66).csv` 从 xw 原始文件
`examples/link-selection/input/traffic/traffic_matrix(324).csv` 机械截取得到：

```bash
head -n 6600 traffic_matrix\(324\).csv | cut -d, -f1-66 \
  > traffic_matrix\(66\).csv
```

这一规则与 xw 已有 `traffic_matrix(73).csv` 的生成方式一致：原文件是
32400 行×324 列，66 星文件保留前 6600 行和每行前 66 列，最终尺寸是
6600 行×66 列。

N 必须等于卫星数。程序每 N 行读取一个时间片，共读取 100 个时间片，并按照
旧版 xw 逻辑将同一源节点的 100 行逐列累计为实际 N×N 发送矩阵。行列按外部
`sat_id` 数值升序，单元值和累计结果的单位均为 Gbps。实际发送速率为：

```text
accumulated_value × offeredLoad × 1e9 bps
```

当 `--offeredLoad=0` 时，程序不读取业务文件，也不创建客户端流。
