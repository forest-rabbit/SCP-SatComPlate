# N4C G3：F1 升温形状、热点标定与大任务 F3

状态：109GB派生修订、有限筛选和六轮验证已完成，**未通过G3整体验收，停在G3审阅**。
109GB确认/验证direct均值70.33/73.67，仍低于75–83；五轮F3单受害任务检查失败，
其中run42还出现95个在途UDP丢包和1个RESULT未完成。只有run41满足固定压力场景资格。
不进入G4/N5，不跑CI、不合并、不打标签；本轮只更新本报告和既有两张图。
下列81.75GB v3结果保留为历史基线（确认/验证均值48.33/51），不改写G1。
旧原始结果在 `output/n4c-g3-v3-20260909`，新结果在 `output/n4c-g3-109g-20260909`。
旧 beta=3、validation 21/22/23 保留在 Git 历史和
`output/n4c-g3-revision-20260909`，不再作为本轮正式或 held-out 证据。

## 冻结业务与模型边界

- 66 星全部 100,000 WU/s，1000 s，orbitStartOffset=0；10 Gbps、fixed 8 ms，
  capacity-aware HRW、size-aware，20 s 网络更新、1 s 指标/故障检查。
- C800 dense/sparse/compression/LLM=240/240/240/80；INPUT=81,750,000,000 B，
  RESULT=44,076,569,084 B，WU=183,958,466。逐任务类别、字节、WU、到达时刻不改，只改端点。
  compute deadline 在首次 RUNNING 时建立，factor=1.3；INPUT/首次排队/RESULT 不占该时限。
- F2 不变：referenceSeuIntensityPerSecond=0.002859196111093899、rho_SF=0.5；
  SAA 经度 [-90,5]、纬度 [-50,5]、中心 (-60,-28)，东西/纬度尺度 12/24/12 度。
- F1/F2 中断 RUNNING，QUEUED 保留，INPUT/新到达继续，RESULT 不受 compute-only fault 影响；
  恢复后按 FCFS 继续，不复活失败任务。F3 永久断星并立即重算路由。
  不恢复 NOTICE/NOTICE_CLEAR/RiskEpisode；QueryComputeRisk、START 概率及任务影响账本保留。

F1 基础/风险/临界/热平衡温度保持 17/20/30/35°C，冻结 **beta=10、gamma=1.5**：

```text
dT/dt = k_h * (35-T)^gamma                            # busy，闭式按实际 elapsed 推进
k_h = (5^(1-gamma)-18^(1-gamma)) / ((gamma-1)*30)      # gamma > 1
k_h = ln(18/5)/30                                    # gamma = 1，仅兼容参考
T_next = max(17, T - 3.25*dt)                         # 所有 non-busy
pT = 0 / expm1(beta*(T-20)/10)/expm1(beta) / 1         # T<=20 / 20<T<30 / T>=30
pF1_1s = min(1, pT*(1+0.1*energyPressure))
q_comp = 1-(1-pF1)*(1-pF2)
```

k_h 自动保证 17→30°C 恰好 30 s，不独立调参；gamma=1.5 时为 0.014100755673629473。
从当前温度推进，不从任务年龄重新升温；非整数 elapsed 和同刻 FCFS 衔接不制造冷却间隙。
pF1 是当前参考 1 s 的**条件概率**，不是历史累计首次故障概率；其他检查步长用
`1-(1-pF1_1s)^dt` 换算。临界过冲钳位到30°C，F1 恢复时长 (T_start-17)/3.25 向上取整到 ns，
在 (0,4] s 内；真实线性冷却至17°C，不在恢复时 reset。F2 恢复仍为8 s。
同刻 F1/F2 分别抽样、来源都保留，只形成一次停机且时长取 max；停机内不抽新故障。
公式、参数和 API 详见 [fault README](../../../contrib/satcompute/fault/README.md)。

## 四组 pilot：先冻结形状，不按故障数选择

固定 seed=1、run=11、原 w64 TaskTrace、F3 off；全部从干净提交 `5af0d0cef` 启动，
墙钟492–501 s。原输入保留是为了四组比较时不改变 placement。

