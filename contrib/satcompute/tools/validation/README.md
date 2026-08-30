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

## F2 轨道暴露标定

`f2-exposure-calibration.cc` 构建为 `satcompute-f2-exposure-calibration`，只推进
ns-3.48 原生圆轨道并按 1 秒读取 ECEF 坐标。它不创建 InternetStack、NetDevice、
路由、任务、F1/F3 或故障执行，因此 1000 秒暴露窗口的选择不受网络负载影响。

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
| `--referenceFailureIntensity` | 可选；以 66 星冻结强度验证更大星座，不参与重新标定 |
| `--outputDir` | 必填；episode CSV 和 summary 的输出目录 |

工具输出 `n4b-f2-exposure-calibration.csv` 与
`n4b-f2-exposure-summary.json`。66 星负责选择窗口并冻结 `lambda_F2/theta_F2`；
351/720 星必须复用 66 星强度，只检查总暴露和期望事件数是否随规模增长，不能各自
重新调成平均一次故障。冻结证据和三组复现命令见
[`docs/calibration/n4b-f2`](../../../../docs/calibration/n4b-f2/README.md)。

## F2 真实平台 Monte Carlo

orbit-only 标定完成后，`run-f2-monte-carlo.py` 才调用 66 星、1000 秒、8 任务的真实
F2-only generate；它覆盖概率抽样、N4A compute 故障、8 秒恢复和任务执行，不重复
实现 F2 公式。

```bash
python3 contrib/satcompute/tools/validation/run-f2-monte-carlo.py \
  --run-count=100 \
  --outputDir=/tmp/satcompute-f2-monte-carlo
```

脚本固定 `randomSeed=1`，依次使用 `randomRun=1..N`，输出逐 run CSV 和统计 JSON。
当前 100-run 实际故障均值为 1.02，近似 95% 均值区间为
`[0.8311, 1.2089]`，包含解析目标 1，因此没有因
单次运行的随机计数重新调整强度。

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
