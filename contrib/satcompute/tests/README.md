# SatCompute 测试

本目录只维护 SatCompute 自有测试。配置、脚本和 GitHub Actions 都不会启用或运行
ns-3 上游 examples、全局 tests 或根目录 `test.py`。

## 目录

```text
tests/
├── unit/                    Python 与 C++ 聚焦测试
├── integration/
│   ├── smoke/               单能力快速闭环
│   └── regression/          五种路由与完整任务回归
├── fixtures/
│   ├── constellation/       4/16 星轻量星座
│   ├── topology/            最小节点切片
│   └── task/                合法与非法算力/任务输入
└── support/                 C++/Python 测试公共构造
```

日常单元、smoke 和回归输出写入临时目录并在退出时清理。手动正式场景的原始指标
保存在 gitignore 排除的本地 `output/`，完整800任务运行不接入 `run-all.sh` 或 GitHub CI。

## Unit

`unit/run-cpp-tests.sh` 按固定顺序运行以下普通 executable：

| 文件 | 主要覆盖 |
|---|---|
| `para-test.cc` | `para.cc` 默认值、分组和关键压力测试默认项 |
| `protection-contract-test.cc` | N5A-G1 独立架构、存储守恒、状态大小、L1/RemoteCommit 时序、attempt 隔离与恢复选择；不发真实备份流 |
| `protection-path-test.cc` | N5A-G2 真实 UDP 动态注册/乱序接收、ID、存储不足、非零初始化、取消与同纳秒计算结束 |
| `recovery-runtime-test.cc` | G3 受控 FaultController→备份/网络/计算/任务闭环，LocalDelivery、服务锁、F1/F2 免疫、F3、deadline、同纳秒实体快照和旧回调 |
| `link-window-test.cc` | 10 Gbps、空闲、双向独立、跨窗/尾窗、可用性、队列与预留时间积分 |
| `constellation-definition-test.cc` | 原生 shell CSV、字段约束和稳定卫星数量 |
| `routing-policy-factory-test.cc` | 五种路由名到 next-hop/path policy 的映射 |
| `task-input-test.cc` | ComputeProfile/TaskTrace closed-world 校验、canonical 排序和派生传输 ID |
| `compute-service-test.cc` | 整数服务时间、非抢占 FCFS、同刻 task ID tie-break 和因果运行任务快照 |
| `fault-lifecycle-test.cc` | FAILED/CANCELLED 幂等终止、迟到包隔离和 reservation 归零 |
| `fault-trace-test.cc` | 生成 trace 的字段、算术/区间校验和 canonical writer |
| `fault-risk-query-test.cc` | 1 秒节点风险、空闲/故障状态、动态恢复、双来源时长、F3 抢占、只读性、时间边界及 query/audit 不改变 RNG 与事件 |
| `fault-model-test.cc` | 内置参数、F1/F2 状态、独立抽样、`q_comp`，共享模型状态滚动预测、原生未来位置、无状态/RNG 副作用，以及 F3 fixed-K/Poisson 数量、范围、无放回和确定性 |
| `compute-fault-execution-test.cc` | 计算故障批处理、任务各阶段、恢复、通信不变、START 当刻抽样前预测和 生成事件与测试注入的一致性 |
| `satellite-fault-execution-test.cc` | 整星端点语义、即时重路由、capacity 重准入和按实时距离恢复 |
| `online-orbit-foundation-test.cc` | 原生 mobility、连续坐标、固定 plus-grid 候选和 canonical 顺序 |
| `online-topology-controller-test.cc` | 距离门控、fixed/distance 时延、周期更新和按边集合重算路由 |

长期 Python 测试收敛为：

