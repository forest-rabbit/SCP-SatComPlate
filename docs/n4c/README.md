# N4C 最终实验合同

当前唯一正式场景为 66 星、800 任务、1300 秒。G3 场景与 G4 解析评估均已人工接受；
G3/G4 已经由 PR #90/#91 合入 n4c。当前进行 N4 最终收尾，尚未合入 main，
唯一一次 [N4 release validation](reviews/N4-final-release-validation.md) 已通过；
阶段 CI 和最终人工审阅通过前不声明 N4 COMPLETE。

- 任务：240 dense / 240 sparse / 240 compression / 80 LLM，705 普通图像
  TN(240,130;50,1000) MB，10×500 MB 和 5×1 GB 固定锚点，到达 1..1050 s。
- INPUT=193526895311 B，RESULT=99846517485 B，WU=351623833；
  每星 100000 WU/s，首次计算开始建立 1.3 倍参考服务时间的 deadline，非抢占 FCFS。
- 网络：10 Gbps、fixed 单向 1 ms、20 s 更新，capacity-aware HRW + size-aware。
- 放置：原生 ECEF 位置，hotspot_weight=64、regional_candidate_limit=1；
  workload_seed=n4c-g1-66，placement_seed=n4c-g3-hotspot。
- 故障：randomSeed=1/randomRun=11；F1 beta=10/gamma=1.5，F2 空间模型及参数不变，
  F3 controlled node62/time1027.055770726 s；只有 none/generate 生产模式。
- G4：默认关闭的只读 shadow，387 START、382 ON、77/82 F1/F2 victim 当刻已 ON；
  net lifecycle saving=94.716%，仅为理想资源下解析结果，不是真实备份收益。

正式入口：
[LEO-66 实验](../../contrib/satcompute/input/experiments/leo-66/README.md)、
[场景冻结](reviews/G3-final-freeze.md)、
[G4 冻结](reviews/G4-final-freeze.md)、
[G4 最终证据](reviews/G4-shadow-decision-evaluation.md)。
当前 S/W/K/RESULT 与 sigma/H 合同见[任务生成模块](../../contrib/satcompute/tools/generation/README.md)；
运行生命周期与风险接口分别见[task](../../contrib/satcompute/task/README.md)和
[fault](../../contrib/satcompute/fault/README.md)。

历史 G3 提交 db51fe874 保留 8 ms 来源；当前参考是已接受的 1 ms 场景。
内部冻结 tag 仅为开发引用，最终发布后追溯使用 commit/PR 与 n4-complete。
旧候选及审阅过程由 Git 保存，不再作为活文档维护。冻结输入与本地原始运行输出未改写，
不使用 SHA-256。N5A 尚未开始：真实 checkpoint、备份流量、容量预留和恢复执行
必须另行实现，正式 N5 模块不得依赖 G4 validation tool。
