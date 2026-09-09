# N4C G3：F1 概率/热恢复与 F3 普通参与修订

## v3 修订预声明（2026-09-09）

正在按工作区 Codex_N4C_G3_Final_Revision_BetaGamma_Hotspot_F3_v3.md 执行。
下方 beta=3 的结果暂时仅作上一轮历史，不作为 v3 正式证据。仍在同一 G3 分支，
不跑 CI、不合并、不进入 G4/N5。输出另存 output/n4c-g3-v3-20260909，不覆盖历史。

- 增加 heatingShapeGamma，采用 dT/dt=k*(35-T)^gamma 的闭式推进；k 从 17→30°C/30s
  派生，不独立调参。冷却和所有故障生命周期保持原合同。gamma=1 仅用于兼容/单元参考。
- seed=1、run=11、原 hotspot=64 输入、F3 off：只跑 (beta,gamma)=(8,1.5)/(8,2)/(10,1.5)/(10,2)
  四个 C800 pilot。按 START 温度/pF1/busy/recovery 选择，不按 direct 数选；相当时优先温和形状。
- 冻结 beta/gamma 后仅筛 hotspot=64/96/128，regional limit=1；run11 仅初筛。
  每个正式候选先按真实 none 时序构造新 F3，再跑 final none 与 calibration=11/12/13。
  目标均值 75–83；低于75才继续提高，超过83停止加权，到128不达标也停止扩搜、如实报告。
- F3 候选 INPUT>200MB、实际 WU progress>50%，优先 none 60–80%；按业务时序/stable hash
  选点，移除旧北部/SAA规避筛选。先前普通节点参与、单victim与故障后排除均须实际验证。
  正式run若被F1/F2提前中断或不满足进度，记验收失败，不换seed/victim/time或概率屏蔽。
- 全部冻结后首次运行 validation=31/32/33；旧21/22/23只作历史，不再称新held-out。
  正常审计默认关闭；正式证据包括逐概率匹配、audit on/off重复、全部维护回归及两张实际数据图。

实现首段已通过模块/入口/工具编译、37个Python测试及15个C++测试。
新增检查覆盖gamma=1兼容、1.5/2与近1极限、30秒端点、前快后慢、分段非整数更新、35°C渐近边界；
统一线性冷却/概率周期/只读查询/同刻双来源及F3抢占回归通过。四组pilot将从此提交的干净工作区启动。

## 上一轮 beta=3 证据（待本轮结果原地更新）

状态：v2 代码修订和本轮实验已完成，**停在 G3 审阅，尚未批准进入 G4**。
beta=3 的标定组 F1∪F2 直接中断均值为 62.67，验证组为 64，均低于约 79 的目标；
模型、生命周期和概率一致性检查通过，不等于故障数量标定已达标。
不合并 n4c/main、不跑 CI、不进入 G4/N5。只更新本报告及既有图，不新增主报告。

## 冻结配置与模型

- C800 类别 dense/sparse/compression/LLM=240/240/240/80；INPUT=81,750,000,000 B，
  RESULT=44,076,569,084 B，WU=183,958,466；逐任务业务属性与 G2 到达时刻不变，只改端点。
- 66 星全部 100,000 WU/s，1000 s，orbitStartOffset=0，10 Gbps，fixed 8 ms，
  capacity-aware HRW、size-aware、20 s 网络更新、1 s 指标/故障检查。compute deadline
  在首次 RUNNING 时建立，factor=1.3；INPUT/首次排队/RESULT 不占 compute deadline。
- 地理热点权重 64、每区域一个最近中心候选。受控 F3 为 node 9、task 79、
  2.130334420 s；普通 task 280 在此前真实使用 node 9 源端，不屏蔽该节点 F1/F2。
- F2 空间函数及全部参数不变：referenceSeuIntensityPerSecond=0.002859196111093899，
  rho_SF=0.5；SAA 经度 [-90,5]、纬度 [-50,5]、中心 (-60,-28)，东西/纬度尺度 12/24/12 度。

F1 默认基础/风险/临界/热平衡温度为 17/20/30/35°C，最终 beta=3：

```text
tau_h = 30 / ln(18/5) = 23.4204132448 s
T_next = 35 - (35-T) * exp(-dt/tau_h)                  # busy
T_next = max(17, T - 3.25*dt)                          # 所有 non-busy
pT = 0                                                # T <= 20
pT = expm1(beta*(T-20)/10) / expm1(beta)                # 20 < T < 30
pT = 1                                                # T >= 30
pF1_1s = min(1, pT*(1 + 0.1*energyPressure))
q_comp = 1 - (1-pF1)*(1-pF2)
```

