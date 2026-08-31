# 确定性任务生成器

`generate-task-workload.py` 根据一份 topology-only 节点切片和一份
ComputeProfile 生成 TaskTrace。它不生成星座、坐标、链路或完整平台配置，也不在
Python 中复制 ns-3.48 的轨道计算。

脚本包含四个明确的生成档：默认 `stress` 用于可调规模压力任务；
`f1-validation` 固定生成 N4B 第一阶段的 66 星、20 任务输入；`f2-validation` 固定
生成第二阶段的 66 星、8 任务输入；`n4b-joint-validation` 固定生成 N4B 最终联合
验收的 66 星、100 任务输入。四者共用同一套输入闭集校验、稳定 ID 和 JSON writer，
不再维护独立的故障场景生成器。

## 输入与输出

输入必须满足以下约束：

- `--nodes-file` 是 `nodes_<time>s.json`，包含至少 3 颗 `sat` 节点及唯一
  `node_id`；坐标字段可以存在，但只用于确认这是节点切片，不参与任务分配；
- `--compute-profile` 是平台可直接读取的 ComputeProfile，其中所有算力节点都必须
  出现在节点切片中；
- `stress` 档的字节、任务数量和时间边界均使用整数，时间参数单位为 ns；
- `f1-validation` 要求节点切片恰好包含 66 星、ComputeProfile 至少包含 6 个节点；
- `f2-validation` 要求节点切片恰好包含 66 星，并包含固定验证节点
  `0/11/18/29/40/51` 的算力配置。
- `n4b-joint-validation` 要求节点切片恰好包含 66 星，且全部卫星均具有算力配置。

脚本写出两个 JSON：

- `--output-task-trace`：平台可直接读取的 `{"tasks": [...]}`；
- `--output-workload-summary`：stress 档记录分布与预算，F1/F2 验证档记录任务角色；
  仅用于检查生成结果，不是平台输入。

## stress 快速示例

先用平台导出一个节点切片，再运行：

```bash
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --nodes-file=/tmp/satcompute-topology/topology/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-count=100 \
  --total-input-bytes=500000000 \
  --seed=example \
  --arrival-start-ns=0 \
  --arrival-end-ns=1000000000 \
  --arrival-mode=uniform \
  --output-task-trace=/tmp/tasks.json \
  --output-workload-summary=/tmp/tasks-summary.json
```

## F1 验证档

```bash
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --profile=f1-validation \
  --nodes-file=/tmp/satcompute-topology/topology/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --seed=n4b-f1-66 \
  --output-task-trace=/tmp/f1-task-trace.json \
  --output-workload-summary=/tmp/f1-workload-summary.json
```

该档固定产生 20 个任务：3 个热点节点分别包含连续负载与恢复后任务，1 个节点只
形成风险 episode，2 个节点承载稀疏短任务。它只构造任务忙闲条件，不预先写故障；
是否发生故障仍由正式仿真中的 `FaultModelEngine` 根据实时状态判定。

## F2 验证档

先用 `--orbitStartOffset=302 --topologyOnly=1` 导出 66 星空间标定窗口的 0 秒节点切片，
再运行：

```bash
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --profile=f2-validation \
  --nodes-file=/tmp/satcompute-n4b-f2-topology/topology/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --seed=n4b-f2-66 \
  --output-task-trace=/tmp/f2-task-trace.json \
  --output-workload-summary=/tmp/f2-workload-summary.json
```

该档固定产生 8 个轻量任务：两个 60 秒长任务、两个 10 秒后续任务、两个 20 秒
中等任务和两个 5 秒对照任务。任务只为真实平台 Monte Carlo 提供完整执行环境，
不再预先声明某个随机 run 必须在哪颗卫星、哪个时刻故障。完整命令见
[`leo-66-1000s-f2`](../../input/examples/leo-66-1000s-f2/README.md)。任务仍只用于
验证执行生命周期，F2 是否发生由正式平台的实时空间风险和 ns-3 随机流决定。

## N4B 联合验收档

```bash
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --profile=n4b-joint-validation \
  --nodes-file=/tmp/satcompute-n4b-joint-topology/topology/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
  --seed=n4b-joint-66 \
  --output-task-trace=/tmp/n4b-joint-task-trace.json \
  --output-workload-summary=/tmp/n4b-joint-workload-summary.json
```

