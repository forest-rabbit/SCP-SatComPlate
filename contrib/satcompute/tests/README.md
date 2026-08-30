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

测试输出全部写入 `mktemp` 创建的 `/tmp` 目录，并在脚本退出时清理，不向仓库写入
运行结果。

## Unit

`unit/run-cpp-tests.sh` 按固定顺序运行以下普通 executable：

| 文件 | 主要覆盖 |
|---|---|
| `para-test.cc` | `para.cc` 默认值、分组和关键压力测试默认项 |
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

`test_workload_generators.py` 检查 stress 任务生成器的确定性、总输入字节预算、
结果大小和无版本/hash 字段合同；同时检查 F1 验证档的 66 星、20 任务，以及 F2
验证档的 66 星、8 任务、热点故障、恢复后、风险-only/截断风险和稀疏对照角色。

## Smoke

| 脚本 | 主要覆盖 |
|---|---|
| `run-routing-smoke.sh` | 在线 IPv4、更新次数以及未变边集合不重复重算 |
| `run-capacity-aware-smoke.sh` | 完整路径准入、pacing 和 reservation 释放 |
| `run-task-smoke.sh` | 输入传输、FCFS 计算、结果传输的单任务闭环 |
| `run-diagnostics-smoke.sh` | strict 部分完成、队列丢包和失败诊断文件 |
| `run-topology-smoke.sh` | topology-only 切片、终点采样、XYZ 演化和逐字节确定性 |

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
  已持续时间、右删失标签和 Brier 汇总，并检查无预警故障无正式预测、无风险输出和
  none 模式陈旧文件清理；F3 部分覆盖无任务 fixed-K 永久
  整星故障、即时重路由、F3 抢占活动 compute 区间以及 F1/F2/F3 同开。

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

## CI 规则

GitHub 的 `SatCompute CI` 是手动阶段门禁：一个大阶段的分支全部合并并清理后，
只在 `main` 上触发一次。阶段内的小提交和 PR 只运行与改动匹配的本地检查；最终
仍需通过上面的完整 SatCompute 门禁。
