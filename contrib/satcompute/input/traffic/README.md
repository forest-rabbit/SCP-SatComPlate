# 业务流量输入

本目录只保存 SatCompute 当前支持的 JSON 业务输入：

```text
json/workload/  NetworkTransfer 正式和本地压力输入
json/test/      NetworkTransfer 小型回归输入
json/task/      TaskTrace 任务到达输入
```

JSON 每条记录只描述 transfer ID、源卫星、目的卫星、应用字节数和到达时间。
分包模式、MTU 和 UDP 端口由运行参数及程序确定性派生。NetworkTransfer
不设置人工应用发送速率；每包按当前选定首跳的链路序列化时间调度。

旧版 traffic-matrix 倍率、TCP OnOff 和 legacy UDP 聚合能力已作为批准的
范围收缩退出项目，不迁移到 JSON。这里的 CSV 退出只涉及 SatCompute 自有
业务输入，不影响 ns-3 上游 CSV helper，也不改变 JSON 拓扑快照格式。
