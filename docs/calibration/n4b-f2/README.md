# N4B F2 轨道暴露与参数标定

本目录保存 N4B 第二阶段空间辐射连续暴露故障的可复现标定证据。校准只推进
ns-3.48 原生圆轨道并统计 F2 区域暴露，不创建网络协议栈，不计算路由，也不运行
任务、F1、F3 或故障执行。

## 方法

三组星座都从各自 CSV 的轨道 epoch 0 开始运行 7200 秒，每 1 秒读取一次同一个
`LeoCircularOrbitMobilityModel` 的 ECEF 坐标。F2 区域固定为：

```text
-90 deg <= longitude <= 5 deg
-50 deg <= latitude  <= 5 deg
```

校准工具统计完整、左截断和右截断 exposure episode，并扫描所有 1000 秒窗口。
窗口优先同时包含完整和截断 episode，再按星座总暴露时间降序选择。完整 episode
少于 5 个时工具会直接失败，不会用截断样本计算典型暴露时间。

## 66 星参数选择

`synthetic-66.csv` 在 7200 秒内得到 37 个完整 episode，完整暴露时长为
921–922 秒，中位数为 922 秒。选定窗口为原始轨道的 5695–6695 秒：

- 星座总暴露量 `A_F2 = 6423 satellite-seconds`；
- 包含 13 个相交 episode：1 个完整、6 个左截断、6 个右截断；
- 功能目标为平均约 1 个 F2 可恢复计算故障；
- `lambda_F2 = 1 / 6423 = 0.00015569048731122528 s^-1`；
- 典型完整穿越的一半为 `0.5 * 922 = 461 s`；
- `theta_F2 = 1 - exp(-lambda_F2 * 461) = 0.06925814255738115`。

平台通过 `--orbitStartOffset=5695` 令仿真 0 秒直接对应上述窗口起点，不先空跑
5695 秒。因此任务、故障和指标时间仍位于 0–1000 秒，实时轨道坐标则与原轨道
5695–6695 秒严格一致。

这里的 `lambda_F2` 是为了在有限仿真窗口内得到可观测事件而标定的系统级有效计算
故障强度，不是原始 SEU 发生率，也不表示现实中每颗卫星的绝对失效率。

## 351/720 星规模验证

351 星和 720 星使用同一 F2 区域与 66 星冻结的 `lambda_F2`，不分别重新调参：

| 星座 | 完整 episode | 选定 1000 秒窗口暴露量 | 固定 66 星强度下的期望事件数 |
|---|---:|---:|---:|
| 66 星 | 37 | 6423 | 1.0000 |
| 351 星 | 202 | 28961 | 4.5090 |
| 720 星 | 368 | 60845 | 9.4730 |

总暴露量和期望事件数随星座规模合理增加。351/720 汇总中的
`local_candidate_failure_intensity_per_s` 只表示“若各自也强制目标为 1 次”时的反算
诊断值，不是平台选择值；`fixed_reference_validation` 才是固定 66 星参数后的验证结果。

## 复现

在仓库根目录构建后执行：

```bash
./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --outputDir=/tmp/satcompute-n4b-f2-66"

./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-351.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --referenceFailureIntensity=0.00015569048731122528 \
  --outputDir=/tmp/satcompute-n4b-f2-351"

./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-720.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --referenceFailureIntensity=0.00015569048731122528 \
  --outputDir=/tmp/satcompute-n4b-f2-720"
```

每个星座目录包含：

- `n4b-f2-exposure-calibration.csv`：按时间和稳定卫星 ID 排序的 episode；
- `n4b-f2-exposure-summary.json`：完整/截断统计、滑动窗口、候选起点和参数结果。

本目录中的三组同名文件是上述运行的阶段验收快照；正式 generate/replay 不读取这些
校准输出。
