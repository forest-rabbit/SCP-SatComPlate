# 项目文档

当前行为由[运行手册](../contrib/satcompute/README.md)和各模块 README 维护；
本目录只保留最终实验依据及必要上游资料。

- [项目总览](../README.md)、[里程碑](../MILESTONES.md)。
- [N4C 最终合同](n4c/README.md)：正式800任务场景、G3/G4冻结与N5边界。
- [N5 前置测量](n5-prerequisites.md)：TaskModeling实测rho/H来源及历史压力依据。
- [Pre-N5C 基线](n5/reviews/Pre-N5C-placement-baselines-final.md)、[CB-Sat 验收](n5/reviews/Pre-N5C-cb-sat-v2.md)：冻结场景下的真实保护、恢复与成本比较。
- [10 Gbps 历史压力基线](pressure-10g-baseline.md)：66/351/720星测量结果。
- [F1 标定](calibration/n4b-f1/README.md)、[F2 标定及论文图](calibration/n4b-f2/README.md)。
- [ns-3.48 上游资料](upstream/ns-3.48/README.md)。

N4 已合入 main；N5A、N5B 及 Pre-N5C 基线已集成到 n5。
CB-Sat 当前为独立分支的本地验收，尚未合入 n5；本轮不发布或触发 GitHub CI。
旧候选及中间审阅记录通过Git历史查阅，不再作为当前接口来源。
