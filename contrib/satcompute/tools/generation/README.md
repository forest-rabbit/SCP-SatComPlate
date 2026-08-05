# 任务输入生成器

本目录只生成 TaskTrace，不生成完整平台配置，也不复制 ns-3.48 的轨道传播。
星座与任务仍然分开：先运行 `topologyOnly` 得到节点切片，再把任意一个
`nodes_<time>s.json` 和独立 ComputeProfile 交给生成器。

```bash
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --nodes-file=/tmp/satcompute-topology/topology/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-count=100 --total-input-bytes=500000000 \
  --seed=example \
  --arrival-start-ns=0 --arrival-end-ns=1000000000 --arrival-mode=uniform \
  --output-task-trace=/tmp/tasks.json \
  --output-workload-summary=/tmp/tasks-summary.json
```

相同节点、算力、seed 和参数生成逐字节相同的 TaskTrace。任务中的
`input_bytes`、`compute_work_units` 和 `output_bytes` 都会显式落盘。summary
只保存分布统计和实际生成参数，不保存生成器版本、规则版本或文件哈希。
