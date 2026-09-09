# G3 正式场景冻结索引

**G3 STATUS = PASS / FROZEN**。人工批准日期：2026-09-09。
当前唯一默认为已接受的 **fixed 单向 1 ms** 场景。
历史标签 `n4c-g3-frozen` 指向 `db51fe874ae8bd9dd375325063b2ada25c742152`，
保留 8 ms 历史，不移动；清理后的引用见[G4 冻结](G4-final-freeze.md)。

## 正式输入和生成

全部文件位于
[leo-66-1300s-n4c-g3-truncnormal-v3](../../../contrib/satcompute/input/examples/leo-66-1300s-n4c-g3-truncnormal-v3/)：

| 文件 | 职责 |
|---|---|
| `task-trace.json` | 800 任务及源/计算/结果端点，实验直接读取 |
| `compute-profile.json` | 66 星统一 100000 WU/s；从旧目录原样迁入 |
| `placement-manifest.json` | 原生位置与热点放置来源 |
| `f3-manifest.json` | controlled F3 节点/纳秒时刻及历史来源 |
| `workload-summary.json` | 工作量、变量状态、固定锚点 ID 的原始预算摘要 |

- 240 dense / 240 sparse / 240 compression / 80 LLM；705 普通图像
  TN(240,130;50,1000) MB，10×500 MB、5×1 GB 固定 ID。
- INPUT=193526895311 B，RESULT=99846517485 B，WU=351623833；
  到达 1..1050 s，仿真至 1300 s（排他终点），到达结束后继续故障检查与恢复。
- workload_seed=n4c-g1-66；placement_seed=n4c-g3-hotspot；
  hotspot_weight=64 是选择权重，不是卫星 ID；regional_candidate_limit=1。
- [最终生成器](../../../contrib/satcompute/tools/generation/README.md)从原生 1 s 位置切片重建；
  不读取冻结 TaskTrace 作为生成源。冻结清理已验证双次生成及与正式 TaskTrace 逐字节一致。

## 平台参数与已接受结果

[para.cc](../../../contrib/satcompute/para.cc)默认：
原生 synthetic-66、orbitStartOffset=0、maxIslDistance=6171353 m、
10 Gbps / 1 ms / 20 s 网络更新，1 s 指标；
capacity-aware HRW、size-aware、MTU64028 B、队列1500000 B、receiver131072 B、
deadline factor1.3、ecmpHashSeed1、randomSeed1/randomRun11。

[fault-para.cc](../../../contrib/satcompute/fault/fault-para.cc)：
F1 beta10/gamma1.5，17→30°C 忙碌30 s，30→17°C 冷却4 s；
F2参考SEU强度0.002859196111093899/s、映射概率0.5、恢复8 s；
F3 node62/time1027.055770726 s 永久整星失效。其余参数以该文件为准。

1 ms 原始证据：`output/n4c-g3-delay-1ms-20260909/`。
none 800/800 完成；generate 717 完成、83 失败；
F1/F2/F3 START=84/2/1，直接 RUNNING victim=82/0/1。
无丢包/截断，资源账本归零，故障引起一次即时路由重算；
3338 条模型/预测概率记录四项误差均为0。

F3 task120：207142024 B，故障时完成70.9012146%。
manifest 中的 none 时序仍是原8 ms来源，不伪改为1 ms测量。
固定seed/run不保证增加真实备份负载后F1轨迹不变；本次不是多seed均值、
现实故障率或真实保护性能证明。生成fault trace不是生产replay输入。

原始输出由gitignore排除，保留本地，不随tag上传。旧候选、原生成中间文件及大报告
可从Git历史查阅；当前文档只维护最终合同。[G4证据](G4-shadow-decision-evaluation.md)
证明开启旁路评估后22份真实输出与上述1 ms参考一致。
