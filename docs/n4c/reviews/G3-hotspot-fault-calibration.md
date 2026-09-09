# N4C G3：F1 概率/热恢复与 F3 普通参与修订

状态：按工作区 `Codex_N4C_G3_Review_and_F1_F3_Revision_v2.md` 修订中。
仍在 `feature/n4c-g3-hotspot-fault-calibration`；完成后提交/推送并停在 G3 审阅，
不合并 n4c/main、不跑 CI、不进入 G4/N5。只维护本报告，不新增重复任务书。

## 冻结边界与预声明

- C800 类别 dense/sparse/compression/LLM=240/240/240/80；INPUT=81,750,000,000 B，
  RESULT=44,076,569,084 B，WU=183,958,466；业务属性与 G2 到达时刻不变。
- 全 66 星 100,000 WU/s，1000 s，orbitStartOffset=0，10 Gbps，fixed 8 ms，
  capacity-aware HRW、size-aware、20 s 网络更新、1 s 指标/故障检查，deadline factor=1.3。
- 热参数 17/20/30/35°C；持续升温 30 s 到临界，线性冷却 4 s 到基础温度；能源仅乘性修正。
  F1 直接使用当前 1 s 条件概率，移除 lambdaMax。F1 动态恢复 (0,4] s，F2 固定 8 s。
- F2 空间函数及全部参数不变，referenceSeuIntensityPerSecond=0.002859196111093899，
  rho_SF=0.5；F1/F2 仍各自抽样、同刻合并一次停机。生产移除 NOTICE/risk-only 门控。
- 地理热点权重 64、每区域一个最近中心候选；只改端点。受控 F3 为 node 9、task 79、
  2.130334420 s；普通 task 280 在 F3 前以 node 9 为 INPUT 源。none 必须证实传输
  在 F3 前完成；禁止未来坏端点和额外 F3 直接 victim，不给该星屏蔽 F1/F2。

在新模型 C800 开始前预先固定 ns-3 seed=1；**先 beta=4、再 beta=3**，
均用 run 11、F3 off 做完整 pilot。约 79 个不同 F1∪F2 RUNNING victim 是宽松标定目标，
不是配额；在接近目标且无病态重复停机时优先 beta=3，不为了多数 START 超过 50% 调参。
若需 beta=5/6 会保留所有候选结果，不更换任务/seed。选择后先冻结，再运行
calibration=11/12/13、validation=21/22/23（三轮新模型 held-out 在冻结前不查看），
再做 run 11 audit-off 重复；验证集不回调参数。代表图固定使用 calibration run 11。

## 实现与验证

精确推进上一 busy 状态、独立查询不推进真状态、临界钳位、动态恢复自然冷却；
F1/F2 停机只中断 RUNNING，QUEUED/INPUT/RESULT 语义与 G3 原合同保持一致。
START 概率来自当次抽样；可选审计对所有 RUNNING 任务持续记录，正常运行关闭。
已通过纯模型与运行边界测试，包括非整数恢复、同刻双来源取最大时长、F3 抢占
后旧 RECOVERY 不复活卫星。37 Python、15 C++、6 smoke、4 regression 全通过；
当前 beta=4 的旧 100 任务联合输入为 84 完成/16 失败，482 条逐秒概率全部匹配。
这是功能回归，不替代 C800 标定。完整实验结果待本轮执行后填入。

## 历史与限制

旧模型结果保存在 Git 提交 `7b4ad4889` 和 `output/n4c-g3-20260908`，包括
F1 最大强度 0.2 下 calibration 76/77/81、validation 80/81/84 个 direct victim。
这些数值、旧 NOTICE 记录和旧图不作为本次修订的通过证据；本轮重新跑网络/故障实验。
不改变的原生轨道切片可以复用。F2 模型没有改动，但联合忙闲/停机改变会影响抽样资格。
校准只能证明此加速场景的模型行为，不代表真实航天器故障率；每组仅三轮。
## 新输入 none 与 beta pilot

输出根目录 `output/n4c-g3-revision-20260909`。none 和 beta=4 的启动提交均为
`2b4d93c0e`，工作区干净。none 800/800 完成、1600/1600 传输完成、零丢包/超时，
末端账本归零。普通 task 280 使用 node 9 源端：1.757165882 s 到达，
1.812829526 s 完成 INPUT，比 F3 2.130334420 s 提前 0.317504894 s 释放依赖。
none 最忙节点 busy=393.58089 s；墙钟 491.33 s。

beta=4 / run 11 / F3 off：56 次 F1、2 次 F2；unique direct=54/0，746 完成/54 失败。
F1 START 温度 min/median/max=20.7903/24.9334/27.6077°C；pF1=0.6937/11.5629/37.2586%。
恢复 1.1662–3.2639 s，中位2.4410 s；同节点相邻 START 最短8 s、中位20.5 s。
1728 条概率全部匹配，116 个停机期状态点符合线性降温，网络零丢包，墙钟467.25 s。
beta=3 按约定使用相同输入和 run 11，结果待执行。
