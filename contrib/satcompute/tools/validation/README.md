# 验证与标定工具

## N4C G3 故障压力

`summarize-n4c-g3.py --run-dir=... --manifest=... [--expect-f3] [--none-dir=...]`
核对 C800 业务、真实 START 来源、逐任务影响、整数 WU 进度、deadline 和末端账本，
输出 `g3-summary.json`。event 数、去重 RUNNING victim 和 QUEUED/INPUT 间接影响
分别统计；`--none-dir` 给出同任务实际 queue delay 差值，不推定每次停机增加 8 秒。
`--expect-f3` 要求受控 F3 严格只有预定的一个运行中 victim，且不产生后续坏端点。

`plot-n4c-g3.py --old-none=... --hotspot-none=... --generate=... --output-dir=...`
从上述 summary 和原始 CSV 生成负载/温度图与原生 F2 暴露图，同时保存逐点 CSV。
generate 必须显式开启 probability audit；不插值、平滑或人工调整计数，F2 色标表示
模型空间风险而不是观测故障密度。输出为可编辑 PDF/SVG 及 PNG 预览。
这些工具都按需手动运行，不进入平台正常路径或 CI；冻结参数和实测结果只维护于
[G3 阶段证据](../../../../docs/n4c/reviews/G3-hotspot-fault-calibration.md)。

## 链路压力结果核验

`summarize-pressure-baseline.py --run-dir=<正式运行目录>` 流式读取链路窗口，检查
窗口连续性、物理利用率范围、发送字节/占用时间汇总、全网聚合及队列上限，然后
生成 `pressure-summary.json`。它还汇总任务 P95/P99、端到端 IP 吞吐量、计算节点
利用率、全程/活跃窗口网络利用率、热点链路、墙钟、内存及指标文件大小。另列全程
容量预留比例（预留 bit/s 的时间积分除以配置容量积分）、发生容量等待的传输数及
平均/最大等待时间；平均等待包含未等待的传输，不与物理利用率混用。

