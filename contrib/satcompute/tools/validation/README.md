# 本地输出检查

`check-flow-drop-reasons.py` 对失败诊断中的 FlowMonitor DropReason 证据做一致性
检查。它由 diagnostics smoke 和 workload regression 直接调用。

其余路由、任务、拓扑与确定性断言已经放回对应的 C++、smoke 或 regression
入口，不再维护一套与平台合同重复的大型离线检查器。
