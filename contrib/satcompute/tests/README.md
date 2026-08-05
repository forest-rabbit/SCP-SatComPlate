# SatCompute 测试

本目录只保存 SatCompute 自己的测试资产，不启用或运行 ns-3 上游 examples、
全局 tests 或 `test.py`：

- `unit/`：Python 单元测试与项目自有 C++ 可执行测试；
- `integration/smoke/`：沿用 legacy 的路由、capacity-aware、任务和诊断入口，
  并增加 ns-3.48 原生 topology-only 入口；
- `integration/regression/`：沿用 legacy 的完整路由与完整 workload 两层入口；
- `fixtures/`：仅供测试使用的小型 JSON 输入与 legacy 黄金输入；
- `support/`：稳定仓库路径和共享 fixture 辅助代码。

正式示例和 workload 放在 `../input/`。不得把正式输入迁入 `fixtures/`，也不得
把 test-only fixture 放回生产输入目录。

配置与构建只启用 SatCompute 及其依赖：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
```

在仓库根目录运行项目测试：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

两个 `run-all.sh` 只负责按顺序编排下列具名入口，单项调试时可以直接运行：

```bash
contrib/satcompute/tests/integration/smoke/run-routing-smoke.sh
contrib/satcompute/tests/integration/smoke/run-capacity-aware-smoke.sh
contrib/satcompute/tests/integration/smoke/run-task-smoke.sh
contrib/satcompute/tests/integration/smoke/run-diagnostics-smoke.sh
contrib/satcompute/tests/integration/smoke/run-topology-smoke.sh
contrib/satcompute/tests/integration/regression/run-full-routing-regression.sh
contrib/satcompute/tests/integration/regression/run-full-workload-regression.sh
```

保留的 legacy workload/topology fixture 用于验证 canonical 顺序、五元组、分包、
FCFS、异构算力和动态拓扑行为。平台不再维护 manifest 或 SHA-256 完整性层。

旧 Hypatia/TLE 传播测试不迁入当前 Python 测试集；轨道位置、固定候选 ISL 和
topology-only 切片由共享 ns-3.48 C++ 核心、项目 C++ 检查和入口 smoke 验证。

所有临时测试输出写入 `/tmp`；需要长期保留的正式实验必须显式选择仓库外目录。
