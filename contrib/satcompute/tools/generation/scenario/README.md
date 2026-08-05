# 独立输入组合器

本目录保留 ns-3.33 的 `generation/scenario` 位置和主要文件名，但 v0.3 不再生成
或读取“完整 scenario 运行配置”。平台参数仍只来自 `para.cc`/CLI，星座物理结构
仍只来自 constellation JSON；这里仅把已经独立生成的业务输入复制成一个可校验
的证据 bundle。

组合器首先校验 topology trace 的 `manifest.json` 和全部切片哈希，再从第一份
manifest-listed nodes 切片取得稳定 satellite ID。它不复制拓扑、不选择邻星、
不计算 XYZ，也不把 topology trace 作为平台的第二个配置源。

## Task 模式

```bash
python3 -m contrib.satcompute.tools.generation.scenario.generate_scenario \
  --bundle-name=task-demo \
  --topology-trace=/tmp/topology-trace \
  --compute-profile=/tmp/compute-profile.json \
  --task-trace=/tmp/tasks.json \
  --output-dir=/tmp/task-bundle
```

ComputeProfile 与 TaskTrace 必须同时提供。组合器检查稳定 ID、计算节点子集、任务
端点关系、唯一 task ID、正数工作量/字节数，以及 `arrival_time_ns` 早于 topology
trace 的仿真结束时间。

平台仍通过独立 CLI 使用 bundle 文件：

```text
--computeProfile=/tmp/task-bundle/compute-profile.json
--taskTrace=/tmp/task-bundle/task-trace.json
```

## NetworkTransfer 模式

```bash
python3 -m contrib.satcompute.tools.generation.scenario.generate_scenario \
  --bundle-name=transfer-demo \
  --topology-trace=/tmp/topology-trace \
  --transfer-trace=/tmp/transfers.json \
  --output-dir=/tmp/transfer-bundle
```

Task 模式与直接 transfer 模式互斥，与平台当前 para 合同一致。平台使用：

```text
--transferTrace=/tmp/transfer-bundle/transfer-trace.json
```

## 输出合同

输出只包含 `input-bundle-manifest.json` 和当前模式需要的数据文件。数据文件逐字节
复制，manifest 记录：

- topology manifest 与 constellation 配置的 SHA-256；
- 仿真时长和稳定 satellite ID；
- 每个复制文件的固定文件名和 SHA-256；
- workload mode 与重新计算的条目数；
- 预留但固定为 `null` 的 `fault_trace`。

`input-bundle.schema.json` 为每个生成字段提供说明。checker 同时执行 closed-world
目录检查，任何额外文件、子目录、哈希变化或业务关系错误都会失败：

```bash
python3 -m contrib.satcompute.tools.generation.scenario.check_scenario \
  --bundle-dir=/tmp/task-bundle
```

输出目录使用同父目录临时目录并在完整校验后原子改名；已存在目录不会覆盖。相同
bundle name、topology manifest 和业务输入字节会得到相同 bundle 字节。

`compute_placement.py` 与 `compute_profile.py` 保留旧版确定性计算节点放置和
ComputeProfile 构造/校验工具，供以后任务生成流程复用。旧版
`config/synthetic-*.json` 将仿真、拓扑和计算参数混在一个完整 scenario 中，
因此不迁移。

故障模型尚未实现。以后增加时，`fault_trace` 必须作为独立输入由卫星位置/拓扑
证据生成；平台仍需在精确 ns 故障时刻立即禁用并重算路由，而不是等待下一个
周期切片。
