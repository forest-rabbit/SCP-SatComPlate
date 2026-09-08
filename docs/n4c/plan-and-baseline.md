# N4C 计划与基线

当前状态：N4C-0/N4C-1 已完成 G1 v3 工作负载构成对比与本地验证，等待候选选择，见
[G1 审阅包](reviews/G1-workload-state-mapping.md)；G1 尚未批准。源任务书为工作区中的
`Codex_N4C_Implementation_and_Review_Gates.md`（2026-09-08 修订版）。后续对话确认：
**仅修改 SatComPlate；TaskModeling 不修改、不重新实验；LLM 不下载、不运行。**
本页覆盖源任务书第 3.2 节原来的跨仓库开发安排，不修改用户原始任务书。
本轮 G1 以 `Codex_N4C_G1_Review_Workload_State_Revision_v2.md` 为准，覆盖上一版
`Codex_N4C_G1_Review_and_cL_cR_Distribution_Revision.md` 的网格和 batch 统计要求。
不恢复已撤回的 `checkpoint_size_analysis.py`；此前首版的搜索网格也从当前工具中移除。
v3增量依据 `Codex_N4C_G1_v3_Workload_Composition_Review_and_Revision.md`：保留v2映射
及历史证据，只比较C1000/C800/C600和10个1 GB+20个500 MB大图像构成，不先冻结C800。

## 四个审阅批次

| 批次 | 范围 | 停止点 |
|---|---|---|
| 第一阶段 | N4C-0 盘点 + N4C-1 工作量/状态纯函数、LLM 公式、离线候选预览 | G1：工作量、字节、算力候选审阅 |
| 第二阶段 | N4C-2 四类 TaskTrace/deadline/无故障基线 + N4C-3 在线风险与 replay 清理 | G2：输入/接口/故障标定预算审阅 |
| 第三阶段 | N4C-4 原生位置驱动地理负载、none 对照、故障标定与独立验证 | G3：热点、任务影响和参数冻结审阅 |
| 第四阶段 | N4C-5 整体验收、文档、获批合入、阶段 CI 和 tag | G4：收口确认；之后才进入 N5A |

各批次做匹配的本地测试；全部 N4C PR 合入 main 后才手动运行一次阶段 CI。
G1/G2/G3 未批准时不自行推进下一批或合入对应设计。阶段完成前不改里程碑为完成。

## 启动状态和历史

- 平台启动分支 `main`，HEAD `c40e3f56a8696734117ca24f104a0062078c98aa`，工作区干净；
  开发分支 `feature/n4c-workload-mapping`。
- TaskModeling 为 `main`，HEAD `0dbc0c7b6281219e1356151fd640336cde885e7d`，工作区干净；
  仅阅读测量合同和现有结果，不建立分支，不添加导出或运行时依赖。
- 最新相关平台提交是 PR #87 的 N5 前置说明；前一功能阶段是 PR #86 的链路指标与
  10 Gbps 压力基线。新模型不与旧 1500 任务逐项相同。
- `n4b-complete` 是已有 annotated tag；其后还有 `65bd39a1f`（PR #83）的 F2 修订。
  最新完整 replay 实现可在上述平台基线提交查阅，不能只指向旧 tag。第一阶段不删 replay。
- CMake 已配置 `satcompute`，examples/tests 均为 OFF；使用本项目现有 `.venv` 工具。
  新工具只用 Python 标准库，不触碰根 pyproject、依赖、CI 或上游 src。

## 接口盘点与分批影响

| 当前接口/证据 | 现状 | 处理批次 |
|---|---|---|
| `generate-task-workload.py::generate_work_units` | 旧四类、类内排名和抖动分配 WU | 第一阶段独立纯函数；第二阶段正式消费 |
| `TaskDefinition` / `TaskTrace` | 没有 task_profile/deadline，closed-world 拒绝未知字段 | 第二阶段；第一阶段预览不是 TaskTrace |
| `ComputeService::CalculateServiceTimeNs` | `ceil(W * 10^9 / rate)`，uint64 输入/int64 纳秒 | 第一阶段预算沿用；不改变 FCFS |
| `TaskCoordinator` | INPUT/计算/RESULT，故障中止与终态不可逆 | 第二阶段保留、补充分类与等价测试 |
| `FaultController` | 同时承担 generate 和 replay 公共执行职责 | 第二阶段只删 replay 专属入口 |
| `FaultPredictionEngine` | 审计开关启用、NOTICE 输出门控、运行任务影子状态 | 第二阶段改为无副作用在线查询 |
| `fault-para.cc` | 独立 F1/F2；F3 fixed-K；现有空间参数 | 第一阶段不调参；第三阶段审阅标定 |
| TaskModeling 的 `sigma_bytes_per_work_unit` | total-byte 口径，真实三类结果尚无 WU | 不修改；平台新增 variable-only 字段 |

第一阶段新增位置为 `tools/generation/task_workload_model.py`、离线预览工具及
`tests/unit/` 对应测试。所有函数均无 ns-3 运行、模型加载或网络查询副作用。
正式 `para.cc`、TaskTrace、ComputeProfile、故障参数和旧 fixture 保持不变。

## G1 v3 候选与待审阅内容

- 三类图像 `a_z=1`，`W=ceil(3*S/2000)`，1 GB 对应 1,500,000 WU，参考算力
  100,000 WU/s、纯计算15 s；不设最小时长。完整字节分账和来源见
  [工作量模型](workload-mapping.md)。算力变化是实际仿真负载变化，不是实测卫星性能。
- LLM 只用公开结构 `28/8/128` 和 2 B 元素场景；合成 token 使用正整数 WU/token，
  避免 ceil 使统一 sigma 与每 token 字节账不一致。H_LLM=0 是显式预算假设。
- LLM 固定100 WU/token，确定性 N=P+G=5000..10000，对应5..10 s；每 token 的
  KV保持114,688 B，因此总状态随 N 增大。小型请求 INPUT 不等于 KV 字节。
- 状态预算点向后继合法 tile/整图/token 边界对齐，只保留5/10/20%守恒检查；
  不生成 L1/remote/tail backup object，不搜索 n/delta，不统计 D_L/D_R 或成本分段。
- 保留V2-1500作历史对照，新增C1000/C800/C600；比例3:3:3:1、INPUT精确81.75 GB。
  新候选大图像改为10个1 GB、20个500 MB，仍按原权重只分给dense/compression。
- 对比普通图像与完整图像的大小/时长、短任务比例、LLM需求占比和状态预算，不代表
  已完成 TaskTrace 接入、无故障全量运行、deadline 设计、地理分配或故障标定。
- 重要风险：普通图像与 1 GB 尾部时长差异、合法保存粒度及 1 s 风险周期、
  参考 rho 跨输入外推、LLM 等成本 token 简化。不能靠调整故障强度隐藏这些风险。

G1需要用户从C600/C800/C1000中选择并正式批准；不设LLM占比或短任务占比的任意硬阈值。
本地测试通过仅说明实现与候选合同一致，不能因审阅者倾向C800就默认接入G2。
N5A 才做固定 n/delta/节点的真实备份恢复；N5B 做频率优化与状态/成本分析；
N5C 做节点选择和共享池。本轮完成后提交、推送原分支，停止于 G1，不进入 G2。