参数集中在 [fault-para.cc](../../../contrib/satcompute/fault/fault-para.cc)：
新增 heatingToCriticalSeconds=30、coolingFromCriticalToBaseSeconds=4；
tau 和冷却速率为派生量。移除旧 coolingTau、lambdaMax/faultF1MaxIntensity；
beta 可用 faultF1Beta 覆盖。pF1 是**当前参考 1 秒的条件概率**，不是历史累计首次故障概率。
检查周期变化时按 `1-(1-pF1_1s)^dt` 换算，物理 elapsed time 与抽样周期分开。

状态按上一 busy 状态精确推进至任务 START/COMPLETE、故障/恢复及检查时刻；
同刻 FCFS 交接不制造冷却间隙。能源仅乘性修正，低温不独立致故障，DoD 不在恢复时重置。
F1 START 的时长为 (T_start-17)/3.25 秒、向上取整到 ns；检查点临界过冲钳位到 30°C，
有效时长 (0,4] 秒。恢复靠实际线性冷却完成，不直接重置温度。
F2 的 recoveryDurationSeconds=8 独立配置；同刻 F1/F2 分别抽样、来源均保留，
但只产生一次停机，时长取 max(D_F1,D_F2)。停机内不抽新的 staggered F1/F2。

生产移除 NOTICE/NOTICE_CLEAR、RiskEpisode、F1 riskThreshold、notice/warning/riskDuration
字段、risk-only trace 及专属测试。F2 spatialRiskThreshold=0.5 保留，**仅用于独立空间暴露/
绘图的高风险边界分类**，不门控抽样或查询；旧任务生成器的 risk_only 只是历史角色标签。
START 记录实际当次 pF1/pF2/q_comp、温度、continuous_busy_s；RECOVERY 行复用 START
元数据，不表示恢复时温度/概率。完整行为见 [fault README](../../../contrib/satcompute/fault/README.md)。

## 运行顺序与 beta 选择

输出根目录为 `output/n4c-g3-revision-20260909`。在新模型实验前固定 seed=1；
依次完成 **beta=4、beta=3** 的 run 11 / F3 off 全 C800 pilot。两者均保留原始结果：

| pilot | F1 事件 | F2 事件 | F1∪F2 不同直接 victim | 完成/失败 | 匹配概率行 |
|---|---:|---:|---:|---:|---:|
| beta=4 | 56 | 2 | 54 | 746/54 | 1728 |
| beta=3 | 71 | 2 | 67 | 733/67 | 1715 |

beta=4 的 START 温度 min/median/max=20.7903/24.9334/27.6077°C，
pF1=0.6937/11.5629/37.2586%；恢复 1.1662–3.2639 s，中位 2.4410 s；
同节点相邻 START 最短 8 s、中位 20.5 s。beta=3 对应分布见下表（与正式 run 11 一致）。

选择 beta=3：同温度下中温概率更高，已测候选中直接中断更接近目标，分布无明显单点集中，
也符合用户倾向。pilot 67 仍低于 79，不能把 71 次 F1 事件当成 71 个直接失败任务。
beta=5/6 **仅做纯模型曲线与单元测试，未运行 C800 pilot**；未增加额外概率缩放、修改任务或挑选 seed。

正式冻结提交为 `b864fa346717d846691c9a9c821947a78dd1d1c0`。
calibration=11/12/13、held-out validation=21/22/23、run 11 audit-off 重复均从此提交和
干净工作区启动，并启用同一受控 F3。validation 在冻结后首次查看，不反向调参；
calibration run 11 被 pilot 使用过，不称为 held-out。代表图预先固定 calibration run 11。
冻结后仅补测试容差/检查及报告，不修改正式模型或输入。

## 新 none 与 F3 真实参与

重新生成的 [G3 输入](../../../contrib/satcompute/input/examples/leo-66-1000s-n4c-g3/)
实际运行 none：800/800 任务和 1600/1600 传输完成，零丢包/超时/截断、末端账本归零；
最忙节点 busy=393.58089 s，平均排队 4.63599 s，最后任务 615.193805 s 完成。
平均可用链路利用率 0.197929%，聚合 IP 吞吐量 1640.828426 Mbps。none 墙钟 491.33 s。

普通 task 280 在 **1.757165882 s** 到达，node 9 为 INPUT 源，
**1.812829526 s** 完成 INPUT，距 F3 尚有 **0.317504894 s**，此后不再依赖该端点。
none 和所有正式 generate 均验证这一实际时序，不只是候选列表允许使用。

task 79 在 **1.138335016 s** 进入 RUNNING，无故障计算完成时刻为 **9.755335016 s**；
F3 在 **2.130334420 s** 命中，进度 **11.512011%**，恰好一个 RUNNING victim。
无额外 QUEUED/INPUT/RESULT victim，故障后到达任务无 node 9 静态端点。
整星立即失效并触发一次路由重算；F1/F2 不关闭链路。
这是受控 single-victim benchmark，不代表随机碎片故障通常只影响一个任务。

