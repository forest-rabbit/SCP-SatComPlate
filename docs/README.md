# 文档归档

项目当前行为直接记录在根目录总览、SatCompute 运行手册和各模块 README 中，避免
再维护一份容易与实现分离的阶段性规格。本目录保留已完成实验的记录，以及必要的
[ns-3.48 上游资料](upstream/ns-3.48/README.md)：作者名单、API 变更记录和发行
说明；它们不代表 SCP-SatComPlate 的贡献流程或发布记录。

当前项目文档入口：

- [项目总览](../README.md)：环境、构建、完整示例和文档导航；
- [项目里程碑](../MILESTONES.md)：已完成阶段、集成证据、验证结论和后续边界；
- [SatCompute 运行手册](../contrib/satcompute/README.md)：执行流程、全部参数和
  输入输出；
- [模块文档](../contrib/satcompute/)：拓扑、路由、任务、传输、指标、工具与测试；
- [N4B F1 标定](calibration/n4b-f1/README.md)：热模型与概率候选；
- [N4B F2 标定](calibration/n4b-f2/README.md)：东西向非对称空间风险、66/351/720 星
  加权暴露与真实平台 Monte Carlo；
- [10 Gbps 压力基线](pressure-10g-baseline.md)：66/351/720 星各 1500 任务的链路、
  吞吐量与运行成本证据，以及新旧输入的比较边界；
- [N5 前置基础](n5-prerequisites.md)：SCP-TaskModeling 三类任务的增量比例、固定头
  开销和测量边界，以及压力基线与后续备份算法之间尚需确定的内容。
