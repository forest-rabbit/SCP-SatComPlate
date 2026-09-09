# N4C G3：C800 地理热点输入

复用相邻 `leo-66-1000s-n4c/compute-profile.json`：全 66 星均为 100,000 WU/s。
`task-trace.json` 的 800 个任务、类别、INPUT/RESULT 字节、WU 和到达时刻逐项保持 G2
不变；只改变 source/compute/result 端点。总 WU 为 183,958,466，时长仍为 1000 s。

`placement-manifest.json` 是离线分配证据，不是平台完整配置：使用原生 1 秒 ECEF
切片、preceding sample（年龄小于 1 秒），北美/欧洲/东亚矩形中距中心最近的一颗
候选权重 64，其余候选权重 1。位置随轨道变化，不把热点绑定到固定卫星 ID。
该地理分配是可审计实验假设，不代表测得的真实地面业务比例。

受控 F3 只用于单 victim 对照：node 9 的第一个且唯一计算任务为 LLM task 79，
计划故障时刻 2.130334420 s。该星从仿真开始正常通信/转发，直到 F3 才永久断链；
选择早期北侧轨迹和首次计算任务，不修改或屏蔽该星的 F1/F2 概率。
普通任务 280 在 F3 前以该星为 INPUT 源端，传输必须在 F3 前完成；F3 后到达的
任务不再使用该星作为端点。none 与 generate 使用完全相同的这份 TaskTrace。
F3 计划仅供离线场景和故障调度器，不能作为未来在线算法的输入。

生成入口是 `tools/generation/generate-task-workload.py --profile=n4c-hotspot`，
固定 `--seed=n4c-g3-hotspot --hotspot-weight=64 --regional-candidate-limit=1`。
完整标定命令、F1 beta、F2 冻结边界及各轮实际验收见
[G3 阶段证据](../../../../../docs/n4c/reviews/G3-hotspot-fault-calibration.md)。
这是 no-backup 输入，不包含 checkpoint、接管、任务复活或 N5 算法。

本轮冻结 beta=3，也是当前 fault-para.cc 默认值；可用 `--faultF1Beta`
（runner 的 `--f1-beta`）显式记录实验值。候选比较与限制见上述报告。
日常运行无需 `--audit`；仅概率/温度/原生位置核验时打开，输出目录必须是新目录。