| beta/gamma | F1 START | F1∪F2 direct | F2 START/direct | 温度22–25°C / 29.9–30°C次数 | 匹配概率行 |
|---|---:|---:|---:|---:|---:|
| 8/1.5 | 42 | 42 | 2/0 | 4/0 | 1753 |
| 8/2 | 53 | 51 | 2/0 | 6/0 | 1753 |
| 10/1.5 | 36 | 35 | 2/0 | 0/0 | 1777 |
| 10/2 | 43 | 43 | 2/0 | 1/0 | 1758 |

下表每格依次为 **min/P10/P50/P90/max**；pF1 已转为百分数，分位数线性插值。

| beta/gamma | START 温度 °C | 当前步 pF1 % |
|---|---|---|
| 8/1.5 | 23.438/25.174/27.397/28.434/29.293 | 0.492/2.075/12.472/28.558/56.770 |
| 8/2 | 23.229/24.888/27.802/28.637/29.555 | 0.411/1.715/17.210/33.589/70.041 |
| 10/1.5 | 25.323/26.502/28.255/29.177/29.563 | 0.926/3.089/17.469/43.935/64.606 |
| 10/2 | 23.538/26.402/28.109/28.882/29.540 | 0.152/2.733/15.093/32.796/63.101 |

| beta/gamma | continuous busy s：min/P10/P50/P90/max | 动态恢复 s：min/P10/P50/P90/max |
|---|---|---|
| 8/1.5 | 8.282/11.463/17.324/21.585/25.939 | 1.981/2.515/3.199/3.518/3.782 |
| 8/2 | 0/8.129/16.419/21.057/26.606 | 1.917/2.427/3.324/3.581/3.863 |
| 10/1.5 | 0/13.892/20.982/25.253/27.398 | 2.561/2.924/3.463/3.747/3.866 |
| 10/2 | 6.582/11.956/18.409/21.557/25.939 | 2.012/2.893/3.418/3.656/3.858 |

选择10/1.5：没有22–25°C START，pF1 中位/P90最高，没有临界堆积，gamma也较2温和。
不按 direct 数选，35最少也不回调。**仍未完全达到建议的 pF1 中位20–30%、
P90约50%**，不能宣称全部形状目标达标。这些是场景目标，不是航天器实测概率。
busy=0 可对应刚转 idle 后余热，不能伪造 RUNNING victim。所选组同节点 START 间隔
min/P10/P50/P90/max=16/20.8/27/65.9/176 s。
四组共7041行四项概率全部匹配、479个实际停机冷却点通过。

## 热点筛选和预声明停止规则

曲线冻结后仅按64→96→128、regional limit=1筛选，F3 off、seed1/run11。
新版纯 placement 不预留 F3 节点，manifest 的 f3=null；因此重跑w64，不能直接沿用pilot数量。
无故障与筛选使用同一输入，每任务业务与到达保持不变。

| 权重 | 热候选任务占比 | F1/F2 START | F1∪F2 direct | none完成 | none最忙节点busy s | none排队mean/P95/max s |
|---|---:|---:|---:|---:|---:|---|
| 64 | 72.875% | 40/2 | 38 | 800/800 | 414.924 | 4.822/19.659/36.630 |
| 96 | 81.750% | 46/2 | 45 | 800/800 | 446.531 | 6.048/22.284/30.346 |
| 128 | 86.625% | 47/2 | 47 | 800/800 | 514.054 | 13.805/43.736/57.076 |

64、96明显不足，只作screen，不作为正式三轮标定。达到最后候选128后不再扩搜。
正式数量判断必须在重新构造大任务F3、final none验收后，跑calibration 11/12/13，
按不同 RUNNING direct victim 的均值判断75–83；低于75才允许进入下一预声明权重，
超过83停止增加集中度。到128仍不足也如实报告，不继续160/192或回调beta/gamma。
只有达标才称“达到压力目标的最小权重”，不能把搜索上限当作达标。

## 大任务 F3、final none 与正式运行

128仍不足，停止扩大搜索，以此上限候选完成后续正式标定并如实报告差距。
大任务F3由candidate none唯一合法候选确定：node41/task191、1GB/1,500,000WU；
none计算536.690531627→551.690531627 s，F3=547.190531627 s，对应70% WU进度。
普通task22于471.945437896 s实际使用源端node41，472.065216011 s完成INPUT并脱离。
F3只从none真实业务时序选：
INPUT>200MB、WU进度>50%，优先60–80%，固定hash/task ID打破平局；
不读概率/温度/空间风险，移除旧北侧/SAA规避规则。
检查队列、INPUT、RESULT及已到达尚未开始的RESULT依赖。F3前至少一个非victim普通任务
实际使用并脱离该节点；前缀端点与candidate none逐项相同，F3后新任务排除失效星。
正式run若目标提前被F1/F2中断、进度不足或出现额外victim，保留该run并记F3验收失败，
不换seed/目标/时刻、不屏蔽故障。F3计划只供离线构造和调度，在线算法不读取未来故障。

