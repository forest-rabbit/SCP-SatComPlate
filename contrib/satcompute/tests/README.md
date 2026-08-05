# SatCompute 测试

本目录只保存平台自己的轻量测试，不启用 ns-3 上游 examples、全局 tests 或
`test.py`。

- `unit/`：一个任务生成器 Python 测试和 7 个聚焦 C++ 可执行测试；
- `integration/smoke/`：路由、capacity-aware、任务、诊断和原生 topology-only；
- `integration/regression/`：五种 IPv4 路由以及完整任务/完成策略回归；
- `fixtures/`：4 星测试星座、一个节点切片和最小 ComputeProfile/TaskTrace。

配置与构建只启用 SatCompute 及其依赖：

```bash
./ns3 configure --enable-modules=satcompute -G Ninja
./ns3 build
```

在仓库根目录依次运行：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/unit/run-cpp-tests.sh
contrib/satcompute/tests/integration/smoke/run-all.sh
contrib/satcompute/tests/integration/regression/run-all.sh
```

smoke 与 regression 使用具名脚本，失败时可以直接运行单项。所有临时输出均写入
`/tmp`。路由、任务和 topology-only 已由集成层覆盖的重复 unit executable、旧
replay 拓扑、纯 transfer fixture、schema 测试及 SHA manifest 均不再保留。
