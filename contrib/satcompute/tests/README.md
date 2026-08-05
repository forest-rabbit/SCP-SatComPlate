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
│   ├── constellation/       4 星小型星座
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
| `compute-service-test.cc` | 整数服务时间、非抢占 FCFS 和同刻 task ID tie-break |
| `fault-lifecycle-test.cc` | FAILED/CANCELLED 幂等终止、迟到包隔离和 reservation 归零 |
| `online-orbit-foundation-test.cc` | 原生 mobility、连续坐标、固定 plus-grid 候选和 canonical 顺序 |
| `online-topology-controller-test.cc` | 距离门控、fixed/distance 时延、周期更新和按边集合重算路由 |

`test_workload_generators.py` 检查任务生成器的确定性、总输入字节预算、结果大小和
无版本/hash 字段合同。

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
  失败诊断，并执行正式的 100 秒/66 星/20 任务示例。

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
