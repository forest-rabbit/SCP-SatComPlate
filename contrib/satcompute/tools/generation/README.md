# 确定性任务生成器

`generate-task-workload.py` 根据一份 topology-only 节点切片和一份
ComputeProfile 生成 TaskTrace。它不生成星座、坐标、链路或完整平台配置，也不在
Python 中复制 ns-3.48 的轨道计算。

## 输入与输出

输入必须满足以下约束：

- `--nodes-file` 是 `nodes_<time>s.json`，包含至少 3 颗 `sat` 节点及唯一
  `node_id`；坐标字段可以存在，但只用于确认这是节点切片，不参与任务分配；
- `--compute-profile` 是平台可直接读取的 ComputeProfile，其中所有算力节点都必须
  出现在节点切片中；
- 所有字节、任务数量和时间边界均使用整数，时间参数单位为 ns。

脚本写出两个 JSON：

- `--output-task-trace`：平台可直接读取的 `{"tasks": [...]}`；
- `--output-workload-summary`：实际分布、类别计数、总字节、到达范围和节点任务数，
  仅用于检查生成结果，不是平台输入。

## 快速示例

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

## 参数

### 基本任务与到达过程

| 参数 | 含义 |
|---|---|
| `--nodes-file` | topology-only 节点切片 |
| `--compute-profile` | 算力节点及其处理速率 |
| `--task-count` | 任务总数；任务 ID 固定为 `1..N` |
| `--total-input-bytes` | 所有任务 `input_bytes` 的精确总预算 |
| `--seed` | 非空字符串；相同输入和参数生成相同结果 |
| `--arrival-start-ns` | 最早到达边界，含该时刻 |
| `--arrival-end-ns` | 最晚到达边界，含该时刻 |
| `--arrival-mode` | `uniform` 为均匀散布，`burst` 为确定性突发到达 |
| `--output-task-trace` | TaskTrace 输出路径 |
| `--output-workload-summary` | 分布汇总输出路径 |

### 任务类别比例

比例使用 basis point（bp），`10000 bp = 100%`。四项之和必须为 10000。

| 参数 | 默认值 | 类别 |
|---|---:|---|
| `--enhancement-share-bp` | 1500 | 图像增强 |
| `--detection-share-bp` | 2500 | 图像检测 |
| `--dnn-share-bp` | 3500 | DNN 推理 |
| `--preprocess-share-bp` | 2500 | 预处理与压缩 |

脚本以最大余数法把比例转换为整数任务数。类别只用于生成不同的输入权重、计算量
和结果大小，TaskTrace 本身仍保持平台的通用任务字段。

### 大任务尾部与普通任务边界

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

生成过程还保证：

- compute 节点、source 节点和 result 节点按稳定 ID 尽量均衡分配；
- 每个任务的 source 与 compute 不同，result 与 compute 不同；
- `input_bytes` 的总和精确等于 `--total-input-bytes`；
- `output_bytes` 按任务类别生成并显式写入，不由仿真时推导；
- 同一类别内，较大的输入总体对应较大的 `compute_work_units`；
- 到达时刻、任务数组和 summary 中按稳定 task ID 输出。

主要函数按职责分为：输入闭集校验（`read_satellite_ids`、
`read_compute_profile`）、整数预算分配（`largest_remainder`、
`bounded_weighted_allocation`）、稳定节点/类别/时间分配，以及最终 TaskTrace 和
summary 写出。对应单元测试见
[test_workload_generators.py](../../tests/unit/test_workload_generators.py)。
