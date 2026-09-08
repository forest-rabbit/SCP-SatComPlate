# N4C G2：正式输入与运行基线审阅

G2（N4C-2/3）完成后在此暂停，不进入 G3；本阶段不运行 CI、不创建/合并 PR。
依据工作区任务书 `Codex_N4C_G1_Final_Review_and_G2_Implementation_v2.md`。

## 阶段身份与变更

G1 最终批准提交 `a6faf14ee`，G2 从已收口的 `5705690d4` 建立
`feature/n4c-g2-input-runtime`。实现提交：

- `9b61ce6fa`：C800 输入、task_profile、计算 deadline、资源/生命周期指标。
- `5372ad9d0`：移除生产 replay、增加真实状态只读查询、迁移回归和审计比较。
- `023a7fdcb`：F1/F2 都关闭时不虚构模型检查，补充输入/指标字段说明。

上述各提交后工作区均干净；最终 HEAD 以本报告所在分支的 `git rev-parse HEAD` 为准。
修改范围为平台入口/para、task、fault、metrics、输入及对应生成器/测试/README；
完整文件清单可用 `git diff --name-only 5705690d4..HEAD` 获取。
未修改 G1 的 `task_workload_model.py` 或 `preview-n4c-workload.py`，未改 F1/F2/F3
物理模型和内置强度参数，也未修改 ns-3 上游模块或 SCP-TaskModeling。

## 正式输入

文件位于
[`leo-66-1000s-n4c`](../../../contrib/satcompute/input/examples/leo-66-1000s-n4c/README.md)：
`task-trace.json` 和 `compute-profile.json` 均已提交，可直接运行。

| 类别 | 任务数 | INPUT（B） | RESULT（B） | WU |
|---|---:|---:|---:|---:|
| dense-image | 240 | 25,185,996,688 | 25,185,996,688 | 37,779,103 |
| sparse-inference | 240 | 21,821,017,816 | 40,784,755 | 32,731,646 |
| compression | 240 | 34,742,939,528 | 18,847,395,145 | 52,114,517 |
| llm | 80 | 45,968 | 2,392,496 | 61,333,200 |
| 合计 | 800 | 81,750,000,000 | 44,076,569,084 | 183,958,466 |

1 GB / 500 MB 大图共 10 / 20 个：dense 为 2 / 5，compression 为 8 / 15。
逐任务类型、S、W、RESULT 与 G1 `seed=n4c-g1-66` 的 C800 完全一致。
所有 66 星均为 **100,000 WU/s**；每节点分配 12–13 个任务。
到达窗口为 1–600 s，均匀分层；端点采用稳定、非地理分配，source/result 均不同于 compute，
source 和 result 可以相同。未选择地理热点。

TaskTrace 保留原八个必填字段，新增可选 `task_profile`（四个封闭枚举）。
未知字段、未知类别、显式 null 拒绝；旧 fixture 缺省类别明确为 `UNSPECIFIED`，
其参考算力沿用旧 ComputeProfile，不擅自套用新 WU 尺度。
状态参数未内联或新增 sidecar，仍由 G1 共享模型/预览消费；未生成运行时 checkpoint。
LLM 只使用结构公式，未下载或执行模型。

## 计算 deadline

```text
T0_ns     = ceil(WU * 1e9 / 100000)  # 正式四类
budget_ns = ceil(alpha * T0_ns)     # alpha 默认 1.3，有限且 >= 1
deadline  = first_compute_start_ns + budget_ns
TaskSuccess = ComputeOnTime && ResultDelivered
```

首次 RUNNING 前绝对 deadline 为 -1；首次建立后不能重置。初始 INPUT/排队不计入，
RESULT 传输不占计算预算，但必须完整送达才成功。计算完成恰等于 deadline 仍成功，
不依赖同刻 completion/timeout 的事件 UID 顺序。

未算完到达 deadline 即以 `COMPUTE_DEADLINE_EXCEEDED` 失败，取消计算、释放服务台，
不发 RESULT，不使卫星故障；按时算完的 RESULT 可在 deadline 后送达。FCFS/非抢占
不变。取消及截断前的实际计算时间仍计入 busy time。N5 尚未实现，未来接管不得重置
本次建立的 deadline。

专项测试覆盖默认与 CLI alpha=1、取整/溢出/非法倍率、同 ns 完成、慢节点强制超时、
后继任务派发，以及按时计算但 RESULT 被仿真终点截断时不成功。

## C800 无故障完整基线

配置：66 星、1000 s、orbitStartOffset=0、10 Gbps、固定单向 8 ms、
20 s 网络更新、1 s 链路指标、capacity-aware HRW、size-aware 分包、
MTU 64,028 B、单向队列 1,500,000 B、socket 缓冲 131,072 B；
seed/run/路由 seed 均为 1。未放大队列或调整业务量以促成成功。

