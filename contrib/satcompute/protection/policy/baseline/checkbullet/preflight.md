# CB-Sat 实施进度与边界

依据项目外的 `CB_Sat_v2_Codex_Implementation_Plan_Final.md` 与
`CheckBullet_Satellite_Baseline_Model_v2_FullInput_NoTail (1).md`。
分支 `feature/pre-n5c-cb-sat`，起点 `n5@2ccfa392e`。各 G 阶段连续推进，
不增加例行人工等待；合同冲突、范围扩展和最终 PR 审阅仍须明确处理。

## 冻结条件与复用

- 正式输入：`input/experiments/leo-66`，66 星/66 计算星、800 任务、1300 s。
  INPUT=194119753287 B，WU=352513119，LLM=400 WU/token。
  100000 WU/s、10 Gbps、1 ms、20 s、deadline=1.3、10 GB 备份池。
  seed=1/run=11，F3 node62/1027.055770726 s；全部沿用公共 runner。
- 真实服务：TaskCoordinator、ComputeService、NetworkTransferEngine、LocalDelivery、
  BackupStoragePool、PlacementPolicy、ExecutionAttempt 和现有状态大小/成本接口。
- CB 自有：H/X、完整 INPUT、根/连续日志的 r/q、正常保护与恢复编排。
  不构造双层 CheckpointManager/RecoveryController，不扫描其他节点获取更新状态。
- 可选共享机制：仅语义一致的对象搬运/增量应用；恢复节点排序和 REMOTE_BUSY
  扩展显式复用，不能自动引入 tail。旧 fixed/CompFRR 的 tail 保持不变。
- 与模型基础版差异：本轮冻结 pooled MTBF；忙时不等待，显式选择重算/迁移。
  原地合并、占用保底的动态存储份额、零读取/常数合并是工程适配，不是原文实测。
  恢复 cR 的等待已纳入 reserved-idle 时不重复计入总 eq-WU。
- 故障快照只使用严格早于故障纳秒的有效提交；物理清理必须保留这一状态，
  不仅回滚数值。LocalDelivery 仅按实际 source==destination 判断。
- 10 次独立校准（seed1/run101..110、off、F1/F2）后冻结 MTBF，再做八组正式 CB。
  不使用 run11 的未来故障反推参数，不新增哈希/完整场景配置层。
  旧 32 组输出只读；共享改动按影响范围做等价回归，新结果写新目录。

## 阶段记录

| 阶段 | 验收内容 | 状态 |
|---|---|---|
| G0 | 合同、基线、环境、共享边界与旧模式验证 | 完成 |
| G1 | 合法 H、X、完整 INPUT 与独立 r/q 状态单测 | 完成 |
| G2 | 真实初始化/增量/合并及清理 | 完成 |
| G3 | direct、忙时重算/已存迁移、一次恢复 | 完成 |
| G4 | 模式接线、四 placement、计量与旧方案回归 | 完成 |
| G5 | 独立 MTBF 校准与参数冻结 | 完成 |
| G6 | 八组 smoke、八组正式运行、确定性复核 | 完成 |
| G7 | 原始证据审计、统一比较、交付 | 本地完成，待人工审阅与发布授权 |

## 已执行检查

- 构建前工作树干净。项目 uv 环境 `.venv/bin/python` 为 Python 3.10.12。
  沿用既有 Ninja 配置；satcompute enabled，ns-3 examples/tests 均 OFF。
- `source .venv/bin/activate; ./ns3 build -j 2`：通过（修改前基线）。
- 修改前维护 C++：23 个测试程序通过；Python unit：118 项，117 通过、1 项条件跳过。
- G1：项目 CMake 编译目标 `satcompute_test_satcompute-cb-sat-policy-test` 通过；
  `./ns3 run --no-build satcompute-cb-sat-policy-test` 的 89207 项检查通过。
  覆盖合法边界、H 越界、X=0/无穷恢复上限、共享份额、乱序缺口、同纳秒故障截止、
  合并期间接收、完整 INPUT 保留和迟到旧回调；旧 protection-contract 的 17958 项仍通过。
- G2：`satcompute-cb-sat-runtime-test` 15 个真实网络/计算场景、881296 项检查通过。
  四类任务、四 placement、两 owner 份额、首选存储拒绝、失败 FULL 周期重试、
  cL 中止、合并同纳秒旧对象保留、零容量/无穷 MTBF、终态无泄漏均覆盖。
  G1 和旧 protection-contract 同时复核通过。测试中的 MTBF=1 s 不属于正式参数。
