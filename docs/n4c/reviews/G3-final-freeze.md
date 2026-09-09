# G3 正式场景冻结索引

**G3 STATUS = PASS / FROZEN**。人工批准日期：2026-09-09。
当前 v3 是后续 G4/N5 及论文实验的默认公共场景；敏感性实验单独派生，不覆盖此基线。
G4 STATUS = NOT STARTED；N5 STATUS = NOT STARTED。

- 已审阅实现：`e37c00cbb66f05fbde2ae67d58ef4207db5990b2`。
- Freeze tag：`n4c-g3-frozen`，指向其后的冻结收口提交，不指向上述实现提交。
- Freeze commit：以 `git rev-parse 'n4c-g3-frozen^{commit}'` 解析的完整提交号为准。
- 冻结分支：`feature/n4c-g3-hotspot-fault-calibration`；尚未合入main、未运行阶段CI。
- 详细验收及来源核对：[G3主报告](G3-hotspot-fault-calibration.md#g3-final-freeze)。

## 正式输入与参数

候选名保留为 `C800-TruncNormal-v3`，不因冻结而重新生成任务或改名。
以下五份文件均位于
[`leo-66-1300s-n4c-g3-truncnormal-v3`](../../../contrib/satcompute/input/examples/leo-66-1300s-n4c-g3-truncnormal-v3/)：

| 文件 | 职责 |
|---|---|
| `task-trace.json` | 正式800任务及源/计算/结果节点放置；后续实验直接读取 |
| `base-task-trace.json` | 放置前的生成中间输入，保留用于生成器复查 |
| `placement-manifest.json` | none放置、热点及时间窗口说明，不是ns-3完整配置 |
| `f3-manifest.json` | 同一放置及受控F3节点/时刻，wrapper据此设置CLI |
| `workload-summary.json` | 字节/WU/状态预算及固定长任务ID，不是运行期输入配置 |

实际运行还依赖
[计算资源文件](../../../contrib/satcompute/input/examples/leo-66-1000s-n4c/compute-profile.json)、
[星座CSV](../../../contrib/satcompute/input/topology/constellations/synthetic-66.csv)、
[平台参数](../../../contrib/satcompute/para.cc)和
[故障参数](../../../contrib/satcompute/fault/fault-para.cc)；以冻结提交中的内容和主报告CLI为准。
计算资源文件虽然位于旧目录，仍是正式基线依赖，历史清理不能误删。

- 66星均为100,000 WU/s；800任务：240 dense、240 sparse、240 compression、80 LLM。
- 705普通图像 TN(240,130;50,1000) MB，保留原10×500MB和5×1GB固定ID及原取样key。
- INPUT=193,526,895,311 B，RESULT=99,846,517,485 B，WU=351,623,833；模型映射和LLM不变。
- 到达1..1050s，仿真1300s（排他终点）；1050s后F1/F2状态、抽样和恢复继续。
- orbitStartOffset=0；hotspot=64、regional limit=1，三个地理区域及放置直接沿用manifest。
- ISL 10 Gbps、fixed单向8ms、网络更新20s、指标/故障检查1s；capacity-aware HRW、size-aware。
- MTU=64028 B、ISL queue=1500000 B、receiver buffer=131072 B、maxIslDistance=6171353 m。
- compute deadline factor=1.3，FCFS；路由seed=1，故障seed=1/run=11，F1/F2/F3启用。
- F1 beta=10、gamma=1.5，温度17/20/30/35°C，17→30°C忙碌30s、30→17°C冷却最多4s。
- F2保持原SAA参数、SEU强度0.002859196111093899/s、映射概率0.5、恢复8s。
- F3 node62，1027.055770726s永久失效；参考受害task120、207,142,024 B、完成70.0000644%。

## 固定参考结果与证据边界

本地证据根目录：`output/n4c-g3-truncnormal-v3-20260909/`，仍由gitignore排除，未随tag上传。
必须保留`none/`、`fault-11/`及审阅所引用的原始账本，清理前另行明确归档位置。

- `none/`：800任务/1600传输完成，排队mean/P95/max=7.895/33.471/48.248s，零丢包和截断。
- `fault-11/`：84 F1 START/82 direct、2 F2 START/0 direct、1 F3 START/direct；717完成、83失败。
- `fault-11/fault-trace.json`：已接受generate运行的87条故障参考记录，**不是当前可加载的生产输入**。
  G2已移除生产replay；本次不恢复reader、CLI或回放执行，也未验证一次新的replay。
- 同seed/run不保证改变备份负载后的F1轨迹相同。需要固定故障文件对照时，必须另行批准实现和实验合同。
  本轮单受害F3和故障数量仅描述无备份参考运行，不作为未来算法的强制受害数量。

provenance核对通过，含义为现有输入副本、任务账本、命令、事件和提交差异一致。
不生成SHA-256清单；没有事后补造运行时源码/二进制快照，不宣称完整运行快照的逐字节证明。
该固定压力realization不是多seed均值、真实卫星故障率或现实预测准确率证明。

## 后续边界

旧“N4C G4收口”中的场景冻结和文档收口由本次G3 Final Freeze接替；原阶段名已废止。
新的G4仅指 **CompFRR Shadow Decision Evaluation**，须人工单独批准；PR合入及阶段CI仍未执行。
历史清理另做独立提交：保留当前正式场景生成入口及共享依赖、冻结输入、必要证据和简短说明；
其他候选入口、中间审查文档及重复输出可列为清理对象。本次未删除任何历史文件。
