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
│   ├── fault/               合法 replay 故障轨迹
│   ├── topology/            最小节点切片
│   └── task/                合法与非法算力/任务输入
└── support/                 C++/Python 测试公共构造
```

日常单元、smoke 和回归输出写入临时目录并在退出时清理。手动启动的三规模压力
测试是例外：输入和原始指标保存在 `.gitignore` 排除的本地 `output/` 目录，便于
后续备份可行性分析；大规模压力测试不接入 `run-all.sh` 或 GitHub CI。

## Unit

`unit/run-cpp-tests.sh` 按固定顺序运行以下普通 executable：

| 文件 | 主要覆盖 |
|---|---|
| `para-test.cc` | `para.cc` 默认值、分组和关键压力测试默认项 |
| `link-window-test.cc` | 10 Gbps、空闲、双向独立、跨窗/尾窗、可用性、队列与预留时间积分 |
| `constellation-definition-test.cc` | 原生 shell CSV、字段约束和稳定卫星数量 |
| `routing-policy-factory-test.cc` | 五种路由名到 next-hop/path policy 的映射 |
| `task-input-test.cc` | ComputeProfile/TaskTrace closed-world 校验、canonical 排序和派生传输 ID |
| `compute-service-test.cc` | 整数服务时间、非抢占 FCFS、同刻 task ID tie-break 和因果运行任务快照 |
| `fault-lifecycle-test.cc` | FAILED/CANCELLED 幂等终止、迟到包隔离和 reservation 归零 |
| `fault-trace-test.cc` | v1/v2 closed-world 字段、四类记录、算术/区间校验和 canonical writer |
| `fault-model-test.cc` | 内置参数、F1/F2 状态、独立抽样、`q_comp`，共享模型状态滚动预测、原生未来位置、无状态/RNG 副作用，以及 F3 fixed-K/Poisson 数量、范围、无放回和确定性 |
| `compute-fault-execution-test.cc` | 计算故障批处理、任务各阶段、恢复、通信不变、NOTICE/START 当刻模型预测和 generate/replay 预测一致性 |
| `satellite-fault-execution-test.cc` | 整星端点语义、即时重路由、capacity 重准入和按实时距离恢复 |
| `online-orbit-foundation-test.cc` | 原生 mobility、连续坐标、固定 plus-grid 候选和 canonical 顺序 |
| `online-topology-controller-test.cc` | 距离门控、fixed/distance 时延、周期更新和按边集合重算路由 |

`test_fault_probability_comparison.py` 检查概率审计工具对完全一致输入的零误差报告，
以及缺失 replay 记录时的失败结果。`test_workload_generators.py` 检查 stress 任务
生成器的确定性、总输入字节预算、
结果大小和无版本/hash 字段合同；同时检查 F1 验证档的 66 星、20 任务，以及 F2
验证档的 66 星、8 任务、热点故障、恢复后、风险-only/截断风险和稀疏对照角色；
N4B 联合档还检查 100 任务、5 个有界热点、F2/F3 窗口任务、62 个分布式对照和
提交 fixture 的逐字节确定性。

## Smoke

| 脚本 | 主要覆盖 |
|---|---|
| `run-routing-smoke.sh` | 在线 IPv4、更新次数以及未变边集合不重复重算 |
| `run-capacity-aware-smoke.sh` | 完整路径准入、pacing 和 reservation 释放 |
| `run-task-smoke.sh` | 输入传输、FCFS 计算、结果传输的单任务闭环 |
| `run-diagnostics-smoke.sh` | strict 部分完成、队列丢包和失败诊断文件 |
| `run-topology-smoke.sh` | topology-only 切片、终点采样、XYZ 演化和逐字节确定性 |
| `run-link-metrics-smoke.py` | 指标开关不改变业务、空闲/丢包/故障、窗口汇总和陈旧文件清理 |

## Regression

- `run-full-routing-regression.sh`：运行五种 IPv4 模式、distance 时延、重复
  size-aware 仿真和 66 星在线拓扑；
- `run-full-workload-regression.sh`：运行任务确定性、无任务模式、strict/report、
  失败诊断，并执行正式的 100 秒/66 星/20 任务示例；
- `run-fault-lifecycle-regression.sh`：覆盖 N4A compute/整星 replay、N4B F1 热校准，
  以及 F2 轨道偏移、真实 ECEF 暴露、100-run 概率标定、风险-only、有/无预警实际
  故障和 66 星小任务闭环；同时检查同 seed trace 一致、generate/replay 逐文件等价、
  compute 故障不改变路由、故障中任务失败和恢复后新任务完成；预测部分检查滚动的
  F1/F2/`q_comp`、动态 `P_fail_before_finish`、任务剩余时间、NOTICE 当刻输出、风险
  已持续时间，以及 F1-only、F2-only、F1+F2 的 generate 抽样前模型真值与 replay
  预测逐时刻概率审计；这些场景显式开启 `faultProbabilityAudit`，并另行检查正常
  generate/replay 默认不生成审计文件、复用目录时清理陈旧文件、无预警故障无正式
  预测和无风险输出；F3 部分覆盖无任务 fixed-K 永久
  整星故障、即时重路由、F3 抢占活动 compute 区间以及 F1/F2/F3 同开。
- `run-n4b-joint-acceptance.sh`：冻结 66 星、1000 秒、100 任务的四轮联合验收；
  比较正常/审计 generate 的 trace 与正式输出、审计 generate/replay 的事件和概率、
  正常 generate/replay 的正式输出，并在复用 replay 目录后检查陈旧审计文件清理；
  同时固定 93/100 任务终态、82 条概率记录、F1/F2/F3 事件、唯一 F3 重路由、零丢包
  和账本归零。

## 本地运行

首次运行先在仓库根目录配置并构建：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
```

