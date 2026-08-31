# N4B F2 空间辐射风险标定

本目录记录修订后的 F2 空间风险模型。旧的“连续暴露时间提高当前风险、累计概率
触发 NOTICE”模型及其标定文件已经移除，避免与当前实现混用。连续暴露时间现在
只用于 episode 统计，不参与当前故障强度、单步概率、NOTICE 或随机采样。

## 模型

F2 使用 ns-3.48 原生圆轨道的实时 ECEF 坐标，经官方地理坐标转换后判断 SAA：

```text
-90 deg <= longitude <= 5 deg
-50 deg <= latitude  <= 5 deg
```

SAA 内以 `(-60 deg, -28 deg)` 为热点中心，采用系统级二维高斯近似：

```text
w_F2(t) = exp(-0.5 * ((lon-lon_c)/sigma_lon)^2
                   -0.5 * ((lat-lat_c)/sigma_lat)^2)
lambda_SEU(t) = lambda_SEU_max * w_F2(t)
lambda_F2(t) = rho_SF * lambda_SEU(t)
q_F2(t) = 1 - exp(-lambda_F2(t) * dt)
```

`w_F2 >= theta_F2` 只负责开启 NOTICE；`0 < w_F2 < theta_F2` 时仍允许出现未预警
故障。高斯函数是以文献观测热点为锚点的平滑工程近似，不是文献直接给出的公式。
`rho_SF` 是场景级 SEU 到计算服务故障映射系数，也不解释为实测条件概率。

## 参数选择

66 星从轨道 epoch 0 扫描 7200 秒，每秒查询一次位置。sigma 候选为
`{12,18,24} deg x {8,12,16} deg`；阈值候选为 `{0.4,0.5,0.6}`。最终保留中间尺度：

```text
sigma_lon = 18 deg
sigma_lat = 12 deg
theta_F2 = 0.5
rho_SF = 0.5
```

该组合在 7200 秒内得到 38324 satellite-seconds 的 SAA 暴露，其中加权暴露为
9207.6502、高风险暴露为 6952，后者约占前者对应原始 SAA 暴露的 18.1%。共观察到
20 个高风险 episode，18 个完整 episode 的 dwell 中位数为 393.5 秒。因此高风险
区域既未覆盖整个 SAA，也没有缩小到典型轨道难以穿越。

阈值扫描结果为：

| `theta_F2` | 高风险 satellite-seconds | episode 数 | 完整 dwell 中位数（秒） |
|---:|---:|---:|---:|
| 0.4 | 9164 | 22 | 461.0 |
| 0.5 | 6952 | 20 | 393.5 |
| 0.6 | 5108 | 16 | 347.0 |

## 66 星目标与规模外推

加权暴露最大的合格 1000 秒窗口从轨道 epoch `5210s` 开始：

```text
A_F2^w = 1337.3026834075863 satellite-seconds
target E[K_F2] = 2
kappa_F2 = 2 / A_F2^w = 0.0014955477356134454 s^-1
rho_SF = 0.5
lambda_SEU_max = kappa_F2 / rho_SF
               = 0.0029910954712268908 s^-1
```

这里的目标是多随机 run 的平均值约为 2，不要求每个 run 恰好发生两次。冻结 66 星
参数后，351/720 星不再分别调参：

| 星座 | 选定窗口加权暴露量 | 固定参数下的解析期望故障数 |
|---|---:|---:|
| 66 星 | 1337.3027 | 2.0000 |
| 351 星 | 6888.1284 | 10.3015 |
| 720 星 | 14051.9769 | 21.0154 |

100 个固定 `randomRun=1..100` 的真实 66 星平台运行得到平均 2.24 次故障，最小 0、
最大 7，近似 95% 均值区间为 `[1.9683,2.5117]`，包含解析目标 2。单个 run 的
0、1、3 次或更多故障都属于随机过程的正常结果。

## 复现

```bash
./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --targetMeanFaultCount=2 \
  --outputDir=/tmp/satcompute-f2-66"

./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-351.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --targetMeanFaultCount=2 \
  --referenceMaximumFailureIntensity=0.0014955477356134454 \
  --outputDir=/tmp/satcompute-f2-351"

./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-720.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --targetMeanFaultCount=2 \
  --referenceMaximumFailureIntensity=0.0014955477356134454 \
  --outputDir=/tmp/satcompute-f2-720"

python3 contrib/satcompute/tools/validation/run-f2-monte-carlo.py \
  --run-count=100 \
  --calibration-summary=/tmp/satcompute-f2-66/n4b-f2-spatial-calibration-summary.json \
  --outputDir=/tmp/satcompute-f2-monte-carlo
```

orbit-only 工具输出：

- `n4b-f2-spatial-exposure-episodes.csv`：SAA exposure episode；
- `n4b-f2-spatial-calibration-summary.json`：空间场、加权暴露、高风险 dwell、窗口和
  强度标定结果。

这些标定输出不是平台输入。正式 generate 仍从实时轨道位置计算 `w_F2`；replay
仍只执行冻结的 Fault Trace，不读取或重新计算空间风险。

100 万秒空间故障分布图和对应的大规模事件证据将在下一阶段通过同一原生轨道与
F2 模型生成；旧模型的 CSV/JSON 不再作为当前证据保留。