## 正式联合结果：事件、直接影响与终态分开

各轮均为 800 任务。F1/F2 事件数包含发生时没有 RUNNING 任务的故障；
F1∪F2 direct 为不同 RUNNING 任务数。六轮 F2 direct 均为 0，无同刻双来源命中
（双来源边界另有确定性运行测试），每轮均恰好一个 F3 事件和一个 F3 direct。

| 组/run | F1 事件 | F2 事件 | F1∪F2 direct | F3 direct | 完成/失败 | 匹配概率行 |
|---|---:|---:|---:|---:|---:|---:|
| calibration 11 | 71 | 2 | 67 | 1 | 732/68 | 1708 |
| calibration 12 | 64 | 1 | 63 | 1 | 736/64 | 1698 |
| calibration 13 | 60 | 0 | 58 | 1 | 741/59 | 1710 |
| validation 21 | 64 | 3 | 63 | 1 | 736/64 | 1711 |
| validation 22 | 67 | 3 | 66 | 1 | 733/67 | 1703 |
| validation 23 | 65 | 2 | 63 | 1 | 736/64 | 1703 |

F1∪F2 direct 标定均值 **62.67**、样本标准差 **4.51**、范围 58–67；
验证均值 **64.00**、样本标准差 **1.73**、范围 63–66。
分别低于目标 79 约 **20.68% / 18.99%**，**不宣称数量标定达标**。
保留 beta=3 及冻结物理参数，差距交 G3 审阅决定是否接受；未根据验证集继续搜索。

所有失败均能关联到实际直接中断，没有额外 deadline/network/endpoint 失败；
800 个 INPUT 均完成，取消项是失败任务尚未开始的 RESULT。无丢包、链路队列丢弃或截断，
capacity/size-aware 预留和链路队列末端清零，每轮恰好一次故障路由重算。

代表 run 11：74 次 START（71 F1 + 2 F2 + 1 F3），68 个直接失败任务；
1532 个 transfer 完成、68 个取消。逐任务影响共涉及 273 个不同任务：
QUEUED_DELAYED 235 行/214 个任务、outage 期间到达 53 个任务、
INPUT 继续 3 个任务。间接影响涉及 217 个任务，可能与直接 victim 重叠，不能相加。
相同任务对比 none 的平均排队差为 -0.786218 s：失败任务剩余工作被丢弃，可能减轻后续
整体排队，不能据此否认单次 outage 中已经记录的等待。平均可用链路利用率为 0.191871%。

## 代表轮 F1 故障时状态

取预声明 calibration run 11；前四行是 71 次 F1 START，间隔是同节点 59 对相邻 START；
任务量/进度只统计 67 个直接 RUNNING victim。分位数线性插值。

| 量 | min | P10 | 中位数 | P90 | max |
|---|---:|---:|---:|---:|---:|
| START 温度 °C | 20.7984 | 21.5672 | 24.3905 | 26.7356 | 27.6446 |
| 当前步 pF1 % | 1.4180 | 3.1450 | 14.3183 | 34.2852 | 46.6764 |
| 连续 busy s | 0 | 5.5510 | 11.9361 | 17.9819 | 20.9599 |
| 动态恢复 s | 1.1687 | 1.4053 | 2.2740 | 2.9956 | 3.2753 |
| 相邻 START 间隔 s | 8 | 11 | 16 | 35.4 | 154 |
| 直接 victim 完成度 % | 0.9665 | 17.6219 | 58.2647 | 94.5714 | 99.0917 |
| 直接 victim INPUT B | 325 | 455 | 86,124,928 | 1,000,000,000 | 1,000,000,000 |
| 直接 victim WU | 59,125 | 116,176.2 | 706,700 | 1,500,000 | 1,500,000 |
| 故障时 deadline 余量 s | 0.3675 | 0.7406 | 3.1314 | 9.3805 | 19.1239 |

busy=0 对应刚转 idle 后尚有余热的事件，不伪造 RUNNING victim。
直接 F1 类别 dense/sparse/compression/LLM=12/15/17/23；
北美/欧洲/东亚/背景=29/17/17/4。F3 victim 为 LLM，INPUT=715 B、WU=861,700。
每秒独立抽样可在 pF1 尚低于 50% 时提前命中；本轮不把多数 START 超过 50% 作为验收条件。

## 概率、生命周期与回归证据

- 6 轮共 **10,233** 行：真实抽样前状态与独立无 RNG 审计状态的 pF1/pF2/q_comp/
  P_fail_before_finish 全部匹配，四项 MAE/RMSE/max absolute error 均为 0，
  无缺失行或任务上下文错配。它证明同模型概率实现一致，不代表真实世界预测准确率为 100%。