随后依次执行完整本地门禁：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

定位失败时可直接运行对应具名 shell 脚本或 C++ executable。各 runner 默认使用
`./ns3 run --no-build`，因此修改 C++ 后必须先重新执行 `./ns3 build`。

## 手动压力测试：10 Gbps

已完成的三规模结果见[压力基线记录](../../../docs/pressure-10g-baseline.md)。

本轮是无故障、无备份的资源基线。保留旧 75% 档的 **1500 任务、81,750,000,000
字节 INPUT、15 个 1 GB 和 30 个 500 MB 大任务**，使用当前 FNV 任务生成器及
原生轨道；不是旧 SHA-256 工作负载或旧 2 Gbps 仿真的逐项复现。75% 是任务档位，
不代表网络利用率。任务类别暂不改为后续的三种图像加 LLM。

| 星座 | 计算节点 | 仿真时长 | 到达窗口 |
|---|---:|---:|---:|
| 66 | 66 | 1000 s | 1--600 s |
| 351 | 117 | 600 s | 1--340 s |
| 720 | 240 | 300 s | 1--165 s |

运行器显式冻结：10 Gbps、8 ms 单向时延、20 s 拓扑更新、1 s 链路统计、MTU
65,535 字节、每方向队列 64,000,000 字节、socket 缓冲 131,072 字节；路由为
`global-capacity-aware-hrw`，分包为 `size-aware`，ns-3 随机 seed/run 和路由 seed 均为 1，
任务生成器 seed 固定为 `20260726`。
压力测试队列沿用历史 64 MB 档，**不改变 `para.cc` 的正常队列默认值**。

在项目 uv 环境中完成构建后，选择一个不存在的输出目录：

```bash
source .venv/bin/activate
python3 contrib/satcompute/tools/generation/prepare-pressure-baseline.py \
  --output-root=output/pressure-10g-new

python3 contrib/satcompute/tests/integration/regression/run-pressure-baseline.py \
  --input-root=output/pressure-10g-new --size=66 --stage=smoke
python3 contrib/satcompute/tests/integration/regression/run-pressure-baseline.py \
  --input-root=output/pressure-10g-new --size=66 --stage=full
python3 contrib/satcompute/tools/validation/summarize-pressure-baseline.py \
  --run-dir=output/pressure-10g-new/66/full
```

66 星通过后，依次把 `--size` 改为 351、720，每个规模均先 smoke 再 full 和汇总。
smoke 使用正式输入最早到达的 20 个任务、30 s 仿真，并对照指标关闭的运行；full
要求已有 smoke 验证通过。重复实验应使用新目录或 `--label` 指定新的结果子目录，
运行器拒绝覆盖既有输入和结果。

输出保留 `preflight.json`、`execution.json`（代码提交及命令）、`execution-result.json`、
`run.log`、`time.txt` 及正式指标；汇总器核对窗口/逐链路/全网总量、序列化完整性、
带宽和队列上限，产生 `pressure-summary.json`。活跃期结果使用覆盖任务活动的完整
统计窗口，避免将整段仿真末尾空闲纳入活跃期平均；不能当作实际加备份后的保证。

## CI 规则

N4C G1 的 `test_n4c_workload_model.py` 与 `test_n4c_workload_preview.py` 也由现有
Python unittest 发现入口运行，覆盖新WU尺度、字节/sigma分账、合法进度、短任务分档和
V2-1500/C1000/C800/C600四组离线预算。每项只做5/10/20%状态守恒检查，
不枚举备份频率或生成L1/batch/tail。每候选两次CLI业务输出逐字节一致，
并验证普通图像/大图像分账、类别数量、81.75 GB守恒和v2历史总量；输出清单严格排除旧网格。
它们不下载数据/模型，
不运行网络或随机故障标定。手动预览命令见
[N4C 工作量模型](../../../docs/n4c/workload-mapping.md)。

G2增加 `test_n4c_formal_workload.py`，逐任务核对正式C800与G1预算、节点分配和双次生成。
`satcompute-task-deadline-test` 纳入现有C++入口，覆盖正式解析、legacy类型、deadline取整/溢出、
同ns完成优先、超时释放FCFS占用、RESULT晚于deadline送达及仿真截断。

GitHub 的 `SatCompute CI` 是手动阶段门禁：一个大阶段的 PR 全部合并到 `main` 后，
只触发一次，通过并确认提交已合并后清理功能分支。阶段内的小提交和 PR 只运行
与改动匹配的本地检查；最终
仍需通过上面的完整 SatCompute 门禁。
