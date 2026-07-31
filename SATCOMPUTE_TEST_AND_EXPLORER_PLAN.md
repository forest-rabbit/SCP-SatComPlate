# SatCompute 测试集中化与输出目录整理计划

> 状态：实施中。
>
> 本计划只重组 SatCompute 自有测试资产及其路径引用，不修改生产算法、
> 路由合同、仿真行为或 ns-3 上游测试。

## 1. 目标

将 SatCompute 自有测试统一放到 `contrib/satcompute/tests/`，并按 unit、
integration 和 fixture 分层。测试仍按功能域和运行成本独立执行，不把所有门禁
合并成一个耗时入口。

同时清空 `contrib/satcompute/output/` 中无保留价值的旧结果，并让该路径下
今后生成的文件在 `git status` 中可见。output 只用于正式实验结果，测试与审查
仍必须输出到 `/tmp`，任何 output 内容都不得提交。

## 2. 已确认基线

| 类型 | 文件数 | 测试用例数 |
|---|---:|---:|
| 拓扑生成 Python 单元测试 | 12 | 83 |
| 场景生成 Python 单元测试 | 5 | 42 |
| 拓扑间隔分析 Python 单元测试 | 6 | 24 |
| 轨道可视化 Python 单元测试 | 6 | 17 |
| **Python 单元测试合计** | **29** | **166** |
| Shell smoke/regression runner | 5 | — |
| CI 专用 Python smoke driver | 2 | — |
| 测试 JSON fixture | 49 | — |

迁移前四组 Python 测试均已通过。49 个 JSON fixture 的 SHA-256 作为内容不变
基线；迁移只允许路径变化，不允许改写 fixture 字节。

不纳入迁移：

- ns-3 上游测试、bindings、OpenFlow 或其他上游模块；
- 生产代码、生成器、checker、preflight 与普通 CLI；
- Hypatia 的 `smoke_positions.py`，它仍是实现附近的独立诊断工具；
- 正式 example 与 workload 输入；
- build、cache、packet capture、metrics 和任何生成结果。

## 3. 目标结构

```text
contrib/satcompute/tests/
├── README.md
├── support/
│   ├── __init__.py
│   ├── paths.py
│   └── fixtures.py
├── unit/
│   ├── generation/
│   │   ├── topology/
│   │   └── scenario/
│   ├── analysis/
│   │   └── topology_interval/
│   └── visualization/
│       └── orbit/
├── integration/
│   ├── smoke/
│   │   ├── interval_analysis.py
│   │   ├── orbit_visualization.py
│   │   ├── run-routing-smoke.sh
│   │   ├── run-task-smoke.sh
│   │   └── run-diagnostics-smoke.sh
│   └── regression/
│       ├── run-full-routing-regression.sh
│       └── run-full-workload-regression.sh
└── fixtures/
    ├── topology/
    │   ├── snapshots/
    │   └── compute-profiles/
    ├── traffic/
    │   ├── transfers/
    │   └── tasks/
    └── scenario-generation/
```

所有参与统一 `unittest discover` 的目录都必须是 Python package，避免多个
`test_configuration.py` 发生模块名冲突。

## 4. 路径映射

| 当前路径 | 目标路径 |
|---|---|
| `tools/generation/topology/tests/` | `tests/unit/generation/topology/` |
| `tools/generation/scenario/tests/` | `tests/unit/generation/scenario/` |
| `tools/analysis/topology_interval/tests/` | `tests/unit/analysis/topology_interval/` |
| `tools/visualization/orbit/tests/` | `tests/unit/visualization/orbit/` |
| analysis `ci_smoke.py` | `tests/integration/smoke/interval_analysis.py` |
| visualization `ci_smoke.py` | `tests/integration/smoke/orbit_visualization.py` |
| `tools/ci/run-*-smoke.sh` | `tests/integration/smoke/` |
| `tools/ci/run-full-*-regression.sh` | `tests/integration/regression/` |
| `input/topology/tests/` | `tests/fixtures/topology/snapshots/` |
| `input/topology/resources/test/` | `tests/fixtures/topology/compute-profiles/` |
| `input/traffic/test/` | `tests/fixtures/traffic/transfers/` |
| `input/traffic/task/test/` | `tests/fixtures/traffic/tasks/` |
| scenario test fixtures | `tests/fixtures/scenario-generation/` |

## 5. 实施顺序与提交合同

1. 记录测试与 fixture 基线；清理 output 并调整精确反忽略规则。
2. 建立中央目录、稳定的仓库路径入口和共享测试构造器。
3. 按 topology、scenario、analysis、visualization 四个域迁移单元测试。
4. 按 snapshots、compute profiles、transfers、tasks、scenario generation
   五类迁移 fixture。
5. 迁移两个 Python smoke driver、三个 Fast runner 和两个 Full runner。
6. 执行统一 discovery、构建、smoke、Fast 与 Full 回归。

任何路径迁移提交都必须同时更新它的直接消费者、workflow 命令、workflow
`paths` 过滤器和相关 README。不得先留下坏路径，再用后续提交修复。

公共辅助对象不能继续从其他 `test_*.py` 导入；固定的
`Path(__file__).parents[n]` 必须替换为中央路径 helper。每一批都先运行最小
相关测试，完整回归只在最终 checkpoint 运行。

## 6. 输出目录合同

- `para.cc` 的默认输出保持 `/tmp/satcompute-output`。
- 审查、CI、smoke 和回归统一写入 `/tmp`。
- 正式实验可显式指定 `--outputDir=contrib/satcompute/output/<experiment>`。
- `.gitignore` 保留通用 `output/` 规则，但精确反忽略
  `contrib/satcompute/output/`，使实验结果可见。
- 空目录不使用 `.gitkeep` 提交；首次实验时由程序创建。
- output 内容、metrics、路由 dump、packet capture 和 cache 永不进入提交。

## 7. 最终验收

- [ ] 29 个测试模块、166 个测试用例均保留并通过。
- [ ] 49 个 fixture 均迁入 `tests/fixtures/`，内容哈希不变。
- [ ] 5 个 Shell runner 和 2 个 Python smoke driver 均迁入
      `tests/integration/`。
- [ ] SatCompute 自有测试资产不再散落于 `tools/**/tests`、`tools/ci` 或
      `input/**/test*`。
- [ ] workflow、README、runner 和 checker 不再引用旧路径。
- [ ] 正式 example/workload 输入仍保留在 `contrib/satcompute/input/`。
- [ ] ns-3 上游内容和生产 C++ 代码未改动。
- [ ] 标准 SatCompute 构建和 66 星、110 秒 smoke 通过。
- [ ] Fast Smoke、Full Regression、interval smoke 和 visualization smoke
      全部通过。
- [ ] `git diff --check` 和旧路径全文审计通过。
- [ ] `contrib/satcompute/output/` 内没有提交任何生成文件。
