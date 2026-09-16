# N5A-G3：故障恢复闭环审阅

状态：**实现与本地验证完成，等待 G3 审阅；STOPPED AT N5A-G3**。

## 基线与范围

- 分支：`feature/n5a-protection-runtime`；继续 Draft PR #95，base=`n5`。
- 本报告对应的代码 HEAD：`6a74b0a18be17118b183619386cebc178e77678e`；其后的报告提交不改变代码。
- `n5` 基线：`2a64595c7307fbac1cc647e55e1ec24d19985ea0`。
- G2 审阅起点：`14a0776078b9f110fa33a882819f24bace0797d4`；G2 代码 `73e30c9a48b4db8f3acce1a1ec6e21e8ab5a51d1`。
- 授权材料：用户提供的 `N5A_G2_Audit_and_G3_Start_for_Codex.md`，G2 PASS、允许 G3。
  用户随后确认：同星 RECOVERY_INPUT/RESULT 使用 LocalDelivery，不为产生 UDP 排除合理恢复节点。
- 未改正式输入、参数数值、故障概率模型、随机数、上游 src 或 CI；para 仅修正过时注释。

## 代码与职责

本次代码提交共 37 个文件，集中在既有模块；完整清单可用 `git show --stat 6a74b0a18` 查看。

| 范围 | 改动 |
|---|---|
| `protection/runtime/recovery-controller.*` | G3 机制与任务故障适配；通过既有 ProtectionRuntime 先尝试 checkpoint，再调用 policy 的 RECOMPUTE fallback |
| `protection/mechanism/checkpoint/checkpoint-manager.*` | 严格故障快照、实体保留、Quiesce、共享传输 ID、恢复事件 |
| `protection/storage/backup-storage-pool.*` | 按对象身份保留，其余按任务释放 |
| `task/{compute-task,compute-service,task-coordinator}.*` | generation、真实恢复锁/服务、原 deadline、winning RESULT、实际 busy accounting |
| `traffic/{local-delivery,network-transfer-engine}.*` | 同星逻辑交付；跨星合同不变，仅补业务分类及只读剩余容量查询 |
| `metrics/{core,metrics.cc}` | 恢复摘要/事件、业务 RESULT 分账、logical completion 与实际服务时间 |
| `satcompute.cc`、CMake、模块 README | fixed+generate 接线、输出清理、构建和合同说明 |
| `tests/` | 受控跨组件用例、平台入口 F3 smoke、参数门禁调整；保留原测试 |

## 执行合同

1. **故障拦截**：primary RUNNING 故障先冻结 `xf/lf/rf`、有效 local 对象、remote 对象/字节、
   pending/in-flight 身份、cR-pending、原 deadline。取消 primary attempt 不立即 logical FAILED。
2. **实体保留**：先保留所有潜在可用对象并 Quiesce 旧操作；下一纳秒通信 overlay 已应用后
   裁决并锁定节点，再释放最终不需要的对象。正常完成/保护放弃仍全清理，不复用故障的 Quiesce 语义。
3. **同纳秒**：仅 `valid_time < fault_time` 可用。G3 物理 Merge/旧记录清理延迟 1 ns，名义 commit
   时间不变；故障因此可用旧 backing object。没有第三份完整复制，也没有只回滚进度数字。
4. **节点接受**：非主星、当前存活且计算可用、无运行任务且队列为空、必要路径可达；原子预留
   `(task_id,1)`。等待 INPUT/tail/cR 是 reserved-idle，普通任务可入队但不能抢占，不计 busy。
5. **INITIALIZING/OFF**：不能使用半份 base 或单独 local 增量；由策略回退到原 source 的 INPUT 重放。
   原 source 是输入保留点，不另计备份池。source F3 且无 committed state 时失败。
6. **REMOTE_REDO**：只采用 frozen `rf`，不传 INPUT/tail，直接执行 `W-rf`；`lf=rf` 不创建零字节 tail。
7. **TAIL**：实际传输 `(rf,lf]` records 的字节和（含 H），先预留临时对象；收齐再等 cR 原地融合到
   `M(lf)`，释放被覆盖 local，接入真实 ComputeService 执行 `W-lf`。
8. **估计与选择**：当前路径、传播、剩余瓶颈容量、payload 序列化，加 cR/追赶 WU；不读取未来队列或故障。
   tail 严格更短才执行，否则 redo；只执行一条，不事后取真实耗时最小值。
9. **RECOMPUTE**：按稳定 ID 选可行节点，从原 source 交付实际 input bytes，再执行全部 WU。
10. **LocalDelivery**：source=recovery 的 INPUT、recovery=result 的 RESULT 不创建 UDP/transfer ID，
    不计网络字节；仍有真实 payload、LOCAL 方式、时间和 logical completion。使用 1 ns 本地因果阶段守卫。
11. **跨星与 RESULT**：全部走原引擎；原 `2*T` RESULT 以 CANCELLED/不再需要保留历史，新跨星 RESULT
    从恢复星发出，属于业务流。任务完成看 winning RESULT，不要求废弃旧 RESULT 完成。
12. **实际计算与追赶**：恢复不是另一个裸 `WU/rate -> Schedule`，而是 ComputeService 的真实占用。
    `CATCHUP_REACHED` 在服务达到 frozen xf 时产生；catchup redo WU、后续正常 WU、完整重算 WU 分列。
13. **deadline**：仍从首次 primary RUNNING 建立；等待与恢复消耗原预算，不重置；同纳秒算完视为按时。
14. **F1/F2**：仅已接受的 RECOVERING/RUNNING_BACKUP attempt 忽略后续计算中断。节点可用性、
    模型/RNG/事件照常推进，普通队列仍受停机影响；计算完成结束免疫。
