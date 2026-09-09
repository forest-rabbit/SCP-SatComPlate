# 验证工具

本目录放显式运行的验证与离线分析，不是生产备份算法。正常仿真不自动调用这些脚本。

| 工具 | 用途 |
|---|---|
| `compfrr-shadow/` | G4只读旁路决策、虚拟账本与解析资源统计；[合同](compfrr-shadow/README.md) |
| `summarize-n4c-baseline.py` | 最终800任务none运行的deadline、任务/字节/WU/链路账本 |
| `summarize-pressure-baseline.py` | 通用10 Gbps无故障运行的吞吐量、链路时间积分、容量与等待分析；小型指标smoke仍复用 |
| `compare-fault-probabilities.py` | 同时刻/上下文的模型概率与预测概率一致性，拒绝缺失记录 |
| `check-flow-drop-reasons.py` | FlowMonitor DropReason证据核对 |
| `f1-calibration.cc` | 独立F1温度、恢复和概率标定 |
| `f2-exposure-calibration.cc` | 原生轨道F2加权暴露与期望故障数 |
| `f2-spatial-validation.cc` | F2长时空间事件与网格计数导出 |
| `run-f2-monte-carlo.py` | F2独立多次运行统计 |
| `plot-f2-spatial-validation.py` | 基于保留原始数据绘制F2风险/空间分布 |

历史workload候选比较、F3 victim搜索及G3候选绘图已收口到Git历史。
F1/F2标定和[论文图证据](../../../../docs/calibration/n4b-f2/README.md)不在此次候选清理范围。
命令与门禁见[测试说明](../../tests/README.md)。
