# SCP-SatComPlate

[![SatCompute CI](https://github.com/forest-rabbit/SCP-SatComPlate/actions/workflows/phase_gate.yml/badge.svg)](https://github.com/forest-rabbit/SCP-SatComPlate/actions/workflows/phase_gate.yml)

SCP-SatComPlate 是基于官方 ns-3.48 的纯星上动态网络与计算仿真平台。`SCP` 也向
“控制、收容、保护”轻轻致意——不过这里收容的是卫星、链路与计算任务。

平台使用 ns-3.48 原生圆轨道模型实时计算卫星 ECEF 坐标，支持确定性的动态星间
链路、五种 IPv4 路由、任务传输与星上计算，并能独立输出拓扑切片，为后续故障
建模和前端可视化提供输入。`main` 是 ns-3.48 开发主线；`legacy/ns-3.33` 只读
保留旧版 SatCompute，不与主线合并。

阶段进展、集成提交和验收证据见 [`MILESTONES.md`](MILESTONES.md)。

## 当前能力

- 采用稳定卫星 ID 和 ns-3.48 原生 LEO 位置计算；
- 同轨固定连接前后邻居，异轨在 `t=0` 选择最近的循环一对一匹配并固定对端；
- 按距离门限控制候选链路启停，支持 fixed 与 distance 两种传播时延；
- 仅在有效链路集合变化时重算 hop-based IPv4 路由；
- 支持 global-first、逐流 hash、HRW、size-aware HRW 和 capacity-aware HRW；
- 支持输入传输、非抢占 FCFS 计算和结果传输的完整任务闭环；
- 支持确定性 compute/整星故障的预警、开始、有限恢复、任务/传输终止与即时重路由；
- 支持 `none/generate`，可按实时计算负载生成 F1 温度/能源风险，也可按实时
  ECEF 位置生成 F2 空间 SEU 风险；两者都能产生可恢复 compute 故障和可确定性
  核查的事件 trace；
- 支持独立 F3 fixed-K/Poisson 永久整星故障，并在冲突时优先于可恢复 compute 故障；
- 可按需启用 F1/F2 因果概率预测与 独立模型一致性审计；正常运行默认关闭
  预测采集、审计 CSV 和对比；
- topology-only 模式可输出每个切片的卫星 `x/y/z` 与候选链路状态。

备份恢复、前后端实时状态传输、IPv6、SRv6、地面站和馈电链路尚未实现。

## 仓库结构

```text
SCP-SatComPlate/
├── contrib/satcompute/       SatCompute 平台代码、输入、工具和测试
├── third-party/              仓库级第三方依赖
├── docs/                     ns-3.48 上游资料归档
├── src/                      官方 ns-3.48 模块，不放项目代码
├── doc/                      官方 ns-3.48 文档源码
├── ns3                       ns-3 构建和运行入口
├── MILESTONES.md             已完成阶段与验收证据
├── NOTICE.md                 上游来源说明
└── .github/workflows/        阶段 CI
```

项目代码只位于 `contrib/satcompute/`，不修改上游 `src/`。JSON 解析使用
`third-party/nlohmann/json.hpp`。

## 环境

最低开发环境为支持 C++23 的 GNU C++ 11 或 Clang 17、CMake 3.25、Ninja 和
Python 3。`ccache` 可选，但建议安装以缩短重复构建时间。

如果系统已有合适的 CMake 与 Ninja，可以直接使用系统 Python。也可以用 uv 建立
隔离工具环境：

```bash
uv venv --python 3.10
source .venv/bin/activate
uv pip install "cmake==3.25.*" ninja
```

uv 只管理 Python 工具环境，不能替代 C++ 编译器和系统库。SatCompute 的生成器与
检查器使用标准库；F2 论文绘图脚本通过内嵌 PEP 723 声明 NumPy/Matplotlib，可直接
使用 `uv run <script>` 隔离执行。因此仓库仍不维护额外的 `uv.lock`，也不执行
`uv sync`；根目录 `pyproject.toml` 仍是 ns-3 上游 Python 绑定的打包配置。

## 构建与快速运行

在仓库根目录执行：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
./ns3 run "satcompute --topologyOnly=1 --simulationDuration=2"
./ns3 run "satcompute --help"
```

该配置只构建 SatCompute 及其依赖，不启用 ns-3 上游 examples、全局 tests，也不
运行 `test.py`。SatCompute 自有检查仍作为普通 executable 构建。

## 正式实验与测试示例

当前正式实验入口为 [LEO-66 Final Experiment Scene](contrib/satcompute/input/experiments/leo-66/README.md)：
66 星、800 任务、1300 s、每星 100000 WU/s、10 Gbps、1 ms；正式输入自包含，
测试数据统一放在 `contrib/satcompute/tests/fixtures/`。

仓库提供一组已经纳入回归测试的
[100 秒、66 星、20 任务示例](contrib/satcompute/tests/fixtures/task/20tasks/README.md)。
它使用独立测试星座与算力 fixture，展示完整任务仿真和同周期 topology-only 切片生成。
F1 在线故障闭环见
[120 秒、66 星热点任务示例](contrib/satcompute/tests/fixtures/fault/f1/README.md)。
F2 的纯轨道暴露标定和真实平台闭环分别见
[F2 标定证据](docs/calibration/n4b-f2/README.md)与
[1000 秒、66 星、8 任务示例](contrib/satcompute/tests/fixtures/fault/f2/README.md)。
F3 无任务永久整星闭环见
[1000 秒、66 星 fixed-K 示例](contrib/satcompute/tests/fixtures/fault/f3/README.md)。
F1/F2/F3、任务、路由和概率审计的最终联合闭环见
[1000 秒、66 星、100 任务 N4B 验收场景](contrib/satcompute/tests/fixtures/fault/joint/README.md)。

只生成 0–20 秒、每秒一个拓扑切片：

```bash
./ns3 run "satcompute \
  --simulationDuration=20 \
  --topologyOnly=1 \
  --topologySliceInterval=1 \
  --outputDir=/tmp/satcompute-topology"
```

## 文档

| 文档 | 内容 |
|---|---|
| [项目里程碑](MILESTONES.md) | 已完成阶段、集成证据、验证结论与后续边界 |
| [SatCompute 运行手册](contrib/satcompute/README.md) | 执行流程、全部参数、输入输出与运行模式 |
| [输入说明](contrib/satcompute/input/README.md) | `para.cc`、星座、算力、任务与故障输入的边界 |
| [拓扑模块](contrib/satcompute/topology/README.md) | 原生轨道、固定候选链路、在线更新与切片 |
| [路由模块](contrib/satcompute/routing/README.md) | 五种 IPv4 策略、核心公式与确定性状态 |
| [任务与传输](contrib/satcompute/task/README.md) | 任务状态机、FCFS 与结果大小 |
| [指标模块](contrib/satcompute/metrics/README.md) | 输出文件、字段职责与失败诊断 |
| [故障模块](contrib/satcompute/fault/README.md) | 统一 trace、F1/F2/F3 在线模型与 N4A 执行边界 |
| [F1 标定证据](docs/calibration/n4b-f1/README.md) | 热时间常数、30-run 概率候选与选择边界 |
| [F2 标定证据](docs/calibration/n4b-f2/README.md) | 66/351/720 星轨道暴露、冻结参数与真实平台 Monte Carlo |
| [辅助工具](contrib/satcompute/tools/README.md) | TaskTrace 生成、F1/F2 标定与失败输出检查 |
| [测试说明](contrib/satcompute/tests/README.md) | 本地测试入口、覆盖范围与阶段 CI 规则 |

许可证和上游来源见 [NOTICE](NOTICE.md)。