- QueryComputeRisk 只读、无 RNG/状态副作用、无需 NOTICE 或 CSV；
  任务概率审计覆盖所有可计算节点的 RUNNING 任务，保留 START 当次抽样前记录，
  故障后或任务结束后停止。查询未来窗口与任务剩余窗口语义详见 fault README。
- run 11 audit on/off 的 **18 项正式业务证据相同**（run-summary 排除墙钟字段），
  audit-off 无四个可选概率/状态输出；正常运行不会开启校验 CSV。
- 六轮共 **694** 个实际停机期状态点符合线性冷却；单元测试另外验证非整数秒恢复自然到
  17°C、恢复后 FCFS、临界过冲最多 4 s、同刻 F1/F2 取 max 且仅一次直接中断、
  F3 抢占后旧恢复事件不能复活整星。0.25/1/2 s 概率周期换算不改变物理温度。
- beta=3 重编译后 **37 Python、15 C++、6 smoke、4 regression 全通过**；
  100 任务功能回归为 82 完成/18 失败、460 行概率一致；F1-only/F2-only/联合小例分别
  175/136/90 行一致。G1/G2 冻结业务、deadline、FCFS 和正常关闭审计的回归均保留。
- 后续共用审计断言采用既定 1e-12 容差，允许单步 log-survival 往返约 1e-17 舍入，
  仍严格检查概率在 [0,1]；模型和实验数据未变。已重查全部 7 轮正式/重复数据并复跑
  lifecycle、100 任务联合回归及新增概率周期单元测试。

## 实际数据图与复现

![负载与热状态](figures/g3/load-thermal.png)

左：全部 66 个配对节点的 G2 none 与新 G3 none busy time；右：新 calibration run 11
全部节点计算利用率与峰值温度，标记实际 F1 节点。这是单轮逐节点原始观测，不是 seed 均值。

![原生 F2 暴露](figures/g3/f2-native-exposure.png)

从新 run 的 64,937 条状态中取全部 5,015 个 SAA 节点秒观测，标记两次实际 F2；
颜色表示空间风险，不是故障密度。未平滑、补点或修改次数。
两图均重新导出 PNG/PDF/SVG，字体最小 7 pt、碰撞检查零失败/警告、双面板对齐通过，
已逐面板及整图检查。源点 CSV、provenance、QA 在输出目录 figures/，不提交批量原始日志。
静态预检只有未提供 TIFF 的提示；本轮交付为审阅用 PDF/SVG/PNG。

在仓库根目录使用项目 uv 环境，下面复现代表轮（output 目录必须不存在）：

```bash
source .venv/bin/activate
./ns3 build satcompute
python contrib/satcompute/tests/integration/regression/run-n4c-baseline.py \
  --output-dir=output/n4c-g3-reproduce/calibration-11 \
  --fault-mode=generate --audit --seed=1 --run=11 --f1-beta=3 \
  --task-trace=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3/task-trace.json \
  --hotspot-manifest=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3/placement-manifest.json
python contrib/satcompute/tools/validation/summarize-n4c-g3.py \
  --run-dir=output/n4c-g3-reproduce/calibration-11 --expect-f3 \
  --manifest=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3/placement-manifest.json
```

none 使用同一 task-trace、改为 --fault-mode=none 并去掉 --audit；
pilot 增加 --disable-f3，依次 --f1-beta=4/3；正式六轮只改 run/输出目录；
audit-off 重复去掉 --audit。已有 compare-fault-probabilities.py 比较逐行概率，
summarize-n4c-g3.py 的 --none-dir 可增加同任务排队差对照。
每轮 execution.json 记录完整命令/提交/工作区状态，execution-result.json 记录退出码与耗时；
汇总在输出根目录 aggregate-verification.json。pilot 墙钟分别 467.25/428.38 s；
冻结后 7 轮并行，单轮约 671–691 s，各约 100 MB 峰值 RSS，不构成普遍性能承诺。

## 审阅边界

相对旧审阅点 7b4ad4889：2b4d93c0e 实现模型/NOTICE/F3 修订；
28a5a83a6 补真实冷却测试并记录 beta=4；b864fa346 冻结 beta=3；
最后补充本报告、既有图和数值边界测试，不改冻结模型/输入。
旧 lambdaMax=0.2 结果留在 Git 历史及 output/n4c-g3-20260908，不作为本次通过证据。

本轮明确遗留数量目标差距，是否接受仍需审阅，不能写为已达约 79。
六轮共 11 次真实 F2 事件但无 F2 direct，本北部热点负载不能验证 F2 下的备份收益；
不人为提高 F2 配额。每组仅三轮，这些加速模型结果也不是实测航天器故障率。
本轮未实现任何 checkpoint、备份、任务恢复或 N5 算法，停在 G3 审阅。
