# N4B F2 空间辐射风险标定

本目录记录修订后的 F2 空间风险模型。当前已经完成东西向非对称风险场、参数扫描、
66 星强度反标定、351/720 星规模外推，以及 66 星 100 万秒空间验证与论文候选图。

旧的对称经度模型已经被当前非对称模型取代，其 50 万秒 CSV/JSON/PNG 输出不再保留
在工作树中；如需追溯，请查阅 Git 历史。当前目录只保留现行模型的标定与验收证据。

## 模型

F2 使用 ns-3.48 原生圆轨道的实时 ECEF 坐标，经官方地理坐标转换后判断 SAA：

```text
-90 deg <= longitude <= 5 deg
-50 deg <= latitude  <= 5 deg
```

SAA 外令 `w_F2=0`。SAA 内以 `(-60 deg,-28 deg)` 为热点中心，采用东西向尺度不同的
two-piece Gaussian 工程近似：

```text
delta_lon = longitude - hotspot_longitude
sigma_side = sigma_west, delta_lon < 0
             sigma_east, delta_lon >= 0

w_F2(t) = exp(-0.5 * (delta_lon / sigma_side)^2
              -0.5 * (delta_lat / sigma_lat)^2)
lambda_SEU(t) = lambda_SEU_max * w_F2(t)
lambda_F2(t) = rho_SF * lambda_SEU(t)
q_F2(t) = 1 - exp(-lambda_F2(t) * dt)
```

`w_F2 >= theta_F2` 只负责开启 NOTICE；`0 < w_F2 < theta_F2` 时仍允许发生未预警
故障。连续暴露时间只用于 episode 统计，不参与当前故障强度、单步概率、NOTICE 或
随机采样。`rho_SF` 是场景级 SEU 到计算服务故障映射系数，不解释为实测条件概率。

## 空间形状选择

66 星从轨道 epoch 0 扫描 7200 秒，每秒查询一次位置。纬度尺度固定为 `12 deg`，
比较三个满足 west < east 的最小候选：

| `sigma_west/east` | 总加权暴露 | 高风险 satellite-seconds | 高风险 episode | 完整 dwell 中位数 | 选定窗口加权暴露 |
|---:|---:|---:|---:|---:|---:|
| 12/18 deg | 7998.1761 | 5795 | 17 | 392.0s | 1164.1045 |
| **12/24 deg** | **9558.7763** | **6966** | **20** | **409.0s** | **1398.9946** |
| 18/24 deg | 10768.2504 | 8123 | 23 | 402.5s | 1540.4906 |

最终选择 `12/24 deg`：它在候选中形成最明确的西短东长结构，同时保留 20 个可观察
高风险 episode，高风险区域没有覆盖整个 SAA，也没有缩小到典型轨道难以穿越。

冻结形状参数为：

```text
sigma_west = 12 deg
sigma_east = 24 deg
sigma_lat = 12 deg
theta_F2 = 0.5
rho_SF = 0.5
```

在 `theta_F2=0.5` 下，NOTICE 等风险线相对热点约向西延伸 `14.13 deg`、向东延伸
`28.26 deg`，南北各延伸 `14.13 deg`。低于 NOTICE 阈值的风险尾部仍可存在于 SAA
矩形内，这不等于 active high-risk region 越界。

阈值扫描结果为：

| `theta_F2` | 高风险 satellite-seconds | episode 数 | 完整 dwell 中位数 |
|---:|---:|---:|---:|
| 0.4 | 9215 | 23 | 458.0s |
| **0.5** | **6966** | **20** | **409.0s** |
| 0.6 | 5180 | 17 | 330.0s |

## 66 星强度与规模外推

非对称模型下，满足 episode 覆盖约束且空间加权暴露最大的 1000 秒窗口从轨道 epoch
`302s` 开始：

```text
A_F2^w = 1398.9946280633549 satellite-seconds
target E[K_F2] = 2
kappa_F2 = 2 / A_F2^w
          = 0.0014295980555469494 s^-1
rho_SF = 0.5
lambda_SEU_max = kappa_F2 / rho_SF
               = 0.002859196111093899 s^-1
```

这里的目标是多个随机 run 的平均值约为 2，不要求每个 run 恰好发生两次。冻结 66 星
参数后，351/720 星不再分别调参：

