# LEO-66 Final Experiment Scene

这是当前唯一正式的 66 星论文实验输入。平台运行参数在 `para.cc`/CLI，故障内部参数
在 `fault/fault-para.cc`；本目录不引入另一份完整配置，也不实现真实 checkpoint/备份。

## 目录与冻结参数

| 目录 | 文件与职责 |
|---|---|
| `topology/` | `constellation.csv`：780 km、86.4°、6×11、phasing=1、RAAN span=180° 的原生 shell |
| `compute/` | `compute-profile.json`：ID 0..65，每星 100000 WU/s |
| `workload/` | `task-trace.json`：运行输入；`workload-summary.json`：生成来源与组成摘要 |
| `placement/` | `placement-manifest.json`：原生位置放置的可追溯记录，不用于在线读入拓扑 |
| `fault/` | `f3-manifest.json`：controlled F3 节点及纳秒时刻，由正式 runner 校验 |

任务共 800 个：240 dense-image、240 sparse-inference、240 compression、80 LLM。
704 个普通图像任务使用 TN(240,130;50,1000) 十进制 MB；另有 10×500 MB、5×1 GB
固定任务 ID 锚点，以及显式定为 800 MB 的 compression 任务 120。
INPUT=194119753287 B，RESULT=100168131855 B，WU=352513119。
到达窗口 1..1050 s，仿真至 1300 s；compute deadline 为首次开始时参考服务时间的 1.3 倍。

- 网络：每 ISL 10 Gbps、fixed 单向 1 ms、每 20 s 更新；capacity-aware HRW、size-aware 分包。
- 原生轨道偏移 0 s，距离门限 6171353 m；运行时在线算轨道，不回放位置 JSON。
- 放置：hotspot_weight=64、regional_candidate_limit=1；workload_seed=`n4c-g1-66`、placement_seed=`n4c-g3-hotspot`。
- 随机流：ns-3 seed=1、run=11；与生成器的两个字符串种子分开。
- 故障：F1/F2/F3 均开启，F1 beta=10、gamma=1.5；F2 使用现有空间模型及既定参数；
  F3 固定 node62、1027.055770726 s。详细数值与语义见[故障模块](../../../fault/README.md)。

移除原前置任务 801，仅把任务 120 的 INPUT 改为 800000000 B，并按同一模型得到
1200000 WU（参考计算 12 s）、RESULT=433985046 B；原 source54/compute62/result33、
到达时刻 1024.682825747 s 和其他 799 个任务均不变。不要求预热或 F3 前已有余温。
该任务大小经 B 组筛选，是用于验证真实保护与 F3 恢复的**受控场景**，不能当作无偏随机样本
证明 B 普遍优于其他算法。模型、seed/run、F3 节点/时刻不变，F1/F2 正常抽样；
生产策略不读取未来 F3。与原 800 任务或中间 801 任务历史结果不能作严格配对比较。
当前完整 B 组验收为 800 完成/0 失败，任务 120 在 F3 前实际 ON 并使用检查点恢复；
大小初筛及完整验收见[最终 800 任务场景记录](../../../../../docs/n5/reviews/N5B-final-800-task-scene.md)。

G3 原始冻结使用 8 ms，进入 G4 时人工批准改为当前正式 1 ms。
manifest 中旧 8 ms none-source 或旧生成路径仅为历史来源，不是当前 runtime reference。
生成摘要不是配置，正式任务已固化端点与事件时间。

## 运行与生成

在仓库根目录按[总 README](../../../../../README.md)配置 uv 环境并构建后，正式 runner：

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --output-dir=output/leo-66-run
```

默认执行 generate + F1/F2/F3，概率审计和 shadow 均关闭；显式加 `--audit --shadow`
开启只读验证，`--fault-mode=none` 可用于另行授权的无故障实验。输出必须是不存在的新目录。
N4 release validation 只允许运行一次正式 generate+audit+shadow；不能顺带追加 none/多 seed。

[最终任务生成器](../../../tools/generation/README.md)从原生 1 s 位置切片生成同一 workload。
位置切片由平台 topology-only 导出并放到 output/外部目录，不复制进正式输入。
已有冻结任务不因重新生成而覆盖，生成两次及与正式 TaskTrace 的逐字节一致性由
[长期测试](../../../tests/README.md)验证。

[G4 验证器](../../../tools/validation/compfrr-shadow/README.md)只做解析/虚拟账本评估，
不发送备份、不占实际计算或网络资源，不能将 shadow 指标称为真实备份性能。
N4 最终验证证据见[Release Validation](../../../../../docs/n4c/reviews/N4-final-release-validation.md)。