全部模型、输入、F3、final none、calibration冻结后，才首次运行
**held-out validation 31/32/33**；不根据验证结果反向调整参数。
代表图预声明取calibration run11，不选择最漂亮的一轮。

最终输入冻结在干净提交 **a6e034bafb1e269e6e4c6b8b7c6ebdb34a9d8687**；final none、
三轮calibration、audit-off重复及三轮validation均从此提交启动，模型/输入不再变化。
final none用时450.38 s：800/800任务、1600/1600传输完成，零deadline/endpoint失败、
零丢包/队列丢弃/截断，capacity与末端链路账本清零；最终热点占比87.25%，最忙节点
busy=514.05444 s（51.4054%）；排队mean/P95/max=13.7793/43.7357/57.0765 s，
最后任务606.335354 s完成。平均可用链路利用率0.200277%，有效传输窗口聚合IP吞吐量
1664.845408 Mbps。final none确认上述F3计算窗口及70% WU进度，没有额外端点依赖。

## 正式标定与独立验证

| 组/run | F1/F2 START | F1∪F2 direct | F3 direct | 完成/失败 | 匹配概率行 |
|---|---:|---:|---:|---:|---:|
| calibration 11 | 47/2 | 47 | 1 | 752/48 | 1711 |
| calibration 12 | 53/1 | 53 | 1 | 746/54 | 1736 |
| calibration 13 | 45/0 | 45 | 1 | 754/46 | 1705 |
| validation 31 | 49/0 | 49 | 1 | 750/50 | 1710 |
| validation 32 | 51/3 | 51 | 1 | 748/52 | 1751 |
| validation 33 | 53/3 | 53 | 1 | 746/54 | 1716 |

标定F1∪F2 direct均值 **48.33**、样本标准差 **4.16**、范围45–53，未达到75–83。
权重128只是预声明搜索上限，不是达标候选；停止扩搜，不回调beta/gamma。
31/32/33在上述标定结果确认后首次启动，验证均值 **51**、样本标准差 **2**、范围49–53，
仍低于目标，不用于继续调参。标定与验证分别较参考79少38.82%/35.44%。
六轮F2 direct均为0，没有同刻双来源（边界由确定性测试覆盖）。
六轮F3均恰好一个事件、一个RUNNING victim：compression task191，源57/计算41/结果31，
INPUT=1GB、WU=1,500,000，真实开始536.690531627 s；F3时已完成1,050,000WU（70%），
deadline余量9 s；普通task22事前源端参与时序均通过，无额外QUEUED/INPUT/RESULT victim，
整星立即永久不可用、恰好一次故障路由重算。
六轮均无额外deadline/network/endpoint失败、丢包、队列丢弃或任务截断，末端资源账本清零。

代表run11有50次START，但只有48个直接失败任务；1552个transfer完成、48个取消，
取消项均为失败任务未开始的RESULT，800个INPUT均完成。没有额外deadline/network/endpoint失败。
QUEUED_DELAYED为455行/351个不同任务；停机期到达60个任务、INPUT继续4个任务。
共376个任务出现在影响账本，直接和间接受影响集合会重叠，不能简单相加。
相同任务对比none的平均排队变化为-0.517468 s：失败任务剩余工作被丢弃，可能减轻后续
整体排队，不代表停机没有延误。代表轮平均可用链路利用率0.194681%。

## 代表轮：故障时概率、任务大小和WU完成度

均取预声明calibration11。前四行统计47次F1 START，间隔统计同节点42对相邻START；
任务行只统计47个实际RUNNING direct victim。分位数线性插值。