| 文件 | 覆盖 |
|---|---|
| `test_task_workload_model.py` | 精确S/W/K/RESULT、rho/sigma/H、LLM、合法边界及5/10/20%守恒 |
| `test_final_scenario.py` | 冻结输入、两类种子、固定锚点、缺失切片/非法CLI、正式runner；可选原生切片逐字节复现 |
| `test_compfrr_shadow.py` | 全800任务跨语言布局、虚拟账本、资源分账、证据差异与离线统计 |
| `test_fault_probability_comparison.py` | 概率对概率一致性及缺失记录拒绝 |
| `test_fault_workload_fixtures.py` | 保留F1/F2/N4B固定fixture的角色与分布，不再依赖旧生成profile |
| `test_link_metrics_report.py` | 通用链路统计分位数 |
| `test_protection_contract.py` | 保护入口拒绝非法值与 shadow 混跑、generate 通过模式校验，以及生产模块无 shadow 依赖 |

C++另保留 `task-deadline-test.cc`（当前正式输入和deadline边界）以及
`compfrr-shadow-model-test.cc`（成本分档、严格START、初始化不双计、频率枚举、
OFF/ON追赶与状态边界）。通用task/routing/fault/network测试未删除。

测试内部的 `support/fault-injection.h` 只安排直接 ns 事件，保留同刻排序、任务中断和
拓扑恢复边界覆盖；不读取生产故障文件，也不提供用户 replay 模式。

## Smoke

N5A 定向验证（需要先构建；使用项目 uv 环境）：

```bash
./ns3 run --no-build "satcompute-protection-contract-test"
./ns3 run --no-build "satcompute-protection-contract-test --traceInput=contrib/satcompute/input/experiments/leo-66/workload/task-trace.json"
./ns3 run --no-build "satcompute-protection-path-test"
./ns3 run --no-build "satcompute-recovery-runtime-test --outputDir=output/n5a-g3/controlled"
.venv/bin/python contrib/satcompute/tests/integration/smoke/run-protection-smoke.py --output-root output/n5a-g2/smoke
.venv/bin/python contrib/satcompute/tests/integration/smoke/run-recovery-smoke.py --output-root output/n5a-g3/smoke
```

第二条只核对800份任务的字节/WU/合法边界，与 G4 oracle 单向比较，不运行800任务网络仿真。
可加 `--sizingOutput=<新输出文件>` 保存四类代表性状态表；普通平台运行不输出这些验证数据。
G2 smoke 为 16 星/4 类任务/15 s 的 off、fixed、重复、容量不足四次运行，输出可留在上述目录；
不提供 `--output-root` 时自动使用临时目录。核对精确字节、cL/cR、l/r/x、存储、普通计算时间
及普通业务统计隔离；G3 新增受控恢复与平台入口 F3 对照，不是正式 800 任务/1300 s 实验。
详见 [protection](../protection/README.md)。

| 脚本 | 主要覆盖 |
|---|---|
| `run-routing-smoke.sh` | 在线 IPv4、更新次数以及未变边集合不重复重算 |
| `run-capacity-aware-smoke.sh` | 完整路径准入、pacing 和 reservation 释放 |
| `run-task-smoke.sh` | 输入传输、FCFS 计算、结果传输的单任务闭环 |
| `run-diagnostics-smoke.sh` | strict 部分完成、队列丢包和失败诊断文件 |
| `run-topology-smoke.sh` | topology-only 切片、终点采样、XYZ 演化和逐字节确定性 |
| `run-compfrr-shadow-smoke.py` | 8任务off/on、重复、audit独立性、字节/队列账本及同纳秒F3/初始化顺序 |
| `run-protection-smoke.py` | G2 四类真实固定备份流、receiver/commit/存储守恒、off 计算对照和确定性 |
| `run-recovery-smoke.py` | G3 16 星/4 任务/15 s 受控 F3，off/fixed/重复运行，同星 RESULT 的实际字节与零网络流 |
| `run-link-metrics-smoke.py` | 指标开关不改变业务、空闲/丢包/故障、窗口汇总和陈旧文件清理 |

## Regression