15. **F3**：恢复星永久失效立即终止，不做第二次恢复；外部备份不因主星 F3 消失。local F3 失去 tail，
    remote 不可用则尝试重算。RESULT 期间 F3 仍有效，活动跨星 RESULT 保留正常端点 FAILED 原因。
16. **隔离/清理**：旧 primary compute/result 回调被 generation 拦截；逻辑终态唯一。
    失败、完成、仿真截断均取消操作并释放锁/存储/网络预留；结束后清理不重复累加 busy time。

## 指标与受控证据

`recovery-summary.csv` 记录故障身份、冻结进度/对象/在途操作、两条估计、实际路径、各阶段时刻、
Tcatch、deadline、reserved-idle、分类 WU、正常保护成本、终态，以及真实 result bytes、delivery mode、
result transfer ID 和 logical completion。不适用的时刻/估计/ID 留空。
`recovery-events.csv` 记录精确交付方式；必需 G3 事件也写入 `protection-events.csv` 的 generation=1 行。
业务吞吐排除保护流/输入重放和本地交付；真实链路、网络与容量账本仍统计所有真实数据包。

| 验证 | 结果 |
|---|---|
| 项目构建 | 通过；全局 examples/tests 仍关闭 |
| 19 个 C++ 测试程序 | 通过；含原故障/路由/计算/存储测试 |
| G3 专项 | **590 项检查通过**；20 个故障场景、1 个无故障提交定位运行，以及 LocalDelivery/服务锁小测 |
| Python 单测 | **59 通过，无跳过**；使用既有 position slices，不重新跑正式场景 |
| smoke | **9 组通过**；包含原 8 组与新增 G3 CLI 对照 |
| G2 无故障回归 | 4 任务、81 保护流全部完成，17 次远端提交；off/fixed 主计算时间相同、存储归零 |

受控故障覆盖任务书 A–Q：不可用保护/初始化回退、空 tail、tail 更快/redo 更快（相等选择由 G1 合同测覆盖）、
local F3、remote F3/计算停机、source F3 无输入、恢复 F1/F2 继续、恢复 F3 失败、deadline 按时/超时/恰好相等、
同纳秒 UID 反转、迟到 primary 回调，以及新节点跨星 RESULT。另测 RESULT 中 F3 和仿真截断。
故障使用 FaultController 的确定性测试注入，不通过提高随机概率碰场景；恢复经过真实网络、存储、计算和任务服务。

同纳秒实体测试使用**第二次** RemoteCommit：两种 UID 顺序均冻结
`xf=43352, lf=40500, rf=20500 WU`，旧 remote 实体 **23,511,040 B**，tail **22,937,600 B**；
最终本地 RESULT 时刻均为 **1,053,955,594 ns**。证明的不只是零字节初始化或历史数字。

平台入口 smoke：复用 16 星/4 类任务/15 s fixture，主星 3 在 1.4 s 发生受控 F3。
off 完成 **0/4**；fixed 完成 **1/4**，救回正在计算的 dense-image 任务；其余三个任务后续遇到永久失效主星失败。
恢复路径为 TAIL，`rf=22807, lf=32245, xf=35488 WU`；tail **6,292,074 B**。
任务在 **1,871,549,002 ns** 算完，下一纳秒在恢复星 0 本地交付 **52,428,800 B RESULT**；
没有结果 UDP 流，原 RESULT 取消历史仍保留。off 是无保护对照，fixed 与 repeat 的重复证据逐字节一致，池最终归零。

## 复现与输出

使用项目 uv 环境，命令见 [测试 README](../../../contrib/satcompute/tests/README.md)。专项：

```bash
./ns3 run --no-build "satcompute-recovery-runtime-test --outputDir=output/n5a-g3/controlled"
.venv/bin/python contrib/satcompute/tests/integration/smoke/run-recovery-smoke.py --output-root output/n5a-g3/smoke
```

本地证据在 `output/n5a-g3/{controlled,smoke}`，按既有 gitignore 不提交生成数据。
构建、全量 C++、Python、smoke 日志分别在 `/tmp/scp-n5a-g3-final-{build,cpp,python,smoke}.log`；
加强后的专项日志为 `/tmp/scp-n5a-g3-recovery.log`。其他 N4 回归没有被删除或降级。

## 边界与下一门禁

已验证 off 的既有五次转换、INPUT/RESULT 编号、FCFS 和故障行为；不增加 off 的恢复对象或 CSV。
保留原 UDP 无 ACK/无重传语义，丢包可能导致等待或截断；估计不包含未来排队，也不声称精确预测真实耗时。
状态是既有应用布局模型，不是真实运行/序列化任意 LLM 或编码器。仍只有一次恢复、健康空闲节点接受，
不加入动态概率决策、频率优化、N5C 放置、1+1 或 Multi-tree。
原 capacity-aware 在 t=0 直接同步启动流的历史限制未在 G3 扩大修改；受控 fixture 从 1 ns 后到达。

没有运行正式 800 任务/1300 s、批量种子或大规模性能实验，也没有运行阶段 CI。
当前未发现阻止提交 G3 审阅的技术问题；更大规模与四类任务联合恢复覆盖留待用户批准 G4。
Draft PR #95 不合并，main/n5 不前移，不打 tag、不清理仍在开发的分支。

**STOPPED AT N5A-G3；等待用户审阅后才能进入 G4。**
