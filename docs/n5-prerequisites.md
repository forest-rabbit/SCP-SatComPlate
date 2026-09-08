# N5 前置基础：任务增量与链路压力基线

截至 2026-09-08，进入 N5 备份算法设计前已有两组独立证据：SCP-TaskModeling 的
真实任务增量测量，以及本平台的无故障、无备份 10 Gbps 压力基线。本页归档可引用的
量、计量口径和适用边界，不是 N5 实现合同，也不表示这些任务参数已接入平台。

## 三类任务的实测参数

下表统一使用 `rho = rho_variable`，表示“可变恢复状态字节数 / 任务输入字节数”；
`K_variable` 包含结果 payload 和恢复位置索引，不含每个 checkpoint 重复支付的
应用层固定头 `H`。数值来自已合入 SCP-TaskModeling `main` 的参考实验。

| 任务类型 | 参考输入 S（B） | K_variable（B） | rho | H / checkpoint（B） |
| --- | ---: | ---: | ---: | ---: |
| 稠密图像处理 `dense-image` | 52,428,800 | 52,429,200 | 1.0000076294 | 65 |
| 稀疏检测 / 推理 `sparse-inference` | 26,246,291 | 49,056 | 0.001869064 | 85 |
| 压缩 / 编码 `compression` | 52,428,800 | 28,441,644 | 0.5424813080 | 65 |

参考条件与边界：

- 稠密图像：Sentinel-2 四波段 `uint16`、50 MiB 原始数组，按 `256 × 256 × 4`
  tile 处理，采用专用 Block-Sparse 增量格式。每块 payload 为 524,288 B，索引
  为 4 B，因此当前表示下 `rho` 由结构决定，不需要内容分位数；更换表示需重新测量。
- 稀疏推理：DOTA128 排序后的前 100 张图，YOLO11n OBB、CPU、`confidence=0.25`。
  输入字节数为图像文件大小之和，不是解码后的像素数组大小。1,856 条检测记录各
  26 B，每张图另有 8 B 索引。相同阈值的 20 张 pilot 为 `rho≈0.003443570`，说明
  内容和检测密度会影响比例，不能把参考值当成所有任务的固定规律。
- 压缩编码：同一 Sentinel-2 参考数组，每个 tile 独立无损 JPEG2000 编码，
  每段有 8 B 索引。JP2 自身容器头属于 payload，外层 checkpoint 头才属于 `H`。
  同景 10 个 50 MiB 窗口的 `rho` 分位数为 **p10=0.4772119770、p50=0.5280219269、
  p90=0.5452593155**；它们不是跨地区、日期或传感器的全局分布。全部样本保留，
  包括约 61.3% 为 nodata 的低比例窗口；参考值 0.5424813080 不等于样本中位数。

三类任务都验证了 5% / 10% / 20% checkpoint 粒度，同一任务的 `K_variable` 和
`rho` 不变，完整字节成本的变化来自 checkpoint 数量。对应粒度实验分别完成
12 / 9 / 12 次故障恢复验证，恢复后的规范结果均与无 checkpoint 基线一致。
这证明的是任务状态可恢复，不是平台已经支持网络备份或接管。

## 如何解释这些量

对固定任务和等长 checkpoint 头，完整任务的实测字节账为：

```text
K_variable = payload 总字节 + 恢复索引总字节
rho = K_variable / S
K_total = K_variable + checkpoint 数量 * H
```

后续若采用按输入进度线性增长的预算模型，可用“本次处理的输入字节数 * rho + H”
估算单次增量。例如进度从 10% 到 20%，新增处理量为 `S * 0.10`。这是平均比例
近似；稀疏推理和压缩的实际区间大小随内容变化，不能声称每段实测比例完全相同。
此解释不冻结任务计算量到输入进度的映射，也不冻结 N5 的 checkpoint 策略。

`H` 的 65 / 85 B 是上述实验 task ID 下的值，并非平台通用常量：稠密图像与压缩
为 `44 + task ID 的 UTF-8 字节数`，稀疏推理为 `48 + task ID 的 UTF-8 字节数`。
`rho_total` 已包含重复头，不能再按它估算后重复加 `H`；IP/UDP 等网络协议头也
不属于这里的 `H`，传输成本须由平台另行统计。

## 本平台的 10 Gbps 压力基线

2026-09-06 的 66 / 351 / 720 星正式运行，每组均完成 1500 个任务和 3000 个
transfer，6,510,481 个 UDP 包全部收到、零丢包，预留账本最终归零。全程平均链路
利用率分别为 0.1668% / 0.1279% / 0.1619%，单链路最大 1 s 窗口利用率约 80%。
因此不能只凭低全网平均值认定备份没有拥塞成本。

本轮保留历史 75% 负载档位，但不是旧任务逐项重放；三个规模的到达时间跨度不同，
不能据此单独归因带宽变化或规模变化。测试仍使用原任务生成规则，**没有应用上表
的新任务增量模型**。物理链路利用率与容量预留比例分开统计，正常运行的链路 CSV
仍默认关闭。完整输入、吞吐量、等待与运行成本见[10 Gbps 压力基线](pressure-10g-baseline.md)。

## 进入 N5 时尚需确定的内容

已有 N4A/N4B 的[故障执行与因果概率预测](../contrib/satcompute/fault/README.md)，
现在补齐任务增量与网络基线的参考证据。以下内容仍未完成，不能从本页数值直接推定：

- 任务输入大小到计算量（WU）的映射，以及最终 `sigma` 和 `sigma/H` 的计费口径；
  三类任务当前均未输出最终 `sigma`，不能将微基准墙钟耗时直接换成卫星计算量。
- 将三类图像任务及后续 LLM 标签、参数接入平台；LLM 的 KV-cache 与 `sigma`
  尚未完成标定，不能套用上述按图像输入字节定义的 `rho`。
- 备份触发与节点选择、checkpoint 传输与提交、故障恢复和接管，以及有备份时的
  成本与收益验证；本轮压力通过不代表 N5 算法已实现或其参数已最优。

## 可追溯证据

- 平台：[PR #86](https://github.com/forest-rabbit/SCP-SatComPlate/pull/86)，合入提交
  `a9bf4ad16`；[阶段 CI](https://github.com/forest-rabbit/SCP-SatComPlate/actions/runs/34014479346)
  已通过。复现命令和本地输出位置见压力基线文档。
- 任务测量：[PR #5](https://github.com/forest-rabbit/SCP-TaskModeling/pull/5) 合入后的
  `0dbc0c7` 快照，包含此前三类任务结果。以下链接固定到该提交，避免后续参数变动
  改写本次依据：[计量合同][measurement]、[稠密图像][dense]、[稀疏推理][sparse]、
  [压缩编码][compression]；原始输出位置与复现命令保留在各结果文档中。

[measurement]: https://github.com/forest-rabbit/SCP-TaskModeling/blob/0dbc0c7b6281219e1356151fd640336cde885e7d/docs/measurement-contract.md
[dense]: https://github.com/forest-rabbit/SCP-TaskModeling/blob/0dbc0c7b6281219e1356151fd640336cde885e7d/docs/dense-image-pilot-results.md
[sparse]: https://github.com/forest-rabbit/SCP-TaskModeling/blob/0dbc0c7b6281219e1356151fd640336cde885e7d/docs/sparse-inference-pilot-results.md
[compression]: https://github.com/forest-rabbit/SCP-TaskModeling/blob/0dbc0c7b6281219e1356151fd640336cde885e7d/docs/compression-encoding-pilot-results.md
