# Pre-N5C：CB-Sat v2 工程验收

2026-09-13，分支 `feature/pre-n5c-cb-sat`，从 `n5@2ccfa392e` 开始。
G0–G7 本地实施与验收连续完成；尚未发布 PR、合并、打 tag 或运行 GitHub CI。
这是完整 INPUT、单备份节点、逐份保存日志、无 tail 的卫星适配，不是原 CheckBullet
训练系统的逐项复现，也不是 N5C 节点选择算法。

## 执行证据

| 阶段 | 实际执行及证据 |
|---|---|
| 独立标定 | `output/cb-sat-v2/20260912T163425212424Z-calibration`；HEAD `a47128a1d`，10 次完整 1300 s，seed1/run101–110，off/F1/F2，F3 关闭 |
| 参数冻结 | 33430 次有效检查、33430 s 等效暴露、806 次联合故障，覆盖 64 星；`MTBF=41.47642679900744 s` |
| 八组小场景及重复 | `output/cb-sat-v2/20260912T170241158297Z-smoke`；HEAD `1a43a1431`，四 placement × 两 busy，加 FA-LRL/relocate 重复，26 个文件确定性匹配 |
| 八组正式场景 | `output/cb-sat-v2/20260912T171143276614Z-formal`；统一干净执行 HEAD `494e133c5`，全部 1300 s、退出码 0；单组墙钟 3091.8–3345.0 s，最多八组并行 |
| 最终代码小场景复核 | `output/cb-sat-v2/20260912T182403752476Z-smoke`；HEAD `17fd3cc49`，八组及重复通过；与原 smoke 每组 26 个文件确定性匹配，排除执行身份/路径/墙钟，离线审计使用同一版本 |
| 最终审计 | 上述正式目录的 `run-manifest.json`、`cb-sat-matrix-summary.csv/.json`、每组 `cb-sat-audit.json`；分析提交与执行提交分开记录 |

正式输入没有修改：66 星/66 计算星、800 任务、INPUT=194119753287 B、
RESULT=100166291859 B、352513119 WU、LLM=400 WU/token、100000 WU/s、10 Gbps、
fixed 1 ms、20 s 网络切片、原 compute deadline factor=1.3、每星 10 GB 备份池。
seed1/run11、F1/F2/F3 和 node62/1027.055770726 s 的 F3 保持冻结。

冻结 profile 在 `contrib/satcompute/protection/policy/baseline/checkbullet/calibration/`；
逐 pilot/逐节点统计均保留。32 次范围外故障不进入标定分子；连续主计算服务
32897.853004236 s 仅作诊断，不替代离散采样暴露。run11 未用于拟合。

首次自动审计误把平台 `PARTIAL`（有任务失败）当成仿真截断，导致四组暂记 AUDIT_FAIL。
修正仅在离线检查：退出码、1300 s 和任务全部终态仍须满足，不能要求 800/800 才通过。
原始运行及 `initial-matrix-status.json` 保留，重新审计八组全部 PASS。

运行结束后还加强了非法恢复计划的拒绝：B 上对象必须对应冻结 ID；C 上对象必须有
真实完成的 B→C FULL/LOG 传输。八组原始记录逐项通过新增来源断言，正常已执行路径
没有改变；未改 H/X、成本、输入、故障或已记录数值。正式表始终标执行 `494e133c5`，
不将后续审计提交伪称为仿真执行版本。

## 八组正式结果

`active = execution waste + normal protection`；`total = active + reserved idle`。
后两者单位为百万 eq-WU；idle 是容量机会成本，不是实际 CPU 计算。
额外流量为正常保护及故障恢复的实际应用发送量，单位十进制 GB，包含取消前已发量。

| placement | busy | 完成/失败 | active（百万 eq-WU） | total（百万 eq-WU） | 额外 GB | 平均链路利用率 |
|---|---|---:|---:|---:|---:|---:|
| FFP | recompute | 795 / 5 | 5.988402 | 6.316027 | 300.994 | 0.656686% |
| FFP | relocate | 800 / 0 | 2.326332 | 2.719637 | 301.584 | 0.656654% |
| LRL | recompute | 797 / 3 | 4.938801 | 5.162965 | 299.753 | 0.700952% |
| LRL | relocate | 800 / 0 | 2.349877 | 2.618361 | 300.090 | 0.701328% |
| FA-FFP | recompute | 795 / 5 | 5.989802 | 6.317627 | 300.985 | 0.659979% |
| FA-FFP | relocate | 800 / 0 | 2.327732 | 2.721237 | 301.575 | 0.659948% |
| FA-LRL | recompute | 798 / 2 | 3.660245 | 3.884104 | 299.753 | 0.701663% |
| FA-LRL | relocate | 800 / 0 | 2.350467 | 2.599335 | 299.869 | 0.701983% |

