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
| G2 | 真实初始化/增量/合并及清理 | 进行中 |
| G3 | direct、忙时重算/已存迁移、一次恢复 | 未开始 |
| G4 | 模式接线、四 placement、计量与旧方案回归 | 未开始 |
| G5 | 独立 MTBF 校准与参数冻结 | 未开始 |
| G6 | 八组 smoke、八组正式运行、确定性复核 | 未开始 |
| G7 | 原始证据审计、统一比较、交付 | 未开始 |

## 已执行检查

- 构建前工作树干净。项目 uv 环境 `.venv/bin/python` 为 Python 3.10.12。
  沿用既有 Ninja 配置；satcompute enabled，ns-3 examples/tests 均 OFF。
- `source .venv/bin/activate; ./ns3 build -j 2`：通过（修改前基线）。
- 修改前维护 C++：23 个测试程序通过；Python unit：118 项，117 通过、1 项条件跳过。
- G1：项目 CMake 编译目标 `satcompute_test_satcompute-cb-sat-policy-test` 通过；
  `./ns3 run --no-build satcompute-cb-sat-policy-test` 的 89207 项检查通过。
  覆盖合法边界、H 越界、X=0/无穷恢复上限、共享份额、乱序缺口、同纳秒故障截止、
  合并期间接收、完整 INPUT 保留和迟到旧回调；旧 protection-contract 的 17958 项仍通过。

本文件是进度记录，不将尚未执行的校准、正式运行或测试写成通过。
