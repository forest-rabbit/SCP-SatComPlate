# 结构化输出校验工具

本目录保留 ns-3.33 SatCompute 的结果检查器，用于对独立输入、路由选择、任务
闭环、容量账本和诊断证据进行确定性复核。检查器都是只读消费者，不启动仿真，
也不修改输出目录。

```text
check-ecmp-output.py             Hash/HRW、canonical ordering 与规模输出
check-size-aware-output.py       size-aware HRW 静态/动态合同
check-size-aware-replay.py       N1 size-aware 碰撞回放
check-capacity-aware-output.py   capacity-aware 准入、恢复与账本
check-task-output.py             任务、FCFS、算力与两段传输
check-flow-drop-reasons.py       FlowMonitor DropReason 诊断一致性
preflight-task-workload.py       大任务输入运行前预检
```

每个脚本使用 `--help` 查看所需输入。检查器要求被检查目录包含相应的结构化证据；
缺失文件或字段会直接返回非零，不会用默认值掩盖合同变化。当前迁移阶段先恢复
入口、参数和内部判定逻辑；分层 metrics 在阶段 6 恢复后，再把完整黄金场景接回
这些检查器。大型 workload 只在本地按需运行，不进入每次阶段 CI。

开发中的快速入口只检查脚本可编译且 CLI 可达：

```bash
python3 -m unittest \
  contrib.satcompute.tests.unit.test_validation_tools -v
```