每组 83 次主任务恢复机会；全部接受。四个 recompute 组的失败均为 REMOTE_BUSY 后
从零计算超过原 compute deadline：FFP/FA-FFP 为 114、252、450、456、475；
LRL 为 46、114、252；FA-LRL 为 114、252。没有增加第二次恢复或放宽 deadline。
其他任务与传输均终态，所有 CB 存储、服务预留、活动 placement 负载最终归零。

八组均记录 84 次 F1、2 次 F2、1 次 F3 START；实际运行中主任务受害者为 82 次 F1
及 1 次 F3。这两次 F2 没有产生运行中主任务恢复样本，不据此宣称正式 F2 恢复成功率。
受控 F2 主任务恢复另由小测试覆盖。

FA-LRL/relocate：74 次 DIRECT、2 次 RELOCATE、7 次 RECOMPUTE，83/83 恢复成功。
两次迁移为 task114（0→6）和 task252（0→1）。四类恢复数为 dense 13、sparse 24、
compression 31、LLM 15；全部成功。task120 的 F3 从 q=133694 WU 恢复，Wf=172849 WU，
实际重做 39155 WU、追平 0.393550001 s，无 tail。

## 与旧方案的同口径对比

旧 32 组只读复用 `output/pre-n5c-placement-final`，执行 HEAD `b51cc9d63`。
共享状态/成本、机制、存储、恢复、路由、故障、任务和输入代码保持不变；两处公共
policy 头文件仅注释变化。入口/CMake 只接入独立 CB 分支，旧模式完整维护回归通过。
公共命令参数逐项一致，不重复运行旧 32 组、不混用旧 100 WU/token 或 226 GB 截图。

完整 40 行公共表及恢复附表见正式目录的 `comparison-public.csv`、
`comparison-recovery.csv`、`comparison-replica.csv`、`comparison-deltas.csv` 和 `comparison.json`。
下表固定 FA-LRL；R5/R7/CB 均为 busy-relocate，不能解释为只改变检查点频率的消融。

| 方案 | 完成/失败 | 执行浪费（百万 WU） | 正常保护（百万 eq-WU） | idle（百万 eq-WU） | total（百万 eq-WU） | 额外 GB |
|---|---:|---:|---:|---:|---:|---:|
| R0 Recompute | 742 / 58 | 23.870497 | 0 | 0.522007 | 24.392504 | 6.372 |
| R1 1+1 | 799 / 1 | 322.055966 | 0 | 15.720609 | 337.776575 | 192.209 |
| R5 CompFRR eager | 800 / 0 | 0.723559 | 0.429060 | 0.299935 | 1.452554 | 199.371 |
| R7 CompFRR deferred | 800 / 0 | 0.628148 | 0.440710 | 1.987816 | 3.056674 | 107.725 |
| CB-Sat | 800 / 0 | 1.837977 | 0.512490 | 0.248868 | 2.599335 | 299.869 |

R7 相对 CB：active equivalent cost 低 **54.53%**，额外网络流量低 **64.08%**。
但包含实际等待机会成本后，CB 的 total 比 R7 低 **14.96%**；不能说 R7 在所有浪费
口径都更低。CB 正常保护 297.167 GB、故障恢复 2.702 GB；R7 将 INPUT 推迟至故障后，
正常/故障流量为 82.509/25.215 GB，这解释了两者不同的网络与等待权衡。

| 方案 | 恢复成功/机会 | resume P50/P95（s） | catchup P50/P95（s） | catchup 均值（s） |
|---|---:|---:|---:|---:|
| R0 | 25 / 83 | 0.226346 / 0.423105 | 1.095459 / 2.309292 | 1.087775 |
| R5 | 83 / 83 | 0.016297 / 0.144347 | 0.064746 / 0.418548 | 0.123312 |
| R7 | 83 / 83 | 0.241855 / 0.459364 | 0.280240 / 0.606737 | 0.315176 |
| CB-Sat | 83 / 83 | 0.002000 / 0.225772 | 0.221700 / 0.624130 | 0.251427 |

时间样本为实际到达相应里程碑的任务，包括随后才失败的任务；未到达的任务不填零。
R0 仅有 25 个时间样本，不能忽略其余 58 个失败后作无条件速度结论。R7/CB 此处各 83 个，
CB 平均追平比 R7 短 20.23%，P95 则略长。这里是单场景总体描述，不是多 seed 显著性检验。