| 量 | min | P10 | P50 | P90 | max |
|---|---:|---:|---:|---:|---:|
| START温度 °C | 25.8338 | 26.9081 | 28.5595 | 29.1566 | 29.3546 |
| 当前步pF1 % | 1.5467 | 4.5395 | 23.6780 | 43.0283 | 52.4428 |
| continuous busy s | 1.5147 | 14.9415 | 22.3838 | 24.6501 | 26.2641 |
| 动态恢复 s | 2.7181 | 3.0486 | 3.5568 | 3.7405 | 3.8014 |
| 同节点START间隔 s | 17 | 20 | 26 | 30 | 132 |
| 直接victim完成度 % | 2.9180 | 11.2956 | 37.4927 | 83.9314 | 97.6438 |
| 直接victim INPUT B | 429 | 522.6 | 129,497,184 | 500,000,000 | 1,000,000,000 |
| 直接victim WU | 64,129 | 110,170.4 | 246,793 | 913,900 | 1,500,000 |
| deadline余量 s | 0.5037 | 0.8916 | 2.4571 | 10.2441 | 17.5092 |

F1 victim类别dense/sparse/compression/LLM=11/9/15/12，区域北美/欧洲/东亚=21/21/5。
22–25°C和29.9–30°C START均0次；温度和pF1中位符合建议，P90仍低于约50%，保留差距。
两次F2分别在263 s/node28、742 s/node17，pF2=0.0240649%/0.1243540%、pF1=0，
当时没有RUNNING任务，不能为它们编造任务大小或完成度。各轮逐条pF1/pF2/q_comp、任务
字节、WU进度、deadline余量和终态保留在fault-events.csv与fault-task-impact.csv。

## 实现与功能回归

- 已通过配置内模块/入口/工具编译、39个Python、15个C++、6个smoke及4个维护regression。
  无ns-3全局example/test、无CI。
- 新增gamma=1兼容、近1/1.5/2端点、前快后慢、非整数分段流、热平衡边界检查；
  保留概率周期换算、只读查询、恢复/FCFS、同刻双来源、F3抢占旧恢复等边界。
- 大任务F3生成器测试覆盖确定性、strict >200MB、WU进度、队列/未来RESULT、
  none前缀不变及故障后端点排除；不完整F3计划在仿真和输出创建前拒绝。
- F1/F2/联合小例概率行161/136/111全部一致；100任务联合回归88完成/12失败、
  483行概率匹配。回归不要求固定随机故障配额。
- 六轮正式运行 **10,329行** pF1/pF2/q_comp/P_fail_before_finish全部匹配，
  MAE/RMSE/max absolute error均为0，无缺失或上下文错配；**862个**实际停机冷却点通过。
- run11 audit on/off的18项正式业务证据相同（run-summary只排除墙钟时间）；关闭审计时
  四种可选概率/状态输出均不存在。正常运行默认关闭，QueryComputeRisk无需CSV。
  概率相等验证的是同模型实现一致，不是现实世界“预测准确率100%”。

## 当前109GB实际数据图与历史复现命令

![负载与热状态](figures/g3/load-thermal.png)

两图更新为109GB的预声明代表轮confirmation11，并非事后选中的run41。
左为81.75GB/109GB final none的66个配对节点busy time，两场景逐任务端点完全一致；
右为109GB run11的全部66个节点利用率/峰值温度，标记8个实际F1节点。
均为单轮原始节点观测，不是seed均值；run11的F3验收失败也保留，不挑选更漂亮的轮。

![原生F2暴露](figures/g3/f2-native-exposure.png)

从65,482条模型状态中取全部5,015个SAA节点秒观测，标出两次实际F2；
颜色是空间风险，不是故障密度。两图按科研绘图技能沿用Python流程，原地导出PNG/PDF/SVG，
不补点、平滑或修改观测值。最小字体7 pt，碰撞检查零失败/警告，双面板1.5 pt对齐通过；
各面板及整图在完整导出/100 dpi实际尺寸预览下检查通过。源点、provenance和QA在
输出目录figures/。静态预检仅无TIFF提示：本轮为审阅包，不是期刊投稿导出。

下面保留历史81.75GB代表轮的复现命令；109GB命令见文末。完成构建后，在仓库根目录
使用uv环境执行（输出目录必须不存在）：

```bash
source .venv/bin/activate
python contrib/satcompute/tests/integration/regression/run-n4c-baseline.py \
  --output-dir=output/g3-v3-run11 --fault-mode=generate --audit \
  --seed=1 --run=11 --f1-beta=10 --f1-gamma=1.5 \
  --task-trace=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3/task-trace.json \
  --hotspot-manifest=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3/placement-manifest.json
```