先完成 20 任务 / 40 transfer 小样本（alpha=1.3 和 1 都成功），再运行完整 C800 两次。
原始证据：`output/n4c-g2-20260908/{none-a,none-b}`；两次启动提交均为干净的
`9b61ce6fa`。分别耗时 444.96 s / 448.91 s；13 份业务/链路文件逐字节一致，
`run-summary.json` 排除墙钟后也一致，见同目录 `none-comparison.json`。

- 四类分别完成 240 / 240 / 240 / 80；800 个任务均经历 INPUT、排队、计算和 RESULT，
  800 次计算按时完成，800 个 RESULT 完整送达；失败、截断、超时均为 0。
- 1600 个传输全部完成；发送/接收应用字节均为 125,826,569,084 B，
  4,286,805 个 UDP 包，FlowMonitor 丢包及设备队列丢弃均为 0。
- 排队：中位数 0 s，均值 0.162024 s，P95 0.316473 s，最大 9.297349 s。
- 端到端：均值 2.664386 s，P95 8.721033 s，P99 16.299032 s，最大 17.941207 s；
  最后任务在 601.145701 s 完成。
- 纯计算最长 15 s；LLM 为 5.137–9.891 s，中位数 7.713 s。
  compute slack 为 0.079788–4.5 s。
- 节点总 busy time 1839.58466 s，单节点 12.14953–56.6728 s，
  平均/最大算力利用率 2.78725% / 5.66728%，最大排队长度 2。
- 242 条定向链路、242000 条窗口记录；全程平均链路利用率 0.198510%，
  覆盖业务活动的 [1,602) s 为 0.330300%；最大单链路窗口 80.0375%。
  FlowMonitor 有效观测窗口吞吐量 1679.244989 Mbps（非 1000 s 全程平均）。
- 最大队列 64,030 B；7 个传输发生容量等待，最大 0.127989 s。
  全程容量预留比例 0.341104%；结束时 reservation、待准入、队列均清空。

完整分类型生命周期/时间分布与逐节点账本在每轮 `n4c-summary.json`。
低平均利用率与短时高峰同时存在，不等于已证明备份带宽足够。

## replay 清理与共用能力

| 类别 | 本次处理 |
|---|---|
| removed | replay CLI 值/输入路径、ReadFaultTrace 及其专属解析代码、生产 Configure(trace) 文件调度入口、3 份文件回放 fixture |
| retained | FaultController、事件结构、writer、同刻排序、真实 F1/F2/F3、独立 RNG、任务终止与 F3 拓扑联动、纯概率函数 |
| migrated | 回归改为同 seed 重复 generate 与 audit off/on；原生 F3 取代 smoke 的文件输入；当前 README 不再提供 replay 命令 |
| test-only replacement | `tests/support/fault-injection.h` 在 C++ 测试内安排直接 ns 事件；无 reader/CLI，不作为正式输入 |

独立 `FaultPredictionEngine` 仍保留为可选 **generate 审计器**，用于检验独立状态
演化与真实模型的一致性；不是 replay 服务，也不是在线查询所依赖的状态源。
历史校准记录中的旧 replay 证据不改写为新实验结果。

## 只读在线风险查询

