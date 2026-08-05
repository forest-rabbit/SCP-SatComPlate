# SatCompute 测试

本目录只保存 SatCompute 自己的测试资产，不启用或运行 ns-3 上游 examples、
全局 tests 或 `test.py`：

- `unit/`：Python 单元测试与项目自有 C++ 可执行测试；
- `integration/smoke/`：快速平台、在线拓扑与工具合同；
- `integration/regression/`：五种 IPv4 路由、workload、strict/report 和重复性回归；
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

阶段 5 恢复的 54 个 legacy workload/topology fixture 由
`fixtures/legacy-workload-manifest.json` 固定 SHA-256；C++ parity test 直接验证旧输入
canonical 顺序、五元组、分包、FCFS、异构算力和动态拓扑行为。除明确更新合同外，
不得静默修改这些黄金文件。

所有临时测试输出写入 `/tmp`；需要长期保留的正式实验必须显式选择仓库外目录。
