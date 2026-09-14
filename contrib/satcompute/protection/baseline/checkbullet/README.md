# CB-Sat canonical 实现

本目录拥有独立 CB-Sat C++ 实现，不继承 CompFRR Frequency 或双层 local tail。

| 文件 | 职责 |
| --- | --- |
| `cb-sat-policy.*` | 独立 MTBF 驱动的 H/X 决策 |
| `cb-sat-state.*` | INPUT、root、连续 log 与严格故障 cutoff |
| `cb-sat-manager.*` | 真实生成、传输、融合与对象生命周期 |
| `cb-sat-recovery.*` | 主 baseline 重算及显式扩展 relocation |
| `cb-sat-controller.*` | 平台任务/故障接线 |
| `cb-sat-config.*` | 读取冻结 profile，不从本次未来故障反推 MTBF |
| `cb-sat-metrics.cc` | CB 独立事件、状态与 planned/actual 账本 |

参数来源、冻结 MTBF、工具入口与历史实验身份继续保留在
[原 CB-Sat 手册](../../policy/baseline/checkbullet/README.md)，避免破坏 profile 的路径和溯源。
该旧目录的导出头仅转发到本目录，不存在两套运行时。
公共能力边界见[架构](../../../../../docs/protection/architecture.md)，项目统一测试见
[tests](../../../tests/README.md)。本次整理不重新标定或运行正式性能矩阵。