1+1 单独统计：800 次申请/准入，83 个主 attempt 故障，82 次接管并完成，1 个双失败。
82 个实际 takeover 延迟均为 0 ns（同纳秒完整 batch 之后），**不是 catchup=0**。
副本实际工作集内存未单独计入 checkpoint 池，R0/R1 的备份池峰值为不适用。

CB FA-LRL 的全程平均链路利用率为 0.701983%，单链路最高全程平均为 5.318216%，
最高 1 s 窗口为 81.883818%；额外备份池单节点峰值 1.960504 GB。R7 对应全程平均
0.431624%、单链路最高全程平均 1.445854%、备份池单节点峰值 0.890773 GB。
应用 Byte、逐跳链路利用率、存储峰值是不同指标，不能互相替代。

## 模型边界与测试

- H 由独立 MTBF 和公共 cL/cR 求解，不跟随实时 F1/SAA 风险。FA-LRL/relocate 的
  H 中位数 9.16%；800 个主任务均有合法周期目标，794 个选到 B，793 个完成初始化。
- 当前读取成本为零，非空日志恢复一次 cR；该组 24386 次 X 决策的恢复上界均不绑定，
  X 由剩余合法事件约束，中位数 14。实际正常合并 694 次；70 次故障 r<q，13 次 r=q。
  这些数字不证明原论文随日志长度增长的恢复代价模型，需要有来源的统一读取模型才能验证。
- 复用公共计算/网络/LocalDelivery/存储/placement/attempt 和状态大小/成本；没有抽取
  新公共 helper，也没有套用整个双层 RecoveryController。CB 自己选根、日志、恢复起点；
  增量应用使用已有原地 Merge，读取权限不因此扩展为 tail。
- 旧 eager 图像保留“剩余 INPUT + K”，deferred 故障后取完整 INPUT；旧 LLM 根只按 KV
  预算计量，不证明 KV 足以恢复真实 prefill/decode。图像依赖独立 tile/文件和线性状态预算。
  本平台不运行真实业务算法，CB 的完整 INPUT 也不等于已经验证任意真实程序可恢复。
  因此本报告是已声明抽象下的系统比较，不提供真实 LLM 恢复完备性或最小状态结论。
- 构建、27 个维护 C++ 程序、128 项 Python（127 通过/1 条件跳过）、完整 smoke 和
  regression 通过；CB 最终受控恢复 26 场景/5079 检查，26 份原始证据审计通过，
  11 类损坏证据被拒绝。包括同一任务的外部 66% 状态不被读取/清理、未传输的伪对象拒绝、
  F2 主任务恢复、完整同 ns fault batch、F3 依赖和仿真截止。
- 当前只批准一套 800 任务正式输入。50/100 GB 工作负载没有批准 manifest，标记
  NOT_PREPARED；没有伪造 24 组覆盖。十个 pilot 和八种配置不能混成十八个成功率样本。

## 可复查命令

以下是本轮实际使用的入口（从仓库根目录启用 `.venv`）；正式与校准均用 `--jobs 8`，
最终 smoke 用 `--jobs 2`。正式及校准均已完成，不需为了审阅重复执行。新运行自动使用新目录，
已有 MTBF profile 会拒绝重新标定覆盖。

```bash
source .venv/bin/activate
./ns3 build -j 2
contrib/satcompute/tests/unit/run-cpp-tests.sh
python -m unittest discover -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
python contrib/satcompute/protection/policy/baseline/checkbullet/tools/calibrate-cb-sat-mtbf.py --stage all --jobs 8
python contrib/satcompute/protection/policy/baseline/checkbullet/tools/run-cb-sat-matrix.py --stage smoke --jobs 2
python contrib/satcompute/protection/policy/baseline/checkbullet/tools/run-cb-sat-matrix.py --stage formal --jobs 8
python contrib/satcompute/protection/policy/baseline/checkbullet/tools/analyze-cb-sat-matrix.py --root output/cb-sat-v2/20260912T171143276614Z-formal
git diff --stat 2ccfa392e..HEAD
```

全量 smoke/regression 的执行版本分别为 G4/G5；后续只增加 CB 测试、来源拒绝和离线审计，
旧模式核心不变。最终 C++/Python 日志在 `/tmp/cb-g7-cpp-final.log`、
`/tmp/cb-g7-python-final.log`，新增 F2 场景在 `/tmp/cb-g7-final-recovery.log`。
Python 唯一跳过项为未指定 `SATCOMPUTE_POSITION_SLICES` 的原生轨道生成器重复实验；
其原因与 CB 无关，不冒充已执行通过。

操作命令、模块文件及参数来源见
[CB README](../../../contrib/satcompute/protection/policy/baseline/checkbullet/README.md)。
保留本分支等待人工审阅，不自动发布或进入 N5C。
