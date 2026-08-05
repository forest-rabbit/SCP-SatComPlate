# 失败输出一致性检查

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
