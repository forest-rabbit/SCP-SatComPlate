# N5A-G2：无故障固定备份数据路径审阅包

状态：**G2 实现与本地验证完成，等待用户审阅；STOPPED AT N5A-G2。**
G1 已由用户提供的 `N5A_G1_Audit_and_G2_Start_for_Codex.md` 明确批准；
本轮仅执行其 G2 部分，不实现 G3，不合并 Draft PR #95。

## 版本与范围

- 分支：`feature/n5a-protection-runtime`；同一 Draft [PR #95](https://github.com/forest-rabbit/SCP-SatComPlate/pull/95)，base=`n5`。
- 经验证的代码 HEAD：`73e30c9a48b4db8f3acce1a1ec6e21e8ab5a51d1`；本报告在其后的纯文档提交中交付。
- G1 起点：`3fe81123ab3245072289883c3373fdcdef3c8aec`；n5 基线：`2a64595c7307fbac1cc647e55e1ec24d19985ea0`。
- main 保持 `009788ca9c5042160e50a014c6d657e785f225f3`；未改上游 src、正式输入、故障模型或 CI workflow。
- 代码改动集中于新增 CheckpointManager、FixedProtectionController、稳定流键、保护 metrics、两类验证；
  既有 engine 仅扩展动态注册/终态观察/批量取消，TaskCoordinator 仅隔离保护流完成判断。
  README 只同步实际入口/模块/测试行为；未新增里程碑长篇说明。

## 数据路径合同

| 项目 | G2 实现与验收口径 |
|---|---|
| 运行期注册 | `RegisterRuntimePlan` 复用现有 sender/receiver、分包、路由、队列、capacity admission 和 finalizer；不建第二套网络 |
| ID/flow | 普通 ID 仍为 `2*T-1/2*T`；保护 ID 从全部普通 ID 的最大值+1单调分配；每源端口延续10000–65535，不复用，耗尽显式失败 |
| 同纳秒确定性 | 请求按 `(task_id,generation,kind,sequence)` 排序，下一纳秒注册；固定1ns等待不算cL/cR。kind依次为INIT_BASE/INIT_STATE/L1/REMOTE_BATCH，sequence为覆盖WU、初始化为0 |
| 应用启动 | 新动态应用先完成同纳秒 StartApplication，再允许 capacity-aware 的同步准入/发送；普通流启动顺序保持原样 |
| 初始化 | START捕获合法当前进度，base与状态生成路径并行；状态等cL。两条路径完成后等cR，再融合为单一Mstate对象 |
| 零状态 | x=0 的INIT_STATE走明确逻辑完成，仍等cL并与base汇合；不创建0字节或伪1字节UDP。LLM在x=0也能显式进入ON |
| L1 | 捕获不可变边界→cL→接收端预留→真实UDP→receiver收齐→CommitReservation→连续前缀l；sender-finished/部分接收不提供有效状态 |
| 字节 | 使用TaskStateAdapter的 `K(new)-K(old)+H`；batch严格为n个连续已收记录的实际字节和，保留各record的H |
| batch | n=4；先预留remote临时对象再注册/发送；远端收齐只把reserved转used，不推进r |
| RemoteCommit | 收齐后精确等cR，原地Merge、推进r、删除batch并清理已覆盖local；没有ACK、UDP重传或第二份解析网络时延 |
| 存储不足 | 初始化失败停止本次保护；L1失败留下不可跳过的缺口；remote batch失败保留local及r并停止后续batch尝试，不影响普通任务 |
| 清理 | 生成中/L1在途/batch在途/cR等待/计算完成/仿真结束均可取消；批量finalizer避免清理期间误准入待取消的兄弟流；按task释放全部对象 |
| 同纳秒计算结束 | Live守卫使用真实服务起点/WU/速率和当前任务状态，inclusive compute end优先，不依赖两回调的UID；终止后不能新建有效状态 |
| cL/cR | 来自唯一生产档位，不暂停主ComputeService；旧G4仅用于布局/成本/字节oracle，不用其理想网络时序替代真实传输 |
| Metrics | fixed开启4份保护CSV，off不开启；普通transfer/run-summary应用指标仅INPUT/RESULT，FlowMonitor/链路/路由容量包含所有实际保护流 |

每个存储事件包含task、节点池、对象ID及used/reserved快照；每条流携带已预留对象身份。
始终检查 `r <= l <= x`，接收字节等于声明字节才能commit，融合峰值为old state+batch，
不申请第三份完整状态。任务清理后local/remote used/reserved均为0。

## 验证与输出

本地使用项目uv环境。完整命令入口见 [tests README](../../../contrib/satcompute/tests/README.md)，
实现/参数/输出字段见 [protection README](../../../contrib/satcompute/protection/README.md)。

| 检查 | 结果 |
|---|---|
| 模块及项目测试构建 | PASS；全局ns-3 examples/tests保持OFF。保留现有整数预算的`__int128` pedantic警告，无编译错误 |
| C++ | 18个程序全部通过；G2真实数据路径1,979项检查，G1独立合同25,427项检查 |
| Python | 58项全部通过，无跳过；复用现有原生位置切片检查生成器，不重新跑正式网络场景 |
| Smoke | 8组全部通过，包含原路由、容量、任务、诊断、拓扑、链路、G4及新增G2 |
| G2 fixture | 16星、4类任务、15s、100000WU/s、10Gbit/s、1ms；primary=3、local=2、remote=0，delta=5%、n=4、每星10GB |
| 正常闭环 | 4任务完成；8条普通INPUT/RESULT，81条真实保护流全部完成，共1,276,334,717 B保护应用payload |
| 有效状态 | 64条L1收齐；13个batch完成，加4次初始化，共17次远端提交；未把生成或发送计作有效状态 |
| 存储 | local峰值115,949,568 B，remote峰值460,013,568 B；最终全部used/reserved=0 |
| 对照 | off/fixed主计算起止时间相同；普通业务字节/任务数相同，fixed链路实际传输量增加 |
| 重复性 | 两次fixed的4份保护CSV逐字节一致；反转同纳秒两任务创建回调顺序后真实流ID签名一致，另测同任务多kind排序与uint64溢出 |
| 非正常数据路径 | 控制测试覆盖L1生成中/在途、batch在途/待融合、计算结束和仿真停止，失败/取消释放资源；另测非零初始化、真实乱序/部分接收、L1缺口、local/remote容量拒绝 |

主fixture没有取消流；取消/失败不是通过改故障概率制造，而由独立受控数据路径fixture触发。
真实乱序测试让后发的小L1先收齐，确认不能越过未收到的大L1；两个同纳秒测试分别覆盖
计算完成与保护回调的先后UID顺序。批量取消测试确认等待中的兄弟流发送字节仍为0。

任务1的实际时间锚点（ns）：START=`1045112356`，零状态逻辑完成=`1045212356`，
base收齐=`1090224713`，初始化commit=`1090724713`；首个batch收齐=`1291023757`，
RemoteCommit=`1291523757`，恰好相差cR=500000ns；计算结束与保护清理均为`1831552356`。

本地证据目录为 `output/n5a-g2/`：`build.log`、`cpp-tests.log`、`python-tests.log`、
`smoke-tests.log`、`protection-smoke.log`，以及`smoke/{off,fixed,repeat,pool-full}/`。
输出/日志不提交Git；四类输入fixture和验证脚本已提交，可直接复现。

## 已知边界与下一门禁

- 当前入口明确只接受无故障fixed网络任务，shadow关闭；默认off不变。没有预测驱动频率或节点优化。
- 端口不复用、单任务只允许一批remote在途；容量失败不自动重试。1ns注册等待是显式工程时序，不是估计网络时延。
- C++控制fixture的普通输入从1ns到达，端到端fixture从1s开始。本轮未扩展既有capacity-aware普通流在0ns、Application尚未启动时同步准入的边界。
- 未运行800任务/1300s正式仿真、大规模regression或阶段GitHub CI；已有回归脚本保持启用，未削减。
- G3仍需接真正的任务恢复机会/attempt执行、INITIALIZING fallback、真实tail/redo/recompute、恢复服务准入及免疫/F3终止。
  完整故障同纳秒phase与旧存储保留也留给G3；G1历史快照和G2清理不能代替这项集成。
- 没有额外需求阻塞本次G2交付；是否通过门禁由用户审阅。本分支/PR保持Draft，main/n5不合并，分支暂不清理。

**STOPPED AT N5A-G2；等待审阅，不自动进入G3。**