- 修复一份本机失效的生成头文件缓存（LRL 仍指向旧 experimental 目录），由 CMake
  重新生成；未修改对应源代码。显式 configure 会刷新 PCH，后续只做增量 build。
- G3：`satcompute-cb-sat-recovery-test` 21 个真实场景、943 项检查通过。
  65/40/60、三条路径、非 busy 等价、部分初始化、F1/F2 attempt 免疫、F3 依赖中断、
  原 compute deadline（含同纳秒完成）、非法日志计划拒绝和实际执行/释放均覆盖。
  维护 C++ runner 全部 26 个程序通过；原有 tail/重计算/1+1 测试保持通过。
- G4：构建、27 个维护 C++ 程序、126 项 Python（125 通过、1 条件跳过）及完整
  smoke runner 通过。恢复测试含 CSV 后为 3712 项检查；21 份实际恢复证据的独立审计
  通过，8 种损坏证据均被拒绝。旧 fixed tail、CompFRR、R0/R1 及 16 组 placement
  smoke 保持通过；CB 缺少正式 MTBF profile 时明确失败，不回落到单测值。
  构建保持 satcompute enabled、ns-3 examples/tests OFF；无上游源码或旧方案机制改动。
  校准统计的 8 项 Python 边界测试已通过；这不代表 10 次 pilot 已执行。
- G5：10 次完整 1300 s pilot（run101–110）全部成功。累计 33430 次有效检查、
  33430 s 暴露、806 次联合故障，冻结 MTBF=41.47642679900744 s，覆盖 64 颗星。
  32 次范围外事件不计入；真实主计算服务 32897.853004236 s 仅作诊断。
  证据：`output/cb-sat-v2/20260912T163425212424Z-calibration`，执行 HEAD `a47128a1d`。
  同一执行版本的完整 regression runner 通过，联合验收 88 完成/12 失败、11 次 START，
  483 条实时概率记录全部匹配。旧故障生成器、输入与恢复机制未修改。
- G6 前置：八组 smoke 与额外 FA-LRL/relocate 重复运行全部通过，26 个输出文件确定性匹配。
  证据 `output/cb-sat-v2/20260912T170241158297Z-smoke`，执行 HEAD `1a43a1431`。
  新增同纳秒完整 fault batch、仿真截止、68/40/60 且外部持有 66% 的 direct/relocate
  测试；25 个恢复场景共 4577 检查通过，25 份原始证据通过独立审计、10 种损坏被拒绝。
  Python 工具验证新增缺组/重复组/混合执行版本/混合参数拒绝，不将不完整矩阵算作完成。
- G6 正式：`output/cb-sat-v2/20260912T171143276614Z-formal`，统一执行 HEAD
  `494e133c5`，八组完整 1300 s、退出码均为 0。四个 relocate 组均 800 完成；
  recompute 按 FFP/LRL/FA-FFP/FA-LRL 为 795/797/795/798 完成，失败保持原 deadline。
- G7：八组原始证据全部通过审计，生成 40 行公共表、36 行恢复附表、4 行副本附表。
  首次审计错误地拒绝 PARTIAL，已修正离线判据并保留原审计记录，不改原始运行。
  来源约束加严后，全部真实已执行恢复仍满足约束；正式执行与后续审计提交分别记录。
  最终 26 个恢复场景/5079 检查、26 份证据/11 类损坏拒绝通过；27 个维护 C++ 程序
  及 128 项 Python（127 通过、1 条件跳过）通过。Python 跳过项需要未指定的
  `SATCOMPUTE_POSITION_SLICES` 原生轨道切片，并非 CB 测试缺失。
  详细比较、执行命令和适用边界集中在
  [验收报告](../../../../../../docs/n5/reviews/Pre-N5C-cb-sat-v2.md)。
- 最终代码复核：执行 HEAD `17fd3cc49`，
  `output/cb-sat-v2/20260912T182403752476Z-smoke` 八组及重复均通过。
  八组分别与原 smoke 的 26 个输出文件匹配（同一离线审计器，仅排除执行身份、路径与墙钟），
  证据为 `determinism.json` 和 `cross-version-determinism.json`。故障路径由上述
  26 个受控场景及八组正式来源审计覆盖，不把无故障 smoke 当作正式故障重跑。

本轮不自动 push、PR、CI、merge 或 tag；保留分支等待人工审阅，不进入 N5C。