去掉audit即正常关闭诊断；none用新的输出目录和fault-mode=none，不能带audit。
独立验证只改run=31/32/33和输出目录。重新生成输入须先做纯placement none，再用
generate-task-workload.py的f3-from-none指定它，不能从generate状态选取目标；流程见
[生成器说明](../../../contrib/satcompute/tools/generation/README.md)。

## 109GB workload-intensity revision

预声明：只增加690个普通image任务字节，复用G1映射，保留800个ID、类别、到达、
80个LLM及30个500MB/1GB任务的逐项属性。模型、算力、网络和deadline均不改。
热点按64→96→128有限递进，none须先通过；run11/F3 off只筛选。
重新从none业务时序构造F3，显式优先冻结的500MB/1GB尾部，再dense/compression，
再60–80%WU进度；不读取风险、不屏蔽随机故障。
确认轮11/12/13，未查看独立验证41/42/43；确认均值低于75才继续增压，超过83停止增加，
128仍不足也停止。代表图固定确认轮11。
固定压力场景只在上述六轮全部结束后，从通过F3及生命周期硬验收的轮中取F1∪F2 direct最大者，
并列取最小run；全部结果保留，不扩充run pool，不把选中的最大值当均值或held-out表现。
若没有合格轮，则不冻结固定压力场景。完成后STOP AT G3 REVIEW，不进入G4/N5。

三组screen从干净提交8b8d0e4a5启动，F3 off、seed1/run11。全部none通过800任务/1600传输、
零deadline/endpoint失败、零丢包/queue drop及末端账本清空；不重跑历史81.75GB矩阵。

| 109GB权重 | 热候选任务占比 | F1/F2 START | F1∪F2 direct | none最忙busy s | none排队mean/P95/max s |
|---|---:|---:|---:|---:|---|
| 64 | 72.875% | 60/2 | 59 | 507.584 | 10.494/35.722/48.894 |
| 96 | 81.750% | 65/2 | 65 | 550.352 | 18.486/50.886/72.375 |
| 128 | 86.625% | 65/2 | 65 | 616.010 | 35.799/124.211/156.179 |

128只是搜索上限，不是已达标的最小权重；不追加160/192或回调模型。
已重新用109GB none的业务时序构造F3：16个合法大于200MB候选中，按尾部优先规则仍选中
node41/task191（1GB compression），536.690531627–551.690531627 s计算，
F3=547.190531627 s、70%WU。时刻恰与旧版相同是重新核验的结果，不是直接复用旧计划；
ordinary task22的源端实际释放时间变为472.103968016 s。

### 预算、配对与final none

新输入独立存放在 `contrib/satcompute/input/examples/leo-66-1000s-n4c-g3-109g/`：
`base-task-trace.json`是未做热点分配的业务基线，`task-trace.json`为最终placement；
`workload-summary.json`记录派生预算，`placement-manifest.json`记录位置分配与F3 none证据。
109GB是新的800任务压力变体，不等同于历史2000任务/109GB压力场景；GB/MB使用十进制。

| 类别 | 任务数 | INPUT B | RESULT B | WU |
|---|---:|---:|---:|---:|
| dense | 240 | 34,313,774,584 | 34,313,774,584 | 51,470,782 |
| sparse | 240 | 31,452,335,768 | 58,786,308 | 47,178,629 |
| compression | 240 | 43,233,843,680 | 23,453,551,934 | 64,850,871 |
| LLM | 80 | 45,968 | 2,392,496 | 61,333,200 |
| 合计 | 800 | 109,000,000,000 | 57,828,505,322 | 224,833,482 |

只改变690个普通image任务及其派生量；30个尾部ID/大小、LLM请求与token逐项不变。
LLM字节从冻结records求和，不手写参与分配；普通图像合计88,999,954,032 B，
最大242,353,107 B，仍低于300MB。G1公式重新派生WU/RESULT/K/rho/sigma/H；
全任务变量状态合计128,168,194,909 B，只是解析预算，未执行checkpoint或备份。
5%/10%/20%合法状态分割守恒通过；配对CSV和完整预算在输出目录，未改变G1历史。

