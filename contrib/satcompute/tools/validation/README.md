# 验证与标定工具

## F1 参数标定

`f1-calibration.cc` 构建为 `satcompute-f1-calibration`，直接调用
`F1SelfStateFaultModel` 比较 `tau_h`、`tau_c` 和 `lambda_F1_max` 候选。它不建立
卫星网络，不生成正式 Fault Trace，也不在 Python 中重新实现风险公式。

```bash
./ns3 run "satcompute-f1-calibration \
  --outputDir=/tmp/satcompute-f1-calibration"
```

| 参数 | 含义 |
|---|---|
| `--outputDir` | 必填；标定 CSV 和 summary 的输出目录 |

工具直接读取并校验 `fault/fault-para.cc` 中的内置参数，避免平台运行和标定工具出现
两套配置来源。若修改故障参数，必须重新编译后再运行标定。

输出为：

- `n4b-f1-calibration.csv`：升降温候选逐秒状态，以及四个强度候选各 30 个固定
  run 的计数和分布；
- `n4b-f1-calibration-summary.json`：候选汇总、选择参数、热时间、任务数量、平均
  故障数、风险-only 数、故障温度和预警提前量。

当前冻结输出与解释见
[`docs/calibration/n4b-f1`](../../../../docs/calibration/n4b-f1/README.md)。这些结果
属于 66 星/1000 秒功能场景标定，不代表客观航天器失效率。

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
| `--spatialRiskThreshold` | 高风险 NOTICE 候选阈值 |
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

左图使用完整地球经纬度坐标显示东西向非对称理论风险场；右图使用 3 x 3 邻域均值
展示每个 2.5 度网格的故障数，色标上限固定为原始网格最大故障数，并叠加未平滑的
实际故障位置。平滑只用于论文图呈现，不覆盖事件 CSV、原始整数网格或曝光归一化
故障率。

该脚本内嵌 PEP 723 依赖声明，`uv run` 会隔离解析 NumPy 和 Matplotlib；不需要把
绘图依赖加入 ns-3 Python 绑定环境。`--allow-unaccepted` 只供短程工具调试，正式
论文图必须继续使用默认的已通过验收输入。

## Generate/Replay 概率一致性

`compare-fault-probabilities.py` 比较一次 generate 的抽样前真实模型概率与一次
replay 的因果预测概率。replay 仍只以 generate 的 Fault Trace 为故障输入；这里的
CSV 只用于仿真结束后的实现验证。两次平台运行都必须显式传入
`--faultProbabilityAudit=1`；正常运行默认不生成这些文件，平台也不会自动调用本
对比脚本。

```bash
python3 contrib/satcompute/tools/validation/compare-fault-probabilities.py \
  --model=/tmp/generate/fault-model-probabilities.csv \
  --prediction=/tmp/replay/fault-predictions.csv \
  --detail=/tmp/audit/fault-probability-audit.csv \
  --summary=/tmp/audit/fault-probability-audit-summary.json
```

脚本以 `(simulation_time_ns,node_id,task_id)` 为主键，要求 `fault_id`、NOTICE、任务
进度和预测窗口上下文一致，再分别比较当前步 `q_F1`、`q_F2`、`q_comp` 以及任务
完成前累计概率 `P_fail_before_finish`。summary 固定给出：

- model、prediction、matched 与双向缺失记录数；
- 上下文不一致记录数；
- 四个概率字段各自的 MAE、RMSE 和最大绝对误差；
- `within_tolerance` 总结论。

`--absolute-tolerance` 默认 `1e-12`。没有匹配记录、键集合不同、上下文不同或任一
最大误差超限时返回非零状态。N4B 回归分别在 66 星 F1-only、F2-only 和 F1+F2
场景运行该工具。

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