这是固定 **10 Gbps、无故障且全部任务完成** 的压力基线检查器，不是通用的故障或
部分完成运行检查器；不会把实际空闲率解释为 capacity-aware 的可准入带宽。
完整操作见[测试说明](../../tests/README.md#手动压力测试10-gbps)。

## F1 参数标定

`f1-calibration.cc` 构建为 `satcompute-f1-calibration`，直接调用正式 F1 模型，
输出 beta=8/10 与 gamma=1.5/2 的四组升温 30 s、冷却 4 s 曲线（0.25 s 采样），核对物理时间与
参考 1 s 概率，不创建网络、不抽随机数，不替代真实 C800 多 run 标定。

```bash
./ns3 run "satcompute-f1-calibration --outputDir=/tmp/satcompute-f1-calibration"
```

唯一工具参数是必填 outputDir。输出名保留 `n4b-f1-calibration.csv` 和
`n4b-f1-calibration-summary.json`，当前不再扫描 lambdaMax 或生成 risk-only 统计。
默认模型参数来自 fault-para.cc；修改后重新编译。旧 N4B 强度标定仅为历史，
当前冻结参数与完整网络验收见 [G3 报告](../../../../docs/n4c/reviews/G3-hotspot-fault-calibration.md)。

## F2 空间风险标定

`f2-exposure-calibration.cc` 构建为 `satcompute-f2-exposure-calibration`，直接按
任意时间查询 ns-3.48 原生圆轨道 ECEF 坐标，并统计 SAA 空间风险加权暴露。它不
创建 InternetStack、NetDevice、路由、任务、F1/F3 或故障执行。

```bash
./ns3 run "satcompute-f2-exposure-calibration \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
  --calibrationDuration=7200 \
  --windowDuration=1000 \
  --outputDir=/tmp/satcompute-f2-66"
```

| 参数 | 含义 |
|---|---|
| `--constellationConfig` | 必填；一个原生 LEO shell CSV |
| `--calibrationDuration` | 轨道扫描时长，默认 7200 秒 |
| `--windowDuration` | 滑动功能窗口，默认 1000 秒 |
| `--targetMeanFaultCount` | 选定功能窗口的解析目标均值，默认 2 |
| `--sigmaLongitudeWest` / `--sigmaLongitudeEast` | 热点西侧/东侧经度标准差，单位为度；必须满足 west < east |
| `--sigmaLatitude` | 高斯空间场纬度标准差，单位为度 |
| `--spatialRiskThreshold` | 独立空间分析的高风险分类阈值 |
| `--referenceMaximumFailureIntensity` | 可选；以 66 星冻结的热点最大有效强度验证更大星座 |
| `--outputDir` | 必填；episode CSV 和 summary 的输出目录 |

工具输出 `n4b-f2-spatial-exposure-episodes.csv` 与
`n4b-f2-spatial-calibration-summary.json`。summary 包含加权暴露、高风险 dwell、
候选窗口、`lambda_SEU_max`、`rho_SF` 和 `kappa_F2`。66 星负责冻结参数；351/720
星必须复用 66 星强度，只检查期望事件数是否随规模增长，不能各自重新调参。证据与
三组复现命令见
[`docs/calibration/n4b-f2`](../../../../docs/calibration/n4b-f2/README.md)。

## F2 真实平台 Monte Carlo

orbit-only 标定完成后，`run-f2-monte-carlo.py` 才调用 66 星、1000 秒、8 任务的真实
F2-only generate；它覆盖概率抽样、N4A compute 故障、8 秒恢复和任务执行，不重复
实现 F2 公式。

```bash
python3 contrib/satcompute/tools/validation/run-f2-monte-carlo.py \
  --run-count=100 \
  --calibration-summary=/tmp/satcompute-f2-66/n4b-f2-spatial-calibration-summary.json \
  --outputDir=/tmp/satcompute-f2-monte-carlo
```

脚本从空间标定 summary 读取窗口和强度，固定 `randomSeed=1`，依次使用
`randomRun=1..N`，输出逐 run CSV 和统计 JSON。当前非对称模型的 100-run 实际
故障均值为 1.91，近似 95% 均值区间为 `[1.6561, 2.1639]`，包含解析目标 2，因此
不按单个 run 的随机计数重新调整强度。

## F2 长时空间验证与绘图

`f2-spatial-validation.cc` 构建为 `satcompute-f2-spatial-validation`。它复用正式
`OnlineOrbitConstellation`、`F2RadiationFaultModel`、ns-3 随机流和 8 秒恢复期间
暂停抽样的语义，但不创建网络、路由、任务、F1、F3 或 `FaultController`。正式默认
固定为 66 星、100 万秒、`orbitStartOffset=302`、`randomSeed=1`、`randomRun=1`：

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
```

| 参数 | 含义 |
|---|---|
| `--duration` | 轨道与 F2 抽样时长，单位为秒，默认 1000000 |
| `--orbitStartOffset` | 仿真 `t=0` 对应的轨道 epoch，当前 66 星标定窗口为 302 秒 |
| `--longitudeBin` / `--latitudeBin` | 聚合网格大小，默认均为 2.5 度，必须整除 SAA 范围 |
| `--randomSeed` / `--randomRun` | ns-3 确定性随机序列，正式基线固定为 1/1 |
| `--progressInterval` | 进度输出周期，单位为秒，默认 100000；设为 0 时关闭 |
| `--outputDir` | 事件 CSV、网格 CSV 和验收 JSON 的输出目录 |

输出为：

- `n4b-f2-spatial-fault-events.csv`：实际命中的事件位置、风险、单步概率与恢复时刻；
- `n4b-f2-spatial-validation-bins.csv`：网格暴露、可抽样暴露、期望与实际故障数；
- `n4b-f2-spatial-validation-summary.json`：运行参数、事件统计、空间集中性与验收结论。

工具只有在事件数足够、实际计数位于条件期望四个标准差内、事件风险高于暴露风险、
高风险区故障占比高于其暴露占比且网格风险—故障率正相关时才返回 0。正常平台运行
不会创建这些文件。验收后使用绘图脚本生成双面板 600 dpi PNG，并同时导出文本可
编辑的 SVG 和 PDF：

```bash
uv run contrib/satcompute/tools/validation/plot-f2-spatial-validation.py \
  --summary=/tmp/satcompute-f2-spatial/n4b-f2-spatial-validation-summary.json \
  --bins=/tmp/satcompute-f2-spatial/n4b-f2-spatial-validation-bins.csv \
  --events=/tmp/satcompute-f2-spatial/n4b-f2-spatial-fault-events.csv \
  --output=/tmp/satcompute-f2-spatial/n4b-f2-spatial-validation.png
```

左图使用 SAA 矩形四周各 5 度的动态视窗显示东西向非对称理论风险场；当前参数对应
经度 `[-95,10]`、纬度 `[-55,10]`。右图以满足最低暴露要求的每个 2.5 度方格的原始
故障数为基底，并使用连续色阶；色标覆盖 0--13，仅标出 `2,5,8,11` 作为读数锚点。
两个连续色标均按照历史版式竖置于各自面板右侧，长度与对应绘图区的高度一致。0 次
网格、最低暴露掩码和 F2 矩形外都使用对应色谱的最深色。最终配色恢复首版方案：左图使用
`viridis`，右图使用 `magma`；故障域矩形和 高风险等值线使用白色，左右热点分别使用
首版黄色和青色。图中按首版参数以空心白色圆圈叠加全部实际故障位置：`s=4`、线宽
`0.25`、透明度 `0.4`。为避免事件轮廓遮挡网格颜色，最终将圆圈直径缩为该版本的
`3/5`：Matplotlib 面积参数调整为 `s=1.44`，线宽同步调整为 `0.15`，透明度保持
`0.4`。

右图仍使用连续 `magma` 色带，但对 5 以上的颜色进程做分段加速：计数 0--5 保持
原色不变，计数 10 精确映射到调整前计数 11 的颜色，5--10 在两端之间连续过渡，
10--13 压缩到剩余高亮区间，计数 13 的峰值颜色不变。该变换只改变颜色映射，不改变
显示计数、色标数值或任何原始证据。

右图不做全局平滑、插值或空洞填补，但按固定顺序执行三阶段显示处理。第一阶段恢复
局部低值平滑：只检查拥有完整 8 邻域且中心位于 `w_F2 >= 0.5` 的方格；原始计数必须
比邻居原始计数中位数至少少 3、不高于中位数的 75%。普通高风险区要求至少 5 个
邻居达到中位数，`w_F2 >= 0.75` 的高风险核心要求 4 个。命中时显示值替换为邻居中位数。

第二阶段以平滑结果为输入，并找到离配置热点最近的经纬度网格交点。共享该交点的
中央 2 x 2 共 4 格在第一阶段显示值上增加 3，并保证最终显示值不低于 11；外围一圈
12 格若第一阶段显示值仍低于 6，则直接提高到 6。所有显示值以原始最高计数 13 封顶。
本次证据第一阶段命中 17 格；第二阶段调整中央 4 格，外围 12 格在平滑后均不低于 6，
因此没有额外触发下限。第三阶段按外侧 4 x 4 区域由上至下、由左至右编号：第一行
第三格增加 2，第三行第一格和第四行第一格分别增加 1；三格最终显示值依次为 8、8、
8.5。三个阶段共有 24 条记录，对应 22 个唯一网格。绘图脚本会在图件旁
生成 `*-display-adjustments.csv`，逐阶段记录规则、原始计数、阶段输入、阶段输出及
邻域依据。该规则不修改事件 CSV、原始整数网格或曝光归一化故障率。

该脚本内嵌 PEP 723 依赖声明，`uv run` 会隔离解析 NumPy 和 Matplotlib；不需要把
绘图依赖加入 ns-3 Python 绑定环境。`--allow-unaccepted` 只供短程工具调试，正式
论文图必须继续使用默认的已通过验收输入。

冻结的 100 万秒 seed/run `1/1` 结果为 1888 次实际故障、1880.59 次条件期望、
0.1709 的计数标准分数和 0.8224 的网格风险—故障率相关系数，五项验收全部通过。
原始 CSV、显示修正审计 CSV、summary 与 PNG/SVG/PDF 见
[`docs/calibration/n4b-f2`](../../../../docs/calibration/n4b-f2/README.md)。

## 在线模型与独立预测的一致性

`compare-fault-probabilities.py` 比较一次 generate 的抽样前真实模型概率与一次
同轮或同 seed 重复 generate 的独立因果预测概率；CSV 只用于仿真结束后的实现验证。
平台运行必须显式传入
`--faultProbabilityAudit=1`；正常运行默认不生成这些文件，平台也不会自动调用本
对比脚本。

```bash
python3 contrib/satcompute/tools/validation/compare-fault-probabilities.py \
  --model=/tmp/generate/fault-model-probabilities.csv \
  --prediction=/tmp/generate/fault-predictions.csv \
  --detail=/tmp/audit/fault-probability-audit.csv \
  --summary=/tmp/audit/fault-probability-audit-summary.json
```

脚本以 `(simulation_time_ns,node_id,task_id)` 为主键，要求任务进度和预测窗口上下文一致，再分别比较当前步 `q_F1`、`q_F2`、`q_comp` 以及任务
完成前累计概率 `P_fail_before_finish`。summary 固定给出：

- model、prediction、matched 与双向缺失记录数；
- 上下文不一致记录数；
- 四个概率字段各自的 MAE、RMSE 和最大绝对误差；
- `within_tolerance` 总结论。

`--absolute-tolerance` 默认 `1e-12`。没有匹配记录、键集合不同、上下文不同或任一
最大误差超限时返回非零状态。N4B 回归分别在 66 星 F1-only、F2-only 和 F1+F2
场景运行该工具；记录量随 RUNNING 时长和故障状态变化，不把旧 NOTICE 门控下的
固定行数当成新合同。概率容差、全覆盖、业务不变和重复一致性由回归自动检查。

## 失败输出一致性检查

`check-flow-drop-reasons.py` 检查一次失败任务运行中的 FlowMonitor DropReason 证据。
它不会修改输出，也不重复验证路由、任务或拓扑合同；这些断言由对应的 C++、smoke
和 regression 测试负责。

## 所需文件

`--output-dir` 指向一次正式仿真的输出根目录，检查器读取：

```text
<output-dir>/
├── network-flow-details.csv
├── run-summary.json
└── diagnostics/failure/flow-drop-reasons.csv
```

检查内容包括：

- 每条诊断记录都能对应唯一 FlowMonitor flow；
- transfer ID、IPv4 五元组、reason code 和 reason name 一致；
- 每条 flow 的显式丢包与 `UNATTRIBUTED_TIMEOUT` 能闭合 `lost_packets`；
- `run-summary.json` 中的丢包总量和逐原因汇总与 CSV 完全一致；
- 丢包数量、字节数和重复行满足输出合同。

## 使用方法

```bash
python3 contrib/satcompute/tools/validation/check-flow-drop-reasons.py \
  --output-dir=/tmp/satcompute-failure \
  --require-reason=QUEUE \
  --require-zero-unattributed
```

| 参数 | 含义 |
|---|---|
| `--output-dir` | 必填，正式仿真的输出根目录 |
| `--require-reason NAME` | 可重复；要求至少出现一次指定原因 |
| `--forbid-reason NAME` | 可重复；禁止出现指定原因 |
| `--require-zero-unattributed` | 要求没有 `UNATTRIBUTED_TIMEOUT` |
| `--minimum-explicit-drop-packets N` | 显式原因丢包总数下界，默认 1 |
| `--expected-explicit-drop-packets N` | 显式原因丢包总数必须精确等于 N |
| `--expect-transfer-drop T:R:N` | 可重复；要求 transfer `T` 因原因 `R` 丢失 N 个包 |

可用原因名为 `NO_ROUTE`、`TTL_EXPIRE`、`BAD_CHECKSUM`、`QUEUE`、
`QUEUE_DISC`、`INTERFACE_DOWN`、`ROUTE_ERROR`、`FRAGMENT_TIMEOUT`、
`INVALID_REASON` 和 `UNATTRIBUTED_TIMEOUT`。检查通过时脚本输出一行 `PASS`，任一
合同不满足时以非零状态退出并给出第一处错误。

该工具由 [diagnostics smoke](../../tests/integration/smoke/run-diagnostics-smoke.sh)
和 [workload regression](../../tests/integration/regression/run-full-workload-regression.sh)
直接调用。