| 配对量 | 81.75GB | 109GB |
|---|---:|---:|
| 总服务需求 s（100,000 WU/s） | 1839.58466 | 2248.33482 |
| 普通image大小median/P95/max MB | 86.125/161.192/167.865 | 124.114/232.701/242.353 |
| 普通image计算median/P95/max s | 1.292/2.418/2.518 | 1.862/3.491/3.635 |
| final none最忙节点busy s | 514.054 | 616.010 |
| final none排队mean/P95/max s | 13.779/43.736/57.076 | 35.925/124.211/156.179 |
| final none最后完成 s | 606.335 | 644.506 |
| 确认11/12/13 direct均值 | 48.33 | 70.33 |

总服务需求增加22.22%，而非INPUT的33.33%；LLM计算不变。最终800个任务的ID、类别、
到达和三个端点也全部逐项配对一致。新final none从干净提交
`d34afd780f29b37c5c06f0a3ac03659577671cad`启动，用时487.73 s，800任务/1600传输全完成，
零deadline/endpoint失败、零丢包/queue drop/截断，capacity及末端链路账本清零。
最终热点占比87.25%；平均链路利用率0.264616%，有效传输窗口聚合IP吞吐量2075.86 Mbps。
compute deadline从首次RUNNING起算，排队变长不等同于违反这个deadline。

### 六轮完整结果与失败证据

全部正式仿真从同一干净提交d34afd780启动。先完成确认，再首次启动验证41/42/43；
没有基于验证回调输入、模型、热点或F3。每次运行约466–485 s，原始输出均保留。

| 组/run | F1/F2 START | F1∪F2 direct | F3 RUNNING direct | 完成/失败/未完成 | 场景资格 | 概率行 |
|---|---:|---:|---:|---|---|---:|
| confirmation 11 | 66/2 | 66 | 1 | 732/68/0 | 失败：额外QUEUED victim | 2074 |
| confirmation 12 | 72/1 | 72 | 1 | 726/74/0 | 失败：额外QUEUED victim | 2088 |
| confirmation 13 | 73/0 | 73 | 1 | 725/75/0 | 失败：额外QUEUED victim | 2106 |
| validation 41 | 75/2 | 75 | 1 | 724/76/0 | 通过 | 2093 |
| validation 42 | 73/2 | 73 | 2 | 724/75/1 | 失败：额外RUNNING victim及丢包/截断 | 2073 |
| validation 43 | 73/0 | 73 | 1 | 725/75/0 | 失败：额外QUEUED victim | 2112 |

确认direct均值70.33、样本SD3.79；验证均值73.67、SD1.15，均未达到75–83。
统计包含全部预声明轮，未删除失败轮；验证组包含截断运行，只能作为完整观测结果，
不能称为“通过的独立验收”。六轮F2 direct均为0，无同刻双来源。

- 六轮191号任务都在70%WU时被F3中断，deadline余量9 s，F3均永久断星并重算路由一次。
  但11/12/13/42/43中的411号任务仍依赖结果节点41：11/12/13/43中为QUEUED，
  run42中已在节点52 RUNNING。none中411于545.023246212 s交付，距F3仅2.167285415 s；
  故障运行的排队变化使该none安全窗口失效。run41中411于546.005158589 s交付，才通过。
- run42的642号任务RESULT在547.112760574 s开始、547.179249326 s已发完，F3在
  547.190531627 s关闭中继接口时仍有包在途。1299个UDP包只收到1204个，95个丢包全部
  归因为INTERFACE_DOWN（无未知丢包），应用缺少6,036,112 B；至1000 s仍为
  RESULT_TRANSFERRING/传输SENDER_FINISHED。路由重算不能重传既有UDP丢包。
  本轮没有新增重传/超时机制，也没有把未完成任务改记成失败或完成。
- run11的1532个transfer完成、68个取消；run42为1524完成、75取消、1个未完成。
  其他五轮无丢包，全部六轮无queue drop且资源账本清空；**资源清空不等于全部任务终态**。

汇总器原先遇到截断便抛异常；现补充独立`lifecycle_acceptance`，保留失败JSON和原始日志，
F3/截断/丢包任一不合格仍返回非零，不放宽门槛。只重新读取已有输出，没有重跑或更换随机轮。

### 故障时的概率、大小与完成度

固定代表轮仍为confirmation11，统计66个F1 START及66个实际RUNNING victim；
下表为min/P10/P50/P90/max，线性插值。LLM请求仅数百字节，因此不能将INPUT大小当作其计算量。