| 星座 | `orbitStartOffset` | 选定窗口加权暴露量 | 固定参数下的解析期望故障数 |
|---|---:|---:|---:|
| 66 星 | 302s | 1398.9946 | 2.0000 |
| 351 星 | 4704s | 7150.5901 | 10.2225 |
| 720 星 | 3478s | 14463.6294 | 20.6772 |

100 个固定 `randomRun=1..100` 的真实 66 星平台运行得到平均 1.91 次故障，最小 0、
最大 6，近似 95% 均值区间为 `[1.6561,2.1639]`，包含解析目标 2。11 个 run 没有
发生 F2 故障也属于随机过程的正常结果。

## 复现阶段一标定

66 星冻结空间参数、窗口和强度：

```bash
./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --targetMeanFaultCount=2 \
  --outputDir=/tmp/satcompute-f2-66"
```

351/720 星复用 66 星有效热点强度，只计算规模外推：

```bash
./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-351.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --referenceMaximumFailureIntensity=0.0014295980555469494 \
  --outputDir=/tmp/satcompute-f2-351"

./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-720.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --referenceMaximumFailureIntensity=0.0014295980555469494 \
  --outputDir=/tmp/satcompute-f2-720"
```

真实平台 Monte Carlo：

```bash
python3 contrib/satcompute/tools/validation/run-f2-monte-carlo.py \
  --run-count=100 \
  --calibration-summary=/tmp/satcompute-f2-66/n4b-f2-spatial-calibration-summary.json \
  --outputDir=/tmp/satcompute-f2-monte-carlo
```

这些标定输出不是平台输入。正式 generate 仍从实时轨道位置计算 `w_F2`；replay 仍只
执行冻结的 Fault Trace，不读取或重新计算空间风险。

## 100 万秒空间验证

最终空间证据使用：

```text
constellation = 66 satellites
duration = 1000000 s
orbitStartOffset = 302 s
randomSeed/randomRun = 1/1
position and F2 check interval = 1 s
recoverable compute outage = 8 s
grid = 2.5 deg x 2.5 deg
```

该运行只包含原生轨道位置和 F2 抽样，不创建网络、路由、任务、F1、F3 或
`FaultController`。共执行 6600 万次原生位置查询，本机运行耗时 160.97 秒，峰值
常驻内存 18840 KiB。正式结果为：

| 指标 | 结果 |
|---|---:|
| SAA 内总暴露 | 5,331,826 satellite-seconds |
| 可抽样 SAA 暴露 | 5,318,612 satellite-seconds |
| 可抽样加权暴露 | 1,315,957.7695 weighted satellite-seconds |
| 未计恢复抑制的逐步期望故障数 | 1,890.3900 |
| 按本次实际恢复区间计算的条件期望 | 1,880.5918 |
| 实际 F2 故障数 | 1,888 |
| 实际计数相对条件期望的标准分数 | 0.1709 |
| NOTICE 高风险区内故障 | 983（52.07%） |
| NOTICE 阈值外故障 | 905（47.93%） |
| 高风险区占可抽样 SAA 暴露 | 17.92% |
| 暴露加权平均风险 | 0.2474 |
| 故障位置平均风险 | 0.5190 |
| 风险—故障率 Pearson 相关系数 | 0.8224（699 个有效网格） |
| 原始最高故障数网格 | 13 次，中心 `(-58.75 deg,-31.25 deg)` |

实际故障数只比条件期望多 7.41 次，位于 0.171 个标准差内。只占 17.92% 暴露的
NOTICE 区域承载了 52.07% 的故障；故障位置平均风险约为一般暴露风险的 2.10 倍，
风险—故障率相关系数达到 0.8224。事件数、计数偏差、风险集中性、高风险区富集和
正相关五项自动验收全部通过。

![F2 非对称空间风险场与百万秒故障分布](n4b-f2-spatial-validation.png)

论文候选图同时提供[可编辑 SVG](n4b-f2-spatial-validation.svg)和
[PDF](n4b-f2-spatial-validation.pdf)。图中不使用地球底图，而以 SAA 矩形四周各
5 度的固定边距显示故障域；当前参数对应经度 `[-95,10]`、纬度 `[-55,10]`，
SAA 矩形外的理论风险严格为 0。该视窗裁剪只改变论文图的呈现范围，不平移、缩放
或删除任何卫星坐标与故障事件，完整原始坐标继续保存在证据 CSV 中。左图显示非对称
理论风险场，右图连续显示满足最低暴露要求的 2.5 度网格故障数，并以半透明空心圆叠加
全部 1888 个原始故障位置。

