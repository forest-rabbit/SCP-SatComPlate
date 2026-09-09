# G4 最终冻结索引

**G4 STATUS = PASS / FROZEN；N5A STATUS = NOT STARTED。**
人工接受及冻结日期：2026-09-09。分支：`feature/n4c-g4-shadow-decision-evaluation`。

- 已审阅 HEAD：`8c3c27724a4723e9bba9abb2f99349566254af3d`。
- Cleanup/freeze commit：`065ce9636971acce1dc701f4b6eadafb8a9e90e5`，
  已通过 [PR #91](https://github.com/forest-rabbit/SCP-SatComPlate/pull/91) 普通合入 n4c。
- 当前 G3 参考为已接受的1 ms场景；原8 ms冻结提交见[G3索引](G3-final-freeze.md)。
- N4 最终发布后可删除内部冻结 tag，历史追溯使用 commit/PR，不依赖临时分支或标签。
  发布状态见[N4C索引](../README.md)；N5 不由本轮自动启动。

## 唯一正式输入与验证器

正式目录：[LEO-66 Final Experiment Scene](../../../contrib/satcompute/input/experiments/leo-66/)。
按 topology/compute/workload/placement/fault 自包含组织；
星座、TaskTrace、ComputeProfile 与各 manifest/summary 数值均保持原样，N4 收尾只迁移路径。
完整参数见[G3索引](G3-final-freeze.md)和[N4C合同](../README.md)。

- [最终生成器](../../../contrib/satcompute/tools/generation/generate-task-workload.py)：
  原生位置切片、workload_seed和placement_seed；唯一800任务构成，无历史候选/F3搜索。
- [正式runner](../../../contrib/satcompute/tests/integration/regression/run-final-scenario.py)：
  none、generate、generate+shadow；固定1300 s、66星、10 Gbps、1 ms、seed1/run11。
- [CompFRR验证器](../../../contrib/satcompute/tools/validation/compfrr-shadow/README.md)：
  Simplified v4纯模型、虚拟状态/事件和`summarize.py`。shadow与概率审计默认关闭。
  输出`shadow-decisions.csv`、`shadow-task-summary.csv`、`shadow-faults.csv`、
  `shadow-events.csv`及`shadow-assumptions.json`。

## 接受的结果与边界

800任务中387 START、382 ON；82个F1/F2直接victim中77个当刻ON、5个初始化miss。
真实平台仍717完成/83失败，shadow没有救回真实任务，也未改变故障/RNG/路由或传输。
ALL-OFF lifecycle waste=25625072.100 WU_eq；shadow=1354096.228 WU_eq；
recovery-only saving=96.647%，net lifecycle saving=94.716%。

仅为理想资源下的shadow/解析评估，不包含真实链路竞争、CPU/存储预留或恢复执行。
F3单列；本轮没有F2直接victim，不能由此估计F2保护覆盖率。
未来N5正式运行代码不得依赖本验证器，只能在测试中用纯模型做oracle。
完整账本结果及新增START大小/剩余计算时间、成本档位统计见[G4证据](G4-shadow-decision-evaluation.md)。

## 清理与短门禁

- 迁移：7份shadow C++文件、README、汇总脚本归入验证目录；ComputeProfile归入正式输入目录。
- 合并：布局辅助函数进入`task_workload_model.py`；G4 Python测试合并为`test_compfrr_shadow.py`；
  最终生成器/输入/runner测试收敛为`test_final_scenario.py`，故障固定fixture检查单列。
- 删除：旧N4C候选输入、旧预览/热点/F3搜索/压力生成入口、候选比较绘图、重复测试，
  G1/G2/G3中间审阅大报告及6份旧图导出；历史由Git保留，原始运行输出未删除。
- 保留：通用task/routing/fault/network测试、F1/F2独立标定工具、F2论文图、功能fixture、
  原生轨道和TaskModeling派生的稳定映射；上游`src/`不改。
- N4C文档只剩`README.md`与本目录三个文件：G3索引、G4索引、G4最终证据。

定向构建、16个C++测试程序、29项Python测试（原生切片复现实际执行、无skip）、
8任务shadow off/on/重复/audit/同刻F3小型smoke、topology smoke和CLI help通过。
长期测试列表及命令见[测试README](../../../contrib/satcompute/tests/README.md)。
迁移时刷新了4份旧路径的可再生成include转发文件；使用项目uv环境CMake重新配置构建，
未修改上游构建宏。旧脚本/目录引用检查与`git diff --check`通过。

最终生成器双次输出及正式TaskTrace逐字节一致；7份C++验证器、ComputeProfile与4份
冻结JSON原样核对。故障模型、TaskCoordinator、主程序代码不变，para仅变输入路径。
既有1 ms none账本及G4的22份真实输出一致性重新离线核验；已接受G4统计不变。
**本轮没有新跑1300 s完整ns-3、没有多seed/候选实验，没有SHA-256或真实备份实现。**

本地证据仍位于`output/n4c-g3-delay-1ms-20260909/`和
`output/n4c-g4-shadow-20260909/fault-11/`，被gitignore排除，不随tag上传。