该档固定产生 100 个任务：30 个任务覆盖 3 个强热点、1 个临界热点和 1 个温热对照
节点；8 个任务覆盖 F2/F3 故障、恢复和邻接对照窗口；其余 62 个 2–5 秒短任务分散
到 55 个非保留计算节点。完整角色、冻结 seed/run 和四轮验收流程见
[`leo-66-1000s-n4b-joint`](../../input/examples/leo-66-1000s-n4b-joint/README.md)。

## 参数

### 基本任务与到达过程

| 参数 | 含义 |
|---|---|
| `--profile` | `stress`（默认）、`f1-validation`、`f2-validation` 或 `n4b-joint-validation` |
| `--nodes-file` | topology-only 节点切片 |
| `--compute-profile` | 算力节点及其处理速率 |
| `--task-count` | stress 必填；任务总数，任务 ID 固定为 `1..N` |
| `--total-input-bytes` | stress 必填；所有任务 `input_bytes` 的精确总预算 |
| `--seed` | 非空字符串；相同输入和参数生成相同结果 |
| `--arrival-start-ns` | stress 必填；最早到达边界，含该时刻 |
| `--arrival-end-ns` | stress 必填；最晚到达边界，含该时刻 |
| `--arrival-mode` | stress 必填；`uniform` 均匀散布，`burst` 确定性突发到达 |
| `--output-task-trace` | TaskTrace 输出路径 |
| `--output-workload-summary` | 分布汇总输出路径 |

### stress 任务类别比例

比例使用 basis point（bp），`10000 bp = 100%`。四项之和必须为 10000。

| 参数 | 默认值 | 类别 |
|---|---:|---|
| `--enhancement-share-bp` | 1500 | 图像增强 |
| `--detection-share-bp` | 2500 | 图像检测 |
| `--dnn-share-bp` | 3500 | DNN 推理 |
| `--preprocess-share-bp` | 2500 | 预处理与压缩 |

脚本以最大余数法把比例转换为整数任务数。类别只用于生成不同的输入权重、计算量
和结果大小，TaskTrace 本身仍保持平台的通用任务字段。

### stress 大任务尾部与普通任务边界

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `--large-1gb-count` | 0 | 最大场景中的 1 GB 任务数 |
| `--large-500mb-count` | 0 | 最大场景中的 500 MB 任务数 |
| `--scenario-scale-bp` | 10000 | 对上述两个数量应用的场景比例 |
| `--tail-preprocess-share-bp` | 7500 | 大任务分给预处理类别的比例 |
| `--tail-enhancement-share-bp` | 2500 | 大任务分给图像增强类别的比例 |
| `--non-tail-min-input-bytes` | 1048576 | 普通任务最小输入字节数 |
| `--non-tail-max-input-bytes` | 300000000 | 普通任务最大输入字节数 |

两项尾部类别比例之和必须为 10000。大任务占用精确字节后，剩余输入预算按类别
权重分给普通任务，并严格落在给定上下界中；预算不可满足时脚本直接报错。

## 生成规则

同一 `seed`、节点集合、ComputeProfile 和命令行参数会生成逐字节相同的两个输出。
确定性来源不是 Python 的全局随机状态，而是脚本内对
`seed + task_id + field_name` 执行的 FNV-1a 64-bit 映射。

stress 档的生成过程还保证：

- compute 节点、source 节点和 result 节点按稳定 ID 尽量均衡分配；
- 每个任务的 source 与 compute 不同，result 与 compute 不同；
- `input_bytes` 的总和精确等于 `--total-input-bytes`；
- `output_bytes` 按任务类别生成并显式写入，不由仿真时推导；
- 同一类别内，较大的输入总体对应较大的 `compute_work_units`；
- 到达时刻、任务数组和 summary 中按稳定 task ID 输出。

F1 验证档额外把热点节点、预期临界故障任务、恢复后任务、风险-only 节点及对照
节点写入 summary。F2 验证档记录轨道起始偏移、固定 seed/run、热点、预期故障时刻、
恢复后、风险-only 与对照任务。联合验收档记录分级热点、F2/F3 窗口任务和分散对照
任务。三种 summary 都只供测试精确断言，不是平台输入。

主要函数按职责分为：输入闭集校验（`read_satellite_ids`、
`read_compute_profile`）、整数预算分配（`largest_remainder`、
`bounded_weighted_allocation`）、稳定节点/类别/时间分配，以及最终 TaskTrace 和
summary 写出。对应单元测试见
[test_workload_generators.py](../../tests/unit/test_workload_generators.py)。