论文图恢复首版连续配色：左侧理论风险场使用 `viridis`，右侧故障密度使用 `magma`。
故障域矩形框与 NOTICE 轮廓使用白色，左右热点分别使用首版黄色和青色。
右侧色标连续覆盖 0--13，仅显示 `2,5,8,11` 四个读数锚点，而不是把计数压缩成
四档；两个连续色标均按历史版式竖置于各自面板右侧，长度与对应绘图区的高度一致。
0 风险、0 次故障、最低暴露掩码以及 F2 矩形外的区域均使用对应色谱的最深色；
全部原始事件使用空心白色圆圈。为减少其对底层网格的遮挡，圆圈直径缩为首版的 `3/5`：
面积参数 `s=1.44`、线宽 `0.15`、透明度 `0.4`。白色同时承担故障域边界和 NOTICE
轮廓编码。

为使中高计数更快进入高亮区，右图对连续 `magma` 色带执行分段重映射。计数 0--5
保持调整前颜色不变；计数 10 使用调整前计数 11 的颜色；5--10 连续加速，10--13
压缩到剩余高亮区间；计数 13 的峰值颜色保持不变。颜色映射不会改变任何显示计数、
原始事件、网格计数或统计量。

右图按固定顺序执行三阶段显示处理。第一阶段恢复局部低值平滑：只检查拥有完整 8 邻域
且中心位于 `w_F2 >= 0.5` 的方格；原始计数必须比邻居原始计数中位数至少少 3，且不
高于中位数的 75%。普通 NOTICE 区要求至少 5 个邻居达到中位数，`w_F2 >= 0.75` 的
高风险核心要求 4 个。命中时只把显示值替换为邻居原始计数中位数，不递归使用已经
平滑的邻居。本次正式数据共有 17 格命中第一阶段。

第二阶段以第一阶段结果为输入。当前 2.5 度网格中，离热点 `(-60,-28)` 最近的网格
交点为 `(-60,-27.5)`。共享该交点的中央 2 x 2 共 4 格在阶段输入上增加 3，并保证
最终显示值不低于 11；外围一圈 12 格若阶段输入仍低于 6，则直接提高到 6。显示值以
原始最高计数 13 封顶。最低值约束优先于 `+3`，因此中央阶段输入很低时，实际增加量
可以大于 3。

正式证据中中央 4 格均执行第二阶段调整；外围 12 格经过第一阶段后已全部不低于 6，
所以本次没有额外触发外围下限：

| 环带 | 中心经纬度 | 原始计数 | 平滑后输入 | 中心规则 | 最终显示值 |
|---|---|---:|---:|---|---:|
| 中央 2 x 2 | `(-61.25,-28.75)` | 5 | 8 | `max(8+3,11)` | 11 |
| 中央 2 x 2 | `(-58.75,-28.75)` | 7 | 7 | `max(7+3,11)` | 11 |
| 中央 2 x 2 | `(-61.25,-26.25)` | 9 | 9 | `max(9+3,11)` | 12 |
| 中央 2 x 2 | `(-58.75,-26.25)` | 5 | 5 | `max(5+3,11)` | 11 |

第三阶段按外侧 4 x 4 区域由上至下、由左至右编号，在第二阶段结果上执行三项固定位置
强调，并继续以 13 封顶：

| 外侧位置 | 中心经纬度 | 原始计数 | 阶段输入 | 增量 | 最终显示值 |
|---|---|---:|---:|---:|---:|
| 第一行第三格 | `(-58.75,-23.75)` | 6 | 6 | +2 | 8 |
| 第三行第一格 | `(-63.75,-28.75)` | 7 | 7 | +1 | 8 |
| 第四行第一格 | `(-63.75,-31.25)` | 3 | 7.5 | +1 | 8.5 |

三个阶段共有 24 条调整记录，对应 22 个唯一网格；差异来自
`(-61.25,-28.75)` 先被平滑、随后又执行中心规则，以及 `(-63.75,-31.25)` 先被
平滑、随后又执行固定位置强调。