- `run-full-routing-regression.sh`：运行五种 IPv4 模式、distance 时延、重复
  size-aware 仿真和 66 星在线拓扑；
- `run-full-workload-regression.sh`：运行任务确定性、无任务模式、strict/report、
  失败诊断，并执行正式的 100 秒/66 星/20 任务示例；
- `run-fault-lifecycle-regression.sh`：F1 热/概率曲线、动态恢复、F2 原生空间风险与
  固定 8 秒恢复、小任务闭环、F3 永久断链；检查 RUNNING 中断、排队保留、通信不变、
  同 seed 重复一致。审计涵盖全部 RUNNING 任务的逐秒概率与剩余窗口，容差 1e-12，
  不依赖 NOTICE 或固定行数，并核对默认关闭/陈旧审计文件清理。
- `run-n4b-joint-acceptance.sh`：复用 66 星/1000 s/100 任务输入，检查四轮
  正常/审计/重复运行业务一致、联合来源、唯一 F3 重路由、零丢包、账本归零、
  所有任务/传输终态与实际 START 合同。旧 93/100、82 行属于旧 F1/NOTICE 结果，
  不再作为新模型固定 golden。

## 本地运行

首次运行先启用项目 uv 环境（`source .venv/bin/activate`），在仓库根目录配置并构建：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
```

随后依次执行完整本地门禁：

```bash
PYTHONDONTWRITEBYTECODE=1 .venv/bin/python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

定位失败时可直接运行对应具名 shell 脚本或 C++ executable。各 runner 默认使用
`./ns3 run --no-build`，因此修改 C++ 后必须先重新执行 `./ns3 build`。

## 最终生成器的原生切片复现

不启动网络仿真，复用已经导出的0..1050 s、间隔1 s原生节点切片：

```bash
SATCOMPUTE_POSITION_SLICES=output/n4c-g3-truncnormal-v3-20260909/orbit/topology \
.venv/bin/python -m unittest discover -s contrib/satcompute/tests/unit -p 'test_final_scenario.py' -v
```

该检查两次生成TaskTrace/摘要，比较双次字节一致及正式TaskTrace逐字节一致；
也核对全部原生放置记录。未提供该环境变量时仅跳过原生切片复现项，并明确显示skip；
其他固定输入、模型、合成位置和CLI测试仍执行。生成器不会读取正式TaskTrace作为生成源。
冻结验收必须提供已有切片并实际通过，不能用skip宣称复现完成。

## 手动正式运行与 G4 验证

`integration/regression/run-final-scenario.py`仅支持当前场景的none、generate、
generate+shadow。默认为generate；概率CSV审计和shadow均默认关闭。
输出必须是新目录，默认1300 s、800任务、66星、10 Gbps、1 ms、seed1/run11。

```bash
# 手动完整运行；不是日常短测试
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --output-dir=output/final-generate
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --fault-mode=none --output-dir=output/final-none
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --audit --shadow --output-dir=output/final-shadow

# 仅分析现有输出，不运行仿真
.venv/bin/python contrib/satcompute/tools/validation/compfrr-shadow/summarize.py \
  --run-dir=output/final-shadow --reference-dir=output/final-generate
```

比较双方必须采用相同概率审计开关（上例若用于逐文件比较，generate也加`--audit`）。
可选文件缺失不能静默忽略。none账本可用`tools/validation/summarize-n4c-baseline.py`分析。
通用CLI仍支持自定义实验参数，但正式runner不保留历史候选/调参兼容入口。

G4清理冻结只允许构建、unit、小型shadow/拓扑smoke与现有CSV离线分析；
**没有重新运行上述完整场景，也没有触发阶段CI。**
[最终验收](../../../docs/n4c/reviews/G4-final-freeze.md)与[解析结果](../../../docs/n4c/reviews/G4-shadow-decision-evaluation.md)
不代表真实备份、带宽/存储占用或故障任务被救回。
