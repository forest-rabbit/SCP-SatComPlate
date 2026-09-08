# N4C G3：临时计算停机、热点与联合故障标定

状态：G3 实现、标定和独立验证完成，自检通过，等待用户审阅；未合并至 n4c/main。
依据工作区 v3 任务书及用户确认的三项账本细节。
仅本文件维护阶段证据，详细运行输出存放忽略目录，不新增重复任务书。

## 基线与执行顺序

- G2 批准提交 `9da94f067`，旧正式 C800 的 34 个失败均在到达时发生，RUNNING 中断为 0。
- [G1 PR #88](https://github.com/forest-rabbit/SCP-SatComPlate/pull/88) 普通合并为 `e49ba2039`。
- [G2 PR #89](https://github.com/forest-rabbit/SCP-SatComPlate/pull/89) 普通合并为 `8d750724a`。
- 阶段分支 `n4c` 包含两个批准阶段全部 ancestry，main 未变化，未触发 CI。
- G3 分支 `feature/n4c-g3-hotspot-fault-calibration` 从 `8d750724a` 建立。
- 仓库 AGENTS 要求功能分支提交到达 main 后才清理，因此 G1/G2 分支暂保留到 G4。

依次实现并本地验证：临时停机生命周期、影响账本、原生位置热点与 none 基线、
F1/F2 预标定、受控 F3 联合运行、独立验证及审阅证据。完成后推送 G3 并暂停，
不合回 n4c/main，不创建 tag，不进入 checkpoint/备份/恢复/N5。

## 冻结边界

C800 每任务类别/INPUT/RESULT/WU 不变；全 66 星 100,000 WU/s，1000 s，
1..600 s 到达主窗口，10 Gbps，compute deadline factor=1.3。
F1/F2 目标约 79 个不同 RUNNING victim，F3 联合场景恰好 1 个 RUNNING victim；
不以最终失败数替代直接影响数，不设事件配额，不筛选 seed。
停机期间保留队列和通信，但不采样新的重叠 F1/F2；恢复不复活已 FAILED 的任务。

| 冻结项 | 本次取值 |
|---|---|
| C800 | dense/sparse/compression/LLM=240/240/240/80，INPUT 81,750,000,000 B，RESULT 44,076,569,084 B，183,958,466 WU |
| 原生轨道 | synthetic-66，orbitStartOffset=0 s；不使用旧 F2 专项场景的 302 s offset |
| 网络 | capacity-aware HRW、size-aware chunk、fixed 8 ms、20 s 更新、1 s 链路指标；MTU 64028，队列 1.5 MB，接收缓存 131072 B |
| F1 | 仅场景 `faultF1MaxIntensity=0.2`；默认仍为 0.005，温度 base/risk/critical/saturation=17/20/30/35 C，tau_h/tau_c=43/40 s，能源项不变 |
| F2 | referenceSeuIntensityPerSecond=0.002859196111093899，rho_SF=0.5；SAA 经度[-90,5]、纬度[-50,5]，hotspot=(-60,-28)，sigma west/east/lat=12/24/12 度，NOTICE 阈值0.5，均未调整 |
| 故障检查/恢复 | 1 s / 8 s；F1 NOTICE 阈值0.6；F1/F2 仍独立抽样，联合命中只停机一次 |
| 受控 F3 | node 9、task 79、2.130334420 s；1 次永久故障，不在运行算法中暴露未来计划 |

## 生命周期与影响账本

`100615eeb` 完成队列保留；随后增加按事件/任务/影响类别去重的账本，保留同刻 F1+F2
来源，START 与 impact 两种时间，取消前真实 WU 进度及 deadline，最终关联任务结果。
状态未在 START 采集时明确 NOT_ARRIVED/NOT_CAPTURED，不伪造历史状态。
专项验证同刻恢复/再次停机、队列保留、新到达、INPUT 继续、FCFS、deadline 尚未建立、
重复故障影响记录、F3 原有永久规则；34 Python、15 C++ 和全部四组 regression 通过，
旧 100 任务联合用例仍为 93 完成/7 失败、82 条概率匹配。
日志 `output/n4c-g3-20260908/lifecycle-regression.log`；未重写旧 G2/N4B 输出。

## 预声明实验与首轮候选

在运行标定之前声明：ns-3 seed=1，calibration runs=11/12/13，validation runs=21/22/23；
正式输入 placement seed=`n4c-g3-hotspot`。先以 run 11 做候选 pilot，不因结果更换 seed。
参数冻结后使用未参与调参的 validation 三轮；如未达标如实报告，不丢弃不利 run。
首轮权重 4:1、区域不限候选：原生 1 秒切片的最大年龄 0.998906 s，
北美/欧洲/东亚/background 为 73/65/39/623 个任务，区域内总需求 416.36978 s，
背景 1423.21488 s。欧洲/东亚空候选发生 48/37 次，确定性回退到其余加权候选。
候选输出在 `output/n4c-g3-20260908/pilot-w4`；所有业务字段和 arrival 与 G2 相同。
受控 F3 候选：node 9、task 79（最早到达 LLM）、2.130334420 s；该星首次任务前无
计算负载且原生北侧轨迹不在 SAA，故障不是用专属概率屏蔽构造，仍需联合运行验证。
controlled F3 合法性、精确 ns、未知节点/越界拒绝及全部 37 Python/15 C++/6 smoke 通过。

## Hotspot none 对照

启动代码 `0da49fcc1`、干净工作区、seed/run=1/11。使用现有 run-n4c-baseline.py，
`--task-trace=output/n4c-g3-20260908/pilot-w{4,64}/task-trace.json`、`--run=11`，
输出 `pilot-w{4,64}-none`；每轮 execution.json 保存完整命令。
两者均 800/800 成功、1600/1600 完成、零丢包、无 deadline 超时、末端账本归零；
summarize-n4c-baseline.py 和 summarize-n4c-g3.py 均通过。

| 候选 | 区域内任务 | 最忙节点 busy（s） | queue 均值/P95/最大（s） | 墙钟（s） |
|---|---:|---:|---|---:|
| 4:1、区域全候选 | 177 | 106.09398 | 0.13615 / 0 / 14.22127 | 444.73 |
| 64:1、每区域最近中心 1 候选 | 601 | 393.58089 | 4.63601 / 17.13602 / 31.48700 | 459.24 |

第二候选区域任务北美/欧洲/东亚/background 为 217/186/198/199。
这是显式改变计算分配集中度，不改变时间/WU/算力/带宽；两候选及结果全部保留。
随后在二者上以 run 11、原 F1=0.005 和原 F2 跑 generate（F3 关闭、audit 开启），
从真实 RUNNING 中断和模型状态确定标定幅度。

原参数 pilot（启动 `ca5964080`，干净工作区）结果：4:1 的 F1/F2 event=0/2，
direct RUNNING=0/1，峰值温度 26.19 C；64:1 为 51/2，direct=48/0，峰值 30.11 C。
两组所有 F2 START 都在 SAA 且模型概率大于零；不将空闲故障计入 direct target。
选用 64:1，预声明下一轮 run 11 的 F1 强度候选 0.1 / 0.3 每秒；其余物理和业务参数
保持不变。原 F1 在 critical 温度强制故障，调大随机强度也会提前中断/降温，
故 direct 数不按强度简单线性外推，必须以完整运行验证。

F1=0.1 / 0.3 两个完整 pilot（均 run 11、F3 off、启动 `d115c3e57`）分别得到
75/98 次 F1、2/2 次 F2，unique F1/F2 RUNNING 为 70/0 和 93/0；均无额外失败。
选择两候选间的简单中值 **F1=0.2 每秒**，不拟合到恰好 79。以此候选进行联合
calibration 11/12/13，并额外重复 11（audit off）；之后才查看 validation 21/22/23。
正式输入位于 `input/examples/leo-66-1000s-n4c-g3`，与已验收 w64 none 输入逐字节一致。
新控制 F3 查询测试已经重新编译并通过；此前错误短目标名触发的旧二进制运行
不作为该新增断言证据。完整 37 Python、15 C++、6 smoke、4 regression 再次通过。

联合 calibration 11/12/13（启动 `1cdd8f8bc`，干净工作区）direct F1/F2 为
76/0、77/0、81/0，联合均值 78，样本 SD=2.64575，min/max=76/81。
各轮 F3 均且仅中断 node 9 的 task 79；compute START=1.138335016 s，
F3=2.130334420 s，无故障完成应为 9.755335016 s，实际进度 11.51201%。
重复 run 11 的 audit on/off 共 18 项正式证据一致，正常输出无任何 audit 文件。
据此冻结 F1=0.2，其余参数不变；之后启动 validation 21/22/23，不用其结果回调参数。

## 三本账：联合标定与独立验证

以下每行是一个完整 run；F1/F2 START 包含空闲故障，direct 只统计去重后的 RUNNING
中断。risk-only 同时区分自然 CLEAR 与仿真终点截断，二者都不是实际故障。

| 组/run | F1/F2 START | F1∪F2 direct | F3 direct | 成功/失败 | RESULT 取消 | risk-only（CLEAR+截断） |
|---|---|---:|---:|---|---:|---|
| calibration/11 | 84/2 | 76 | 1 | 723/77 | 77 | 7（6+1） |
| calibration/12 | 88/1 | 77 | 1 | 722/78 | 78 | 6（5+1） |
| calibration/13 | 87/0 | 81 | 1 | 718/82 | 82 | 6（5+1） |
| validation/21 | 86/3 | 80 | 1 | 719/81 | 81 | 6（5+1） |
| validation/22 | 87/3 | 81 | 1 | 718/82 | 82 | 8（7+1） |
| validation/23 | 88/2 | 84 | 1 | 715/85 | 85 | 8（7+1） |

| 组（n=3） | direct 均值 / 样本SD / min–max | F1 START 均值 / SD / min–max | F2 START 均值 / SD / min–max |
|---|---|---|---|
| calibration | 78 / 2.64575 / 76–81 | 86.33333 / 2.08167 / 84–88 | 1 / 1 / 0–2 |
| validation | 81.66667 / 2.08167 / 80–84 | 87 / 1 / 86–88 | 2.66667 / 0.57735 / 2–3 |

validation 启动于 `81cf04170`，干净工作区；其均值比目标79高3.38%，保留自然波动，
没有回调参数或弃选run。六轮整体均值79.83333仅作描述，不替代独立validation结果。
六轮无同刻 F1+F2 双命中；F2 direct 均为0，共有11次实际空间故障，均在SAA且pF2>0。
不得把这一北半球热点输入解释成已经具备充分的 F2 备份收益验证能力。
六轮均无 deadline 超时、网络/RESULT 故障、任务仿真截断、FlowMonitor 丢包或队列丢包；
1600 个传输中的 INPUT 全部完成，取消的是被中断任务尚未执行的 RESULT。
每轮故障重路由均为1次，来自 F3；F1/F2 不断链。

F3在六轮的目标/时间/进度完全一致，均无额外active/queued victim或故障后坏端点。
概率审计 calibration=219/157/142、validation=181/199/100，共998条全部匹配，
F1、F2、q_comp与完成前概率的MAE/RMSE/最大绝对误差均为0，无缺失或上下文错配。
这是同模型的概率一致性证据，不是基于是否发生故障的分类准确率。

## 代表轮 11 的影响与负载

该轮事先选定用于图表，不因结果最接近79才选。87次实际 START（84 F1+2 F2+1 F3），
77 个直接中断任务，442 个存在任意可观察影响的不同任务；最终77失败、723成功。
另有7个risk-only episode：6个自然CLEAR、1个在1000 s终点截断，均未成为实际故障。
间接影响涉及421个任务，与直接 victim 有交集，不能相加当作总数。
其中 QUEUED_DELAYED=814行/420任务，新到达=183行/183任务，INPUT继续=3行/3任务。
同一任务遇到多次停机保留多行；只等过停机的任务并不因此 FAILED。
相同任务的 queue delay 对照 none：均值增加12.72390 s，中位数5.85275 s，
P90=36.24629 s，最大68.17322 s；不是统一增加8 s。

| 到达时区域 | 任务数 | WU | none 计算需求(s) | generate 实际busy(s) | generate 平均queue(s) |
|---|---:|---:|---:|---:|---:|
| 北美 | 217 | 55,340,607 | 553.40607 | 422.56263 | 20.06664 |
| 欧洲 | 186 | 41,985,292 | 419.85292 | 354.90836 | 30.29594 |
| 东亚 | 198 | 43,180,679 | 431.80679 | 373.70179 | 19.58220 |
| background | 199 | 43,451,888 | 434.51888 | 434.51888 | 0.10625 |

596个任务选中了被加权的最近候选，601个任务的计算节点位于三个矩形内；二者不是
同一统计。逐任务位置、native slice时刻及 fallback 见提交的 placement manifest。
全66节点 generate busy 均值/中位数/最大为24.02563/8.03634/335.47589 s；
全程网络可用链路时间加权利用率0.18913%，不是 capacity-aware 的剩余可准入带宽。

F1 direct 的 dense/sparse/compression/LLM=15/6/23/32，全部最终FAILED；
到达时区域北美/欧洲/东亚=31/24/21。F2 没有直接 victim，因此不伪造其进度分布。
F3 是1个LLM，INPUT=715 B、WU=861700、进度11.51201%、deadline slack=10.21010 s。

| F1 direct 分布（76个任务） | min | P10 | P50 | P90 | max |
|---|---:|---:|---:|---:|---:|
| INPUT (B) | 377 | 429 | 59,434,304 | 500,000,000 | 1,000,000,000 |
| WU | 46,614 | 91,654 | 642,350 | 962,300 | 1,500,000 |
| progress (%) | 0.66062 | 3.13862 | 48.44196 | 91.45999 | 96.18709 |
| deadline slack (s) | 0.26103 | 0.72289 | 3.79334 | 10.25334 | 19.25232 |

其他 run 的相同分布、最终结果交叉表保存在各自 `g3-summary.json/by_fault_source`，
原始 `fault-task-impact.csv` 可按 `(fault_id,task_id,impact_type)` 追溯；WU progress
使用128-bit中间量的整数向下取整，单位WU，不用排队/传输时间冒充计算进度。

## 实际数据图与复现

![66节点负载与温度](figures/g3/load-thermal.png)

左：全部66节点的G2/G3 none busy配对，虚线为相等；右：代表generate轮的实际
利用率与模型峰值温度，三角表示观察到F1 START。它们说明模型内的负载关联，
不宣称真实航天器温度或失效率已被外部数据验证。

![原生SAA暴露与实际F2事件](figures/g3/f2-native-exposure.png)

全部5015个SAA内node-second采样直接着色，星号为该轮两次实际F2 START，
色条是模型空间风险，不是故障密度。使用经度[-95,10]/纬度[-55,10]的明确局部窗口；
没有插值、平滑、人工加计数或删除低概率点。PDF/SVG同名文件与600 dpi PNG一并保留。
字体最小7 pt，两个PDF的字体/碰撞审计和双panel的1.5 pt对齐门禁通过，已人工看图；
静态预检仅保留“没有TIFF投稿稿”的提示，本阶段交付矢量图和审阅PNG，不声称符合特定期刊投稿要求。
逐点CSV、provenance、QA结果位于 `output/n4c-g3-20260908/figures`。

实际使用的主命令（在仓库根目录、激活项目uv环境后；输出目录必须不存在）：

```bash
python contrib/satcompute/tests/integration/regression/run-n4c-baseline.py \
  --task-trace=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3/task-trace.json \
  --hotspot-manifest=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3/placement-manifest.json \
  --fault-mode=generate --f1-intensity=0.2 --run=11 --audit \
  --output-dir=output/n4c-g3-20260908/calibration-11
```

其余calibration/validation只改变预声明的run和输出目录；`repeat-11-audit-off`省略audit。
none省略故障/强度/F3选项，沿用同一份TaskTrace；位置切片由
`satcompute --topologyOnly=1 --simulationDuration=1000 --topologySliceInterval=1`生成。
生成输入的完整参数见输入目录README；每轮`execution.json`保存完整平台命令、HEAD、
seed/run和启动时工作区状态，`execution-result.json/time.txt/run.log`保存退出状态、耗时。
联合标定每轮490.03/496.68/495.57 s，独立验证459.17/449.14/471.80 s；
audit-off重复488.75 s。此前0.005/0.1/0.3及4:1候选均保留在同输出根目录的`pilot-*`。
测试日志为同输出根目录的`final-{build,python,cpp,smoke,regression}.log`。

汇总命令使用 `tools/validation/summarize-n4c-g3.py --run-dir=... --manifest=... \
--none-dir=output/n4c-g3-20260908/pilot-w64-none --expect-f3`；概率核对使用相邻的
`compare-fault-probabilities.py`，重复核对输出为`repeat-comparison.json`。审计器还经过
内存中故意修改WU进度的负向检查，能够拒绝错误；未修改原始CSV。

## 边界与待审阅事项

- 地理权重和F1=0.2是本次66星实验压力设定，不是由实测航天器故障率拟合而来；
  F2模型/参数/原生坐标未变化，也不通过反推F1/F2配额生成事件。
- 每组只有3轮，报告样本SD而非精确现实概率或充分的尾部置信保证。
- F3单victim由离线工作负载构造；不存在victim概率屏蔽，但不能据此推断随机碎片事件
  通常只影响一项任务。该星故障前仍正常转发，故障时立即断链/重算路由。
- 同任务曾经排队受阻后又成为direct victim是合法的；event/impact/terminal三本账不得混用。
- 相同配置和执行可重复，不等于未来不同N5算法必然生成相同故障序列：备份可改变
  busy/热状态，停机又影响后续抽样资格。G3不添加replay或新的随机数方案掩盖这种内生性。
- 本阶段只修正服务恢复后的FCFS，不恢复失败任务；没有checkpoint、L1/batch/tail、
  融合、接管或任何N5备份算法。G3推送后等待用户审阅，不合并、不打tag、不进入G4/N5。