| 量 | min | P10 | P50 | P90 | max |
|---|---:|---:|---:|---:|---:|
| START温度 °C | 25.835 | 27.133 | 28.643 | 29.227 | 29.794 |
| pF1 %（本轮F1时q_comp相同） | 1.549 | 5.685 | 25.738 | 46.177 | 81.350 |
| continuous busy s | 13.421 | 16.125 | 22.419 | 25.541 | 28.730 |
| 恢复 s | 2.719 | 3.118 | 3.582 | 3.762 | 3.936 |
| victim INPUT B | 351 | 585 | 126,526,931.5 | 1,000,000,000 | 1,000,000,000 |
| victim WU | 41,389 | 124,639 | 338,193 | 1,500,000 | 1,500,000 |
| victim完成度 % | 0.431 | 7.260 | 43.287 | 85.456 | 99.988 |
| deadline余量 s | 0.266 | 0.766 | 3.469 | 10.993 | 15.876 |

F1 victim类别dense/sparse/compression/LLM=16/18/13/19。代表轮22–25°C及29.9–30°C START
均为0；其他轮偶发中温START仍原样保留。F2发生于263 s/node28、742 s/node17，
pF2分别0.0240649%/0.1243540%，pF1=0；当时无RUNNING任务，无有效任务完成度。
逐条概率、任务字节、WU进度和终态均可查fault-events.csv/fault-task-impact.csv。

### 固定压力场景与验收边界

按预声明有限池和硬验收过滤后，**只有seed1/run41合格**，因此冻结为
selected fixed stress realization：F1/F2 START=75/2，联合direct=75，F3 direct=1，
724完成/76失败、1524传输完成/76取消、零丢包/截断。选择记录及全部六轮结果在
`fixed-stress-benchmark.json`；75不是平均值，也不是事后选择后仍无偏的held-out表现。
这不掩盖另外五轮失败，不代表F3窗口在随机运行中稳健，也不表示G3整体验收通过。
所选run41的F1概率P10/P50/P90为3.361%/12.725%/48.286%，温度中位27.939°C；
其75个F1 victim的INPUT中位109,635,608 B，WU中位301,998，完成度中位43.733%。
pF1中位低于20–30%的软形状参考，也如实保留，不因事后选定固定轮而再次调模型。
未来N5改变busy状态仍可能改变F1，即使seed/run相同也不能保证故障时刻一致；本轮不恢复replay。

43个Python、15个C++、6个smoke、4个维护regression通过；模块编译通过，无全局example/test或CI。
六轮共12,546行四项概率全部匹配，缺失/上下文错配/MAE/RMSE/max error均为0；
1245个实际停机冷却点通过。概率一致不等于场景验收通过或现实预测准确率100%。
run11审计on/off的18项业务证据一致，off时四种可选输出不存在，正常运行仍默认关闭。
两图继续使用Python原始观测；字体最低7 pt，对齐和PDF碰撞检查通过，完整图及各面板
100 dpi实际尺寸检查通过。源数据、provenance及QA保留在新输出目录figures/。

复现固定压力轮（目录必须不存在；去掉audit即正常运行）：

```bash
source .venv/bin/activate
python contrib/satcompute/tests/integration/regression/run-n4c-baseline.py \
  --output-dir=output/g3-109g-run41 --fault-mode=generate --audit \
  --seed=1 --run=41 --f1-beta=10 --f1-gamma=1.5 \
  --task-trace=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3-109g/task-trace.json \
  --hotspot-manifest=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3-109g/placement-manifest.json
python contrib/satcompute/tools/validation/summarize-n4c-g3.py \
  --run-dir=output/g3-109g-run41 --expect-f3 \
  --manifest=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3-109g/placement-manifest.json \
  --base-task-trace=contrib/satcompute/input/examples/leo-66-1000s-n4c-g3-109g/base-task-trace.json
```

生成器使用`--profile=n4c-c800-109g`产生纯业务基线，再用`--profile=n4c-hotspot`
和`--workload-candidate=C800-109G`做placement；其他命令参数及逐阶段调用保留在
输出目录experiment.py/各execution.json。验收必须显式提供上述base-task-trace，
不能把默认81.75GB检查改成宽松总量检查。

结论：109GB提高了自然故障压力，但数量目标和F3场景稳健性仍有缺口；下一步须人工审阅
none安全窗口的余量及在途传输的故障收尾边界。本轮不继续选目标、调模型或扩充run pool。
