# 独立数据生成工具

本目录只生成 SatCompute 的独立数据输入，不生成平台完整运行配置，也不复制
ns-3.48 轨道传播公式。

## NetworkTransfer

`generate-transfer-workload.py` 从一个 `nodes_<time>s.json` 读取稳定卫星 ID，按
确定性轮转规则生成 NetworkTransfer 0.1：

```bash
python3 contrib/satcompute/tools/generation/generate-transfer-workload.py \
  --nodes-file=contrib/satcompute/input/topology/examples/xw-66sat/nodes_0s.json \
  --count=100 --min-size-bytes=1024 --max-size-bytes=1048576 \
  --arrival-start-ns=100000000 --arrival-step-ns=1000000 \
  --output=/tmp/transfers.json
```

## TaskTrace

`generate-task-workload.py` 同时读取卫星 ID 和独立 ComputeProfile，以显式 seed、
rules version、总输入字节预算、到达窗口和任务类别比例生成 TaskTrace 0.1，并
输出包含 SHA-256 和分布统计的只读 summary：

```bash
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --nodes-file=contrib/satcompute/input/topology/examples/xw-66sat/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-count=100 --total-input-bytes=500000000 \
  --seed=example --rules-version=v1 \
  --arrival-start-ns=0 --arrival-end-ns=1000000000 --arrival-mode=uniform \
  --output-task-trace=/tmp/tasks.json \
  --output-workload-summary=/tmp/tasks-summary.json
```

相同输入字节与参数必须生成逐字节相同的结果。输出中的纳秒事件字段属于业务
数据合同；仿真时长、网络/导出周期、时延、路由和输出目录仍只由
`para.cc`/CLI 决定。

## 独立输入 bundle

`scenario/generate_scenario.py` 保留旧版目录入口，但不生成完整 scenario 配置。
它通过已校验的 topology manifest 取得稳定 satellite ID，将 ComputeProfile +
TaskTrace 或 NetworkTransfer 逐字节复制为确定性 bundle，并记录输入哈希：

```bash
python3 -m contrib.satcompute.tools.generation.scenario.generate_scenario \
  --bundle-name=task-demo --topology-trace=/tmp/topology-trace \
  --compute-profile=/tmp/compute-profile.json --task-trace=/tmp/tasks.json \
  --output-dir=/tmp/task-bundle
```

详情见 `scenario/README.md`。bundle manifest 只作生成证据；平台仍通过
`--computeProfile`、`--taskTrace` 或 `--transferTrace` 读取各自文件。