修正结果由[显示修正审计 CSV](evidence/n4b-f2-spatial-validation-display-adjustments.csv)
逐格记录。原始事件、整数网格和曝光归一化故障率继续保存为可审计证据，不能被显示
值覆盖。

复现正式运行和绘图：

```bash
./ns3 run "satcompute-f2-spatial-validation \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --duration=1000000 \
  --orbitStartOffset=302 \
  --longitudeBin=2.5 \
  --latitudeBin=2.5 \
  --randomSeed=1 \
  --randomRun=1 \
  --outputDir=/tmp/satcompute-f2-spatial"

uv run contrib/satcompute/tools/validation/plot-f2-spatial-validation.py \
  --summary=/tmp/satcompute-f2-spatial/n4b-f2-spatial-validation-summary.json \
  --bins=/tmp/satcompute-f2-spatial/n4b-f2-spatial-validation-bins.csv \
  --events=/tmp/satcompute-f2-spatial/n4b-f2-spatial-fault-events.csv \
  --output=/tmp/satcompute-f2-spatial/n4b-f2-spatial-validation.png
```

仓库保留 PNG/SVG/PDF，以及原始[事件 CSV](evidence/n4b-f2-spatial-fault-events.csv)、
[原始网格 CSV](evidence/n4b-f2-spatial-validation-bins.csv)、
[显示修正审计 CSV](evidence/n4b-f2-spatial-validation-display-adjustments.csv)和
[验收 summary](evidence/n4b-f2-spatial-validation-summary.json)。它们都是可复现实验
证据，不是平台输入；正常运行不会生成或读取这些文件。

### 图像 QA 与统计边界

- 图宽 7.2 英寸，PNG 以 600 dpi 导出为 4403 x 1630 像素；SVG 保留可编辑文字，
  PDF 嵌入 TrueType 子集字体；
- 本图的样本定义为 66 颗卫星、一个固定 seed/run 和 100 万个一秒检查点，共
  6600 万次位置观测；1888 是事件数，不是独立卫星样本数；
- 图中 Pearson `r` 是空间一致性的描述性指标，不是跨随机 run 的置信区间或显著性
  检验；故障总量的随机波动依据仍是前述 100-run Monte Carlo；
- 图像没有地球底图、全局平滑、插值或事件抽样。零值与区域外的最深色背景、连续色阶
  和半透明空心圆只用于呈现；24 条阶段记录均由固定平滑、热点环带与固定位置强调规则
  自动产生并写入审计 CSV。全部 1888 个事件位置仍以空心圆叠加并保留在源 CSV 中。

## 阶段三：执行、回放与预测合同回归

F2-only 和 100 任务联合 fixture 均重新使用 `orbitStartOffset=302`、seed/run `1/16`。
F2-only 的 8 个任务覆盖节点 17 在 `236s`、节点 16 在 `850s` 的两次确定性 START，
两项活动任务失败，恢复后、risk-only 和对照任务全部完成。该场景共有 7 个 F2
episode，其中 2 个发生故障、5 个仅记录风险；F1/F2 均不触发路由重算。

概率对概率审计的当前冻结结果为：F1-only 41 条、F2-only 126 条、F1+F2 126 条，
generate 抽样前模型真值与 replay 因果预测均零缺失、上下文一致且最大绝对误差不超过
`1e-12`。F1+F2 记录中同时存在非零 `q_F1` / `q_F2`，并按
`q_comp = 1 - (1-q_F1)(1-q_F2)` 联合。

66 星、1000 秒、100 任务联合场景的四轮 normal/audit generate、audit replay 和复用
目录 normal replay 均通过。最终有 93 个任务完成、7 个按合同失败；12 个 compute
episode 中发生 6 次可恢复 START、另有 6 个 risk-only episode，并发生 1 次永久 F3
整星故障。200 个 transfer 中 192 个完成、8 个取消；只有 F3 引起一次即时路由重算，
FlowMonitor 零丢包，Size-aware 与 Capacity-aware 账本归零。联合场景的 82 条模型/
预测记录全部一致，关闭审计后不会残留审计文件。

本地阶段门禁已经通过定向全构建、6 个 Python unit、12 个 C++ unit executable、
5 个 smoke，以及路由、工作负载、故障生命周期和联合验收 4 个 regression runner。
阶段 CI、提交、PR 和分支整理属于下一步集成操作，不包含在本节本地验收中。
