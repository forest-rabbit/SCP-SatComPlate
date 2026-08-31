# N4B F2 空间辐射风险标定

本目录记录修订后的 F2 空间风险模型。当前阶段已经完成东西向非对称风险场、参数
扫描、66 星强度反标定以及 351/720 星规模外推。100 万秒空间验证与最终论文图属于
下一阶段，尚未用当前参数生成。

旧的对称经度模型证据没有删除，统一归档在
[`evidence/historical-symmetric/`](evidence/historical-symmetric/)；其中的 50 万秒、
963 次故障和旧 PNG 只用于历史对照，不是当前非对称模型的验收结果。

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

## 下一阶段：100 万秒空间验证

最终空间证据将使用：

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
`FaultController`。最终图只画完整地球经纬度坐标（经度 `[-180,180]`、纬度
`[-90,90]`），不使用地球底图；SAA 矩形外的理论风险严格为 0。左图显示非对称理论
风险场，右图显示邻域平滑后的每网格估计故障数并叠加更醒目的真实故障散点。右图
色标从 0 到本次平滑网格的最大估计故障数，不再使用“每百万 exposure”的刻度。

论文图使用同一组蓝色梯度，低值到高值依次为 `#F4F9FE`、`#D2E3F3`、`#AACFE5`、
`#68ACD5`、`#3888C0`、`#105CA4`、`#08336E`。邻域平滑只用于呈现，避免有限样本在
热点中心形成突兀空洞；原始事件、未平滑整数网格和曝光归一化故障率继续保存为可
审计证据，不能被平滑结果覆盖。

正式运行命令、100 万秒统计结果和最终 PNG 将在下一阶段完成后补入本节。
