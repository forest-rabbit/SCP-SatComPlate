# 任务内部传输

`traffic/` 只实现任务的输入传输和结果传输，不再提供独立 NetworkTransfer
workload 或 `--transferTrace` 输入。

`TaskCoordinator` 为每个任务构造两条确定性的 UDP 传输计划。传输引擎负责
稳定五元组、分包、发送 pacing、接收完成、size-aware 声明字节账本以及
capacity-aware 完整路径容量账本。结果传输的声明字节数直接取 TaskTrace 中的
`output_bytes`。

`fixed` 分包使用 `transferPayloadBytes`；`size-aware` 分包按传输大小使用
1024、8192 或 64000 字节的 payload。传输仍采用 UDP，不虚构 ACK、重传或可靠
恢复；未来故障导致的未完成任务由完成策略和失败诊断如实报告。
