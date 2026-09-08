# N4C G3：临时计算停机、热点与联合故障标定

状态：进行中，尚未达到 G3 gate。依据工作区 v3 任务书及用户确认的三项账本细节。
仅本文件维护阶段证据，详细运行输出存放忽略目录，不新增重复任务书。

## 基线与执行顺序

- G2 批准提交 `9da94f067`，旧正式 C800 的 34 个失败均在到达时发生，RUNNING 中断为 0。
- [G1 PR #88](https://github.com/forest-rabbit/SCP-SatComPlate/pull/88) 普通合并为 `e49ba2039`。
- [G2 PR #89](https://github.com/forest-rabbit/SCP-SatComPlate/pull/89) 普通合并为 `8d750724a`。
- 阶段分支 `n4c` 包含两个批准阶段全部 ancestry，main 未变化，未触发 CI。
- G3 分支 `feature/n4c-g3-hotspot-fault-calibration` 从 `8d750724a` 建立。
- 仓库 AGENTS 要求功能分支提交到达 main 后才清理，因此 G1/G2 分支暂保留到 G4。

依次实现并本地验证：临时停机生命周期、影响账本、原生位置热点与 none 基线、
F1/F2 预标定、受控 F3 联合运行、独立验证及审阅证据。完成后推送 G3 并暂停，
不合回 n4c/main，不创建 tag，不进入 checkpoint/备份/恢复/N5。

## 冻结边界

C800 每任务类别/INPUT/RESULT/WU 不变；全 66 星 100,000 WU/s，1000 s，
1..600 s 到达主窗口，10 Gbps，compute deadline factor=1.3。
F1/F2 目标约 79 个不同 RUNNING victim，F3 联合场景恰好 1 个 RUNNING victim；
不以最终失败数替代直接影响数，不设事件配额，不筛选 seed。
停机期间保留队列和通信，但不采样新的重叠 F1/F2；恢复不复活已 FAILED 的任务。

## 生命周期与影响账本

`100615eeb` 完成队列保留；随后增加按事件/任务/影响类别去重的账本，保留同刻 F1+F2
来源，START 与 impact 两种时间，取消前真实 WU 进度及 deadline，最终关联任务结果。
状态未在 START 采集时明确 NOT_ARRIVED/NOT_CAPTURED，不伪造历史状态。
专项验证同刻恢复/再次停机、队列保留、新到达、INPUT 继续、FCFS、deadline 尚未建立、
重复故障影响记录、F3 原有永久规则；34 Python、15 C++ 和全部四组 regression 通过，
旧 100 任务联合用例仍为 93 完成/7 失败、82 条概率匹配。
日志 `output/n4c-g3-20260908/lifecycle-regression.log`；未重写旧 G2/N4B 输出。

## 预声明实验与首轮候选

在运行标定之前声明：ns-3 seed=1，calibration runs=11/12/13，validation runs=21/22/23；
正式输入 placement seed=`n4c-g3-hotspot`。先以 run 11 做候选 pilot，不因结果更换 seed。
参数冻结后使用未参与调参的 validation 三轮；如未达标如实报告，不丢弃不利 run。
首轮权重 4:1、区域不限候选：原生 1 秒切片的最大年龄 0.998906 s，
北美/欧洲/东亚/background 为 73/65/39/623 个任务，区域内总需求 416.36978 s，
背景 1423.21488 s。欧洲/东亚空候选发生 48/37 次，确定性回退到其余加权候选。
候选输出在 `output/n4c-g3-20260908/pilot-w4`；所有业务字段和 arrival 与 G2 相同。
受控 F3 候选：node 9、task 79（最早到达 LLM）、2.130334420 s；该星首次任务前无
计算负载且原生北侧轨迹不在 SAA，故障不是用专属概率屏蔽构造，仍需联合运行验证。
controlled F3 合法性、精确 ns、未知节点/越界拒绝及全部 37 Python/15 C++/6 smoke 通过。
