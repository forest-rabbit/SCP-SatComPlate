# C800 正式任务输入

66星、1000 s、800任务（dense/sparse/compression/LLM为240/240/240/80），
INPUT精确81.75 GB、RESULT 44,076,569,084 B、总工作量183,958,466 WU。
`compute-profile.json` 为全66星100,000 WU/s；`task-trace.json` 复用G1 C800业务属性，
固定seed `n4c-g1-66`，1..600 s均匀分层到达，无地理热点。
模型来源、rho/sigma/H与LLM结构假设见[工作量模型](../../../../../docs/n4c/workload-mapping.md)。
状态参数仍由G1共享模型/预览消费，未作为checkpoint对象写入TaskTrace。

项目根目录使用项目环境构建后运行：

```bash
source .venv/bin/activate
./ns3 build
./ns3 run --no-build "satcompute --simulationDuration=1000 \
  --computeProfile=contrib/satcompute/input/examples/leo-66-1000s-n4c/compute-profile.json \
  --taskTrace=contrib/satcompute/input/examples/leo-66-1000s-n4c/task-trace.json \
  --computeDeadlineFactor=1.3 --faultMode=none --outputDir=output/n4c-none"
```

默认10 Gbps ISL、size-aware分包、capacity-aware HRW；额外链路指标需显式 `--linkMetrics=1`。
输入重生成使用现有 `generate-task-workload.py --profile=n4c-c800`，提供原生66星的
`--nodes-file`、本例 `--compute-profile`、`--seed=n4c-g1-66`，以及两个输出参数
`--output-task-trace`、`--output-workload-summary`；不要覆盖已有实验结果。
图像1 GB需15 s，LLM需5..10 s；计算deadline从首次开始计算起算，详见[任务模块](../../../task/README.md)。
