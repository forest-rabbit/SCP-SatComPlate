# N4C G3：C800 地理热点输入

复用相邻 `leo-66-1000s-n4c/compute-profile.json`：全 66 星均为 100,000 WU/s。
`task-trace.json` 的 800 个任务、类别、INPUT/RESULT 字节、WU 和到达时刻逐项保持 G2
不变；只改变 source/compute/result 端点。总 WU 为 183,958,466，时长仍为 1000 s。

`placement-manifest.json` 是离线分配证据，不是平台完整配置：使用原生 1 秒 ECEF
切片、preceding sample（年龄小于 1 秒），北美/欧洲/东亚矩形中距中心最近的一颗
候选权重 128，其余候选权重 1。位置随轨道变化，不把热点绑定到固定卫星 ID。
该地理分配是可审计实验假设，不代表测得的真实地面业务比例。

受控 F3 只用于单 victim 对照：node 41 上的 task 191，INPUT=1,000,000,000 B、
WU=1,500,000。由 candidate none 的真实业务时序选择，计划故障时刻 547.190531627 s，
对应 none 中70% WU进度；不读取温度/概率/SAA风险，不修改或屏蔽该星的 F1/F2。
普通 task 22 在 F3 前以该星为 INPUT 源端，于472.065216011 s完成传输并脱离该星。
该星此前正常通信/计算/转发，F3后永久断链；之后到达的任务不再使用它作为静态端点。
none 与 generate 使用完全相同的最终 TaskTrace。正式run若目标提前失败或进度不足，
必须如实记为F3验收失败，不能更换seed或目标补救。
F3 计划仅供离线场景和故障调度器，不能作为未来在线算法的输入。

生成入口是 `tools/generation/generate-task-workload.py --profile=n4c-hotspot`，
固定 `--seed=n4c-g3-hotspot --hotspot-weight=128 --regional-candidate-limit=1`；
先不带 `--f3-from-none` 生成纯placement并运行none，再带此参数生成最终输入。
128是预声明搜索上限，不代表故障数量已达标。完整命令、F1 beta/gamma及实际验收见
[G3 阶段证据](../../../../../docs/n4c/reviews/G3-hotspot-fault-calibration.md)。
这是 no-backup 输入，不包含 checkpoint、接管、任务复活或 N5 算法。

本轮冻结 beta=10、gamma=1.5，也是当前 fault-para.cc 默认值；可用
`--faultF1Beta/--faultF1Gamma`（runner 的 `--f1-beta/--f1-gamma`）记录实验值。
日常运行无需 `--audit`；仅概率/温度/原生位置核验时打开，输出目录必须是新目录。
