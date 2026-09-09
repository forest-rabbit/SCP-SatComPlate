# 文档归档

项目当前行为直接记录在根目录总览、SatCompute 运行手册和各模块 README 中，避免
再维护一份容易与实现分离的通用规格。本目录保留实验记录、当前 N4C 的分批审阅材料，以及必要的
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
  开销和测量边界，以及压力基线与后续备份算法之间尚需确定的内容；
- [N4C 工作量模型](n4c/workload-mapping.md)：已批准的 C800 映射、参数来源与字节口径；
- [G1 审阅结果](n4c/reviews/G1-workload-state-mapping.md)：候选比较、冻结决定及验证证据；
- [G2 审阅结果](n4c/reviews/G2-input-runtime-baseline.md)：C800 正式输入、deadline、在线风险和执行基线；
- [G3 正式冻结入口](n4c/reviews/G3-final-freeze.md)：后续实验默认场景、固定引用、必要证据和清理保护范围。

## N4C 阶段边界

G1/G2已批准，G3的当前v3场景已人工接受并以`n4c-g3-frozen`冻结在开发分支，尚未合入main。
后续实验默认采用G3冻结输入；G1的映射公式不变，81.75GB历史输入不再是默认实验场景。

| 审阅点 | 范围 |
|---|---|
| G1（已批准） | N4C-0/1：工作量、状态映射和离线预览 |
| G2（已批准） | N4C-2/3：正式输入、计算阶段 deadline、none 基线、生产 replay 清理与只读在线风险查询 |
| G3（PASS / FROZEN） | 地理负载热点、none对照、故障模型与正式固定压力realization，含最终证据核对和冻结收口 |
| G4（未开始） | CompFRR Shadow Decision Evaluation；不执行真实备份 |

兼容说明：旧任务书中“N4C G4收口”的场景冻结和文档收口由本次G3 Final Freeze接替，
旧阶段名称已废止，新的G4只指上述shadow评估。旧任务书不重写；PR合入与阶段CI仍须单独完成，
不能因G3冻结就将未执行的集成验收记为通过。

各批次本地验证、提交审阅后停止，不自动进入下一批。全部 N4C PR 合入 main 后才运行一次
阶段 CI；真实备份恢复、频率优化、节点选择分别留给 N5A/N5B/N5C。
TaskModeling只读，LLM不下载、不运行。G3固定seed1/run11结果不代表多seed均值；
历史清理另行审阅，保留正式场景生成入口及公共依赖、冻结输入和必要证据。
