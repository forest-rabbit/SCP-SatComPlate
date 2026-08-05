# SatCompute 测试

本目录只保存 SatCompute 自己的测试资产，不启用或运行 ns-3 上游 examples、
全局 tests 或 `test.py`：

- `unit/`：Python 单元测试与项目自有 C++ 可执行测试；
- `integration/smoke/`：沿用 legacy 的路由、capacity-aware、任务和诊断入口，
  并增加 ns-3.48 在线拓扑/trace 入口；
- `integration/regression/`：沿用 legacy 的完整路由与完整 workload 两层入口；
- `fixtures/`：仅供测试使用的小型 JSON 输入与 legacy 黄金输入；
- `support/`：稳定仓库路径和共享 fixture 辅助代码。

正式示例和 workload 放在 `../input/`。不得把正式输入迁入 `fixtures/`，也不得
把 test-only fixture 放回生产输入目录。

配置与构建只启用 SatCompute 及其依赖：

```bash
./ns3 configure --enable-modules=satcompute --disable-examples --disable-tests
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

工具级集成 smoke 保留 legacy 的 Python 模块入口。interval smoke 只需要项目默认
依赖；可视化 smoke 需要 Matplotlib、NumPy 和 Pillow，不属于 GitHub 阶段门禁：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m \
  contrib.satcompute.tests.integration.smoke.interval_analysis
MPLBACKEND=Agg MPLCONFIGDIR=/tmp/satcompute-matplotlib \
PYTHONDONTWRITEBYTECODE=1 python3 -m \
  contrib.satcompute.tests.integration.smoke.orbit_visualization \
  --work-dir=/tmp/satcompute-orbit-smoke --export-gif
```

renderer/GIF 单元测试也会在缺少这些可选依赖时明确显示为 `skipped`；安装依赖后
同一条 unit discovery 命令会自动执行它们。

阶段 5 恢复的 54 个 legacy workload/topology fixture 由
`fixtures/legacy-workload-manifest.json` 固定 SHA-256；C++ parity test 直接验证旧输入
canonical 顺序、五元组、分包、FCFS、异构算力和动态拓扑行为。除明确更新合同外，
不得静默修改这些黄金文件。

旧 Hypatia/TLE 传播测试不迁入当前 Python 测试集；轨道位置、候选 ISL、导出和
online/replay 等价性由共享 ns-3.48 C++ 核心及其项目自有 C++ 可执行测试验证。
可视化测试只消费 exporter 产生的 ECEF XYZ，不重新计算轨道。

所有临时测试输出写入 `/tmp`；需要长期保留的正式实验必须显式选择仓库外目录。
