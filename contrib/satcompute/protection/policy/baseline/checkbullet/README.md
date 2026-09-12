# CB-Sat v2

CheckBullet 的卫星平台适配基线：一个备份节点，保存完整 INPUT、一个状态根和连续增量日志。
不继承 Fixed/CompFRR 的双层部署或跨节点 tail 获取能力。当前实施进度见
[preflight.md](preflight.md)；正式 MTBF 校准及八组实验尚未完成。

## 状态与正常保护

- `cb-sat-policy.h/.cc`：第一次普通计算开始时求 `g=sqrt(2*MTBF*(cL+cR))`、
  `H=g/T0`。当 H 不小于 1 时不产生周期检查点；否则按原任务进度对齐公共合法边界。
  X 使用恢复预算、实际日志大小与当前存储份额求解，不固定为 4，不把不可行值夹成 1。
- `cb-sat-state.h/.cc`：`r` 是已融合根的 WU，`q` 是该备份节点已保存连续日志可覆盖的 WU。
  初始化的 FULL 不含 INPUT，且不是额外的第一份 DELTA。接收乱序不能跨缺口推进 q。
  故障只使用严格早于该纳秒的提交，停止后拒绝迟到回调。
- `cb-sat-manager.h/.cc`：在第一个 H 边界选择单节点，真实发送 P→B 的完整 INPUT/FULL。
  后续每份增量完成异步 cL 后立即申请传输，不等累计到 X 才发送；B 收齐连续日志达到 X
  后执行本地 cR 合并。源端未确认记录、接收 reserved、INPUT/根/日志均占用公共存储池。
  失败传输只在下一个 H 边界重试同一份记录；源端空间不足时跳过尚未捕获的边界，
  下一次从最后实际捕获的位置生成较长增量，不伪造过去快照。

四种 placement 使用公共单节点接口：minimal 的 ffp/lrl 只实际尝试当次选中的一颗星；
fa-ffp/fa-lrl 先筛选真实路径及存储可行性。常态保护一旦选定 B 就不偷偷重新部署。
恢复时的节点排序与常态 placement 是不同决策，不能据目录名称混为一谈。

当前恢复成本起步约定是零读取、非空日志集合一次公共 cR。因此恢复预算足够时
`X_R` 不形成有限上界，X 主要受实际存储和剩余合法事件限制；这不是已测量的线性读取模型。
节点共享容量按“各 owner 已占用量 + 均分剩余空间”规划，最终由真实 pool 准入。
根/日志原地融合不分配第二份 FULL，完整 INPUT 不参与裁剪。旧对象延迟至下一纳秒清理，
以保障故障同纳秒的严格截止语义。

所有耗时在内部使用整数 ns；cL/cR 不暂停主任务。仅完成的正常操作计 equivalent cost，
尚未完成就取消的生成/合并不冒充已执行完成。实际网络字节仍由公共传输引擎计量。

## 验证

`cb-sat-recovery.h/.cc` 独立编排一次恢复：空闲 B 从 q 直接继续；仅在 REMOTE_BUSY
按公共开关选择从零重算或把 B 的完整 INPUT、根和已存日志迁移至 C。迁移不提高 q。
其他不可用回退沿用公共恢复许可，不把 busy 开关扩大到所有故障。故障决策在完整
同纳秒 fault batch 后执行；F1/F2 只对已接受的恢复 attempt 免疫，F3 仍中断真实依赖。
复用原 compute deadline，实际执行 WU 与计划追平 WU 分账；同星交付不创建 UDP。

测试仍在项目统一的 `tests/unit`，不在本目录建立另一套测试体系：

```bash
source .venv/bin/activate
cmake --build cmake-cache --target satcompute_test_satcompute-cb-sat-policy-test \
  satcompute_test_satcompute-cb-sat-runtime-test satcompute_test_satcompute-cb-sat-recovery-test -j 2
./ns3 run --no-build satcompute-cb-sat-policy-test
./ns3 run --no-build satcompute-cb-sat-runtime-test
./ns3 run --no-build satcompute-cb-sat-recovery-test
```

以上命令从仓库根目录执行，不开启 ns-3 全局 examples/tests。测试显式注入的 MTBF
只用于构造边界场景；正式执行必须使用后续独立 pilot 得到的冻结统计值。

## 接入与执行工具

`cb-sat-config.*` 只读取独立统计 profile；`cb-sat-controller.*` 接入完整生命周期，
`cb-sat-metrics.cc` 保存独立 `cb-sat-*.csv`。公共 task/transfer/link 指标继续保留。
源缓存、INPUT、FULL 和 LOG 的逐对象变化在 events 中，storage 给出每节点峰值和最终占用。
恢复 CSV 中 planned WU 与 actual WU 分列；网络统计使用真实发送量，失败/取消的已发字节也计入。
`normal + execution waste + reserved idle` 可相加；已包含在 idle 中的恢复 cR 不再次相加。

从仓库根目录按顺序执行（已有输出不会被覆盖）：

```bash
.venv/bin/python contrib/satcompute/protection/policy/baseline/checkbullet/tools/calibrate-cb-sat-mtbf.py --stage all --jobs 2
.venv/bin/python contrib/satcompute/protection/policy/baseline/checkbullet/tools/run-cb-sat-matrix.py --stage smoke --jobs 2
.venv/bin/python contrib/satcompute/protection/policy/baseline/checkbullet/tools/run-cb-sat-matrix.py --stage formal --jobs 2
.venv/bin/python contrib/satcompute/protection/policy/baseline/checkbullet/tools/analyze-cb-sat-matrix.py --root output/cb-sat-v2
```

标定完成后先核对并提交 profile，再运行矩阵；runner 要求干净执行快照，但不会自动提交。
10 个 off/F1/F2 pilot 使用相同 arrival trace、seed1/run101–110，F3 关闭。
统计口径是“健康、普通主任务未完成的运行中检查点数 × 检查间隔”；联合命中只计一次，
空闲事件不计入分子。实际连续服务暴露仅作诊断，不能混作分母；pooled MTBF 为总暴露/总事件，
不是每次 MTBF 的平均。零命中显式表示无穷。八组矩阵不读取 run11 的未来故障来反推参数。