接口及字段见 [fault README](../../../contrib/satcompute/fault/README.md#在线节点风险查询)。
`QueryComputeRisk(nodeId,horizonNs)` 返回 nodeId、asOfTimeNs、horizonNs、
AVAILABLE/UNAVAILABLE/NOT_READY、pF1/pF2/pCompute、checkCount、permanentlyUnavailable。

概率使用同一 `(now,now+horizon]` 时间窗口；默认 1 秒 horizon 对应下一次 1 秒检查，
不是整个任务剩余时间概率，也不是 NOTICE 阈值。
当前忙闲条件固定，F2 按原生轨道外推，`pCompute=1-(1-pF1)(1-pF2)`，F3 单独表示。
无 NOTICE、空闲或 RUNNING 均可查询；故障节点返回不可用和空概率，未知/未就绪返回
NOT_READY。F1/F2 都关闭时 pF1/pF2/pCompute=0、checkCount=0，不声称没有 F3 风险。

单元测试验证同刻重复幂等、非法/溢出 horizon、1 ns/1 s/2 s 窗口、
与纯模型及下一检查点真实概率吻合、查询不变温度/风险/RNG/事件、审计关闭仍可查询，
以及 query/audit 不改变整轮故障序列。共享时间戳须在模型事件后读取，才能观察刚发生的故障；
接口不窥视尚未执行的同刻事件、未来 TaskTrace 或 F3 调度表。

## C800 generate 确定性与审计对照

使用相同 C800 和上述网络配置，显式启用原有 F1/F2/F3、seed/run=1/1；
不改内部参数，不为事件数筛选 seed。两轮为
`output/n4c-g2-20260908/{generate-off,generate-audit}`，启动时都是干净的
`5372ad9d0`。随后 `023a7fdcb` 仅补充 F1/F2 均关闭的查询分支及测试/说明，
不改变这两轮均开启 F1/F2 的生成执行路径。耗时分别为 452.35 s / 463.48 s。

两轮 16 份业务、链路、故障文件逐字节相同，run-summary 仅排除墙钟后相同；
列表及断言见 `generate-comparison.json`。审计关闭无三个概率文件，开启才产生。

结果为：

- 766 个任务完成，34 个失败，无未终结任务、无计算超时。
- F3：卫星 62 在 33.469258100 s 永久失效，导致后续 33 个任务失败
  （compute 端 12、source 端 11、result 端 10）；不复活旧任务。
- 另有 3 次可恢复 compute START、5 个 risk-only episode；其中计算故障影响 1 个任务。
  事件数不等于失败任务数，不以这轮结果冻结 G3 的标定目标。
- 1600 个 transfer 中 1532 个完成、68 个取消；本例失败任务均未开始计算，
  INPUT/RESULT 各取消一个。无 FAILED 网络传输、无 FlowMonitor/队列丢包。
- F3 导致一次即时路由重算；含初始计算共两次，周期更新仍为 50 次。
  Capacity-aware/Size-aware 最终账本归零。
- 总 busy time 1771.79857 s，与逐任务实际计算时间之和一致。
- generate 审计的在线真值与独立预测共 **25 条**，四个概率字段 MAE/RMSE/最大误差均为 0，
  无缺失或上下文差异，见 `generate-probability-comparison.json`。
  25 条是 NOTICE 有效且任务 RUNNING 的审计记录，不代表只支持查询 25 次，
  也不是故障发生率或现实预测准确率。

允许输出目录、日志路径、墙钟变化；不允许 fault、task、transfer、拓扑动作或链路业务变化。
原始输出保留在本地忽略目录，不将大批 CSV 或日志加入仓库。

## 验证与复现

项目 `.venv` 环境下完成；配置保持 SatCompute 及其依赖，ns-3 examples/tests 均 OFF：

```bash
source .venv/bin/activate
./ns3 build -j 4
contrib/satcompute/tests/unit/run-cpp-tests.sh
PYTHONDONTWRITEBYTECODE=1 python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

结果：**34 个 Python 测试、15 个 C++ executable、6 组 smoke、4 组 regression 全部通过**。
旧 F1-only/F2-only/F1+F2 概率一致性仍匹配 41/126/126 条，100 任务联合场景仍为
93 完成、7 失败及 82 条概率一致。没有运行全局 ns-3 test.py、examples 或 GitHub CI。

完整 C800 使用以下命令（为每轮选择不存在的输出目录）：

```bash
python contrib/satcompute/tests/integration/regression/run-n4c-baseline.py \
  --output-dir=output/n4c-none-a
python contrib/satcompute/tools/validation/summarize-n4c-baseline.py \
  --run-dir=output/n4c-none-a

python contrib/satcompute/tests/integration/regression/run-n4c-baseline.py \
  --fault-mode=generate --output-dir=output/n4c-generate-off
python contrib/satcompute/tests/integration/regression/run-n4c-baseline.py \
  --fault-mode=generate --audit --output-dir=output/n4c-generate-audit
python contrib/satcompute/tools/validation/compare-n4c-runs.py \
  --left=output/n4c-generate-off --right=output/n4c-generate-audit \
  --output=output/n4c-generate-comparison.json
python contrib/satcompute/tools/validation/compare-fault-probabilities.py \
  --model=output/n4c-generate-audit/fault-model-probabilities.csv \
  --prediction=output/n4c-generate-audit/fault-predictions.csv \
  --detail=output/n4c-probability-comparison.csv \
  --summary=output/n4c-probability-comparison.json
```

各轮 `execution.json` 保存实际展开命令、提交、工作区状态与 seed/run，
`execution-result.json/time.txt/run.log` 保存执行结果。none 的重复运行同样使用
`compare-n4c-runs.py`。对比脚本只由测试显式调用，不加入正常运行流程。

## G3 待用户确认，尚未实施

- 地理热点区域及权重，不依据 G2 随机终态反推；
- C800 的受影响任务数接受范围，不能继续沿用旧 150/1500 目标；
- F1/F2/F3 的期望贡献、F3 K、强度标定和独立 seed 规模。

本阶段未实现 checkpoint、L1、remote batch/tail、恢复状态机、备份节点选择、
频率优化或 cL/cR 标定。完成报告并推送 G2 分支后 **STOP AT G2**。
