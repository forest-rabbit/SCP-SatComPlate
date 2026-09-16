# N5A-G1：架构与存储合同审阅

2026-09-10。**G1 实现与本地验证完成，等待用户审阅；STOPPED AT N5A-G1。**
本报告不是实际备份性能或任务救回结果；没有进入 G2 真实网络数据流。

## 1. Branch / HEAD

工作分支 `feature/n5a-protection-runtime`；审阅 HEAD 以包含本报告的提交为准，
可通过 `git rev-parse HEAD` 获取。G1 推送供审阅，不合并 N5A 分支，不进入下一门禁。

## 2. Base n5 SHA

`2a64595c7307fbac1cc647e55e1ec24d19985ea0`，即
[Pre-N5 PR #94](https://github.com/forest-rabbit/SCP-SatComPlate/pull/94) 的普通 merge。
`n5` 从 `main@n4-complete` 的 `009788ca9c5042160e50a014c6d657e785f225f3` 建立。
Pre-N5 head `7211990d56e1926aa27478e5c59ca3466e6c457a` 已验证可达，随后清理本地/远端 feature。
main、N4 tag 与 legacy 分支不变；未触发 GitHub CI。

## 3–4. Current scope / Changed files

- `protection/`：6组头/源文件，分别提供状态适配、attempt/动作、存储池、runtime接口、固定策略及checkpoint进度合同；另有中文README。
- `para.h/.cc`、`satcompute.cc`：四个保护参数，G1 拒绝尚未实现的 fixed 模式。
- CMake、C++ runner、2份新测试：普通项目测试目标，不启用上游 tests/examples。
- AGENTS：用户批准的 N5 集成分支例外；MILESTONES 仅补充 Pre-N5 已集成/N5A 开发中状态。
- 平台/测试 README 与本报告：入口、边界和审阅证据，不复制多份设计书。
- 仓库外 `project/N5A_Protection_Runtime_Implementation_Task_for_Codex.md` 与
  `N5A_Semantics_Clarification_Supplement_for_Codex.md` 已按用户最终确认修订。
  两份原始任务书不在平台 Git 跟踪范围；仓库内可追溯合同集中在
  [protection README](../../../contrib/satcompute/protection/README.md)。

## 5. Architecture / state changes

Runtime 只分发有限事件和动作，Policy 负责选动作，Mechanism 负责执行；不让 runtime 写死所有保护都是checkpoint。
固定策略首次主计算调度时 START，local/remote 使用健康、空闲且可达候选中的稳定 ID 规则，三节点不同。
候选存在不代表容量预留。已有 mechanism 可接受故障恢复，无人接受才交 policy 的 RECOMPUTE fallback。
G1 使用测试 mechanism 验证分发，不伪造真实 executor。

logical task 与 execution attempt 分开，回调身份为 `(task_id,generation)`；PRIMARY=0、RECOVERY=1。
独立 ExecutionAttempt 守卫已验证旧回调隔离、原deadline、唯一结果与免疫；尚未替换 N4 TaskRuntime。
G3 将接入真正 TaskCoordinator/ComputeService，并在主故障后先判断接管再判最终失败。
保护状态 OFF/INITIALIZING/ON/RECOVERING/DONE 与 attempt 阶段及存储对象存在性分开。

## 6–8. Parameters / Storage / Transfer

默认 `protectionMode=off`、每星10,000,000,000 B、delta=0.05、n=4。仅实验默认，不是优化结果。
池构造器必须显式接收容量；支持零容量fixture，不把普通INPUT/RESULT/队列/权重计入备份池。
used/reserved、峰值、失败计数、对象角色、同任务原地融合和幂等清理均已实现。
local只保留未覆盖尾部，remote峰值为旧状态+batch，commit后仅一个新状态；G1分别测试池与进度操作。
真实跨组件生命周期、存储reservation与发包的原子编排在G2接入。

非LLM整数公式为 `S-floor(S*w/W)+floor(Kvar*w/W)`，LLM为完整token数×114688 B。
每record的H计入L1/batch，不累积进committed state。四类10/50/100%精确字节表见模块README及输出JSON。
全部800正式任务的变量状态、合法边界、目标映射与旧G4布局核对通过；新storage公式另有手算锚点和守恒检查。

cL保留异步生成延迟和成本，不暂停主计算；capture不可变，local收齐连续增量才推进l。
remote收齐后等cR，再RemoteCommit、推进r、清理local；不新增网络ACK。
G1只实现纯时刻/进度合同，没有网络传输、动态transfer ID分配、CPU接管或额外业务CSV。
G2/G3按INIT_BASE/INIT_STATE/L1/REMOTE_BATCH/RECOVERY_TAIL/RECOVERY_INPUT分账并复用既有engine。

## 9–10. Fault / recovery / Metrics

恢复选择只消费故障当刻估计；tail严格更小时选择tail，相等选redo。没有remote base时选择recompute，
不会因为有local增量而绕过初始化。G1不查询未来网络，也不真实执行两条路径。
已接受的recovery attempt仅忽略后续F1/F2；F3终止它，不做二次恢复。完成计算后免疫结束，RESULT服从原规则。
G1不修改故障模型/事件/RNG，不修改普通任务FCFS。实际节点接管锁定及停机期间普通队列隔离属于G3。
新增的只有内存账本查询与可选测试sizing JSON；未添加生产metrics文件，不改变既有CSV列或业务传输ID。

## 11–12. Tests run / Results

- 项目uv环境CMake/Ninja构建SatCompute及其自有测试通过，Examples/Tests均OFF。
- 全部17个C++单元测试程序通过，含新 `satcompute-protection-contract-test`。
- 全部58项Python测试通过，无skip；原生切片双次生成与正式输入逐字节复现实际执行。
- 全部7组小型smoke通过；其中8任务G4验证保持6 START、2个ON victim、重复确定性与审计开关独立。
- 新纯逻辑测试覆盖容量满/不足、两任务共池、reserve/commit/release、零对象、整数边界、H/变量字节守恒、
  L1乱序、cL前拒收、cR前拒commit、同ns保守快照、INITIALIZING取消、stale attempt、deadline与F1/F2/F3差异。
- 读取800任务执行全部布局核对，不启动仿真；源代码比对确认 task/traffic/fault/input/旧shadow/src 相对n5均未修改。
- `git diff --check`通过。编译出现项目既有类型风格的 `__int128` pedantic提示，无编译错误。

复现：

```bash
source .venv/bin/activate
./ns3 build -j 4
contrib/satcompute/tests/unit/run-cpp-tests.sh
SATCOMPUTE_POSITION_SLICES=output/n4c-g3-truncnormal-v3-20260909/orbit/topology \
  PYTHONDONTWRITEBYTECODE=1 python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/integration/smoke/run-all.sh
./ns3 run --no-build "satcompute-protection-contract-test --traceInput=contrib/satcompute/input/experiments/leo-66/workload/task-trace.json"
```

## 13–14. Scenario runs / Output paths

仅运行已有小型单元/smoke fixture；**无正式800任务/1300s仿真，无Pre-N5重跑，无GitHub CI**。
没有执行大型full regression矩阵，本门禁不需要靠压力实验证明接口与整数账本。
本地证据：`output/n5a-g1/` 下cpp-tests.log、python-tests.log、smoke-tests.log、committed-state-sizing.json。
输出由gitignore排除；旧N4运行证据只读，旧shadow未改写。

## 15–16. Differences from N4 / Known limitations

G1不连接production runtime，默认业务/故障行为不变；增加参数和构建产物不等于已经启用保护。
关闭保护时的兼容性证据是旧核心源文件未改与全部unit/smoke通过，不声称重新证明了1300s逐字节结果。
局部状态对象/时刻测试不能代替实际UDP、ComputeService接管或全局同ns排序验证。
同ns故障只读严格更早的committed快照；G2/G3还必须确保真正的旧对象在同刻裁决结束前可用，
不能仅凭G1历史进度就提前物理释放恢复所需数据。
真实网络准入/丢包/超时取消、动态ID与端口管理、recovery INPUT/RESULT路径、恢复实际占用和metrics均留给G2/G3。

## 17–18. Approval / Stop

请审阅独立attempt设计、固定策略/机制接口、冻结状态字节实现、池账本及保守同ns合同。
没有新增待选模型参数；G1审阅获批后才进入G2真实固定备份数据流。

**STOPPED AT N5A-G1。** N5A分支保留，不合入n5/main，不创建完成tag，不提前运行阶段CI。
