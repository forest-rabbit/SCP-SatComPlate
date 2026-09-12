# CB-Sat 参数来源与适配边界

| 量 | 来源及当前合同 |
|---|---|
| MTBF / TF | 独立 off/F1/F2 pilot 的 pooled 有效检查暴露/联合故障数；正式 run11 不参与拟合。标定结果完成后记录于 `calibration/frozen-mtbf-profile.json` |
| H | `sqrt(2*TF*(cL+cR))/T0`，第一次普通主计算时确定；对齐公共合法应用进度，目标仍锚定原任务 |
| cL / cR | 公共 `GetProtectionCosts(TaskStateAdapter::VariableBytes())`；无 CB 私有成本表，不按局部 checkpoint 大小重新选档 |
| INPUT / F / D | 完整原始 S 单独保存；F=公共状态 K(w)+H_header；D=两实际捕获点间的状态差+H_header。不使用旧双层的剩余 INPUT 接口 |
| 恢复读取与融合 | 工程起步：base/read=0；非空日志一次公共 cR。不是原论文测量结果，也不是已校准的线性日志读耗时 |
| X_R | 用上述恢复成本及原 compute deadline 剩余预算求解；预算够用时可无穷，不硬填有限值 |
| X_S / 份额 | 真实待保存日志字节；已占用 U 保底，余量 Free 按 owner 数均分，稳定 ID 分配余数字节；实际申请仍受物理池约束 |
| X | 恢复、存储与剩余合法目标的最小约束，无固定 X=4、无隐含实现 cap，不把不可行值夹成 1 |
| Placement / busy | 公共四 placement；busy 开关仅作用于 REMOTE_BUSY。恢复排序与常态部署不是同一决策 |
| 仿真输入 | 公共 final-scene helper；66 星、800 任务、352513119 WU、400 WU/token、100000 WU/s、10 Gbps、1 ms、1300 s、deadline 1.3 |

MTBF 的时钟是“健康、普通主任务尚未完成时，每次真实模型检查”乘检查间隔。
同星同检查点 F1+F2 双命中只算一次，空闲/不可用时段不进入该统计范围。
任务恰好完成的同纳秒不纳入有效暴露；真实连续计算服务时间另报诊断，不混入分母。
保留逐 pilot、逐节点的暴露/命中及 q 诊断；不是把完成前累计概率当单步概率。

固定 arrival trace 只更换故障随机实现，不能称作十种独立工作负载。八组正式矩阵是
八种配置而非八个统计重复；冻结场景含受控 task120/F3，不能宣称无偏多 seed 论文结论。
CB 与 CompFRR 在 INPUT 保留、单/双层状态以及 tail 权限上不同，主表是系统方案比较。

资源采用现有账本：所有尝试的实际 WU 减去一个成功任务的有用 WU，加正常已完成
cL/cR equivalent cost，再加恢复 reserved-idle equivalent cost。恢复融合 cR 已位于
reserved-idle 时不重复加入；planned catch-up 不能替代实际服务 WU。
网络采用源端实际应用发送 Byte（包含失败/取消已发量），不是逐跳 byte-hop。

独立标定已完成：执行 HEAD `a47128a1d`，证据目录
`output/cb-sat-v2/20260912T163425212424Z-calibration`。
10 次完整 1300 s pilot 的有效检查/暴露为 33430 次/33430 s，联合命中 806 次，
故 `TF=33430/806=41.47642679900744 s`。64 颗星有有效暴露；另外 32 次范围外事件
不计入分子。连续主计算服务时间为 32897.853004236 s，不替代离散检查分母。
逐 run/逐节点证据已保存于 profile；正式 run11 与单测参数均未参与标定。
