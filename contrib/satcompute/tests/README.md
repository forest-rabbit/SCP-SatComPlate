# SatCompute 测试

本目录只保存 SatCompute 自己的测试资产，不改变 ns-3 上游测试目录：

- `unit/`：Python 单元测试，覆盖拓扑生成、分析、可视化和验证工具；
- `integration/smoke/`：快速仿真与工具合同；
- `integration/regression/`：路由和 workload 的完整回归；
- `fixtures/`：仅供测试使用的小型 JSON 输入；
- `support/`：稳定的仓库路径和共享 fixture 构造辅助代码。

正式示例和 workload 继续放在 `../input/`。不得把正式输入迁入
`fixtures/`，也不得把 test-only fixture 放回生产输入目录。

在仓库根目录统一发现并运行全部 Python 单元测试：

```bash
uv run --locked python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
```

integration runner 要求先配置并构建 SatCompute，执行顺序为四个 Fast runner，
然后是两个 Full runner：

```bash
./waf configure --disable-examples --disable-tests --enable-modules=satcompute
./waf build
contrib/satcompute/tests/integration/smoke/run-routing-smoke.sh
contrib/satcompute/tests/integration/smoke/run-capacity-aware-smoke.sh
contrib/satcompute/tests/integration/smoke/run-task-smoke.sh
contrib/satcompute/tests/integration/smoke/run-diagnostics-smoke.sh
contrib/satcompute/tests/integration/regression/run-full-routing-regression.sh
contrib/satcompute/tests/integration/regression/run-full-workload-regression.sh
```

工具 smoke 使用不同的依赖组，因此保留独立入口：

```bash
uv run --locked python -m \
  contrib.satcompute.tests.integration.smoke.interval_analysis
MPLBACKEND=Agg uv run --locked --group visualization python -m \
  contrib.satcompute.tests.integration.smoke.orbit_visualization \
  --work-dir /tmp/satcompute-orbit-smoke
```

CI、审查、smoke、回归和仅用于审查的压力验证全部写入 `/tmp`。需要长期保留
原始结果的正式实验必须显式选择仓库外目录；`../output/` 只是误写保护区，
不是正式实验默认目录。任何生成输出都不得提交。

迁移或整理 fixture 时必须保持文件内容的 SHA-256 多重集合不变，除非任务明确
批准修改测试合同。测试模块通过 `support/` 复用公共构造器，禁止相互导入
`test_*.py`，避免测试发现顺序影响结果。
