# ns-3.33 到 ns-3.48 测试审计

基线为 `legacy/ns-3.33` 的
`f4c7bff6674f9eeaae30e080d52c38c7fb601e21`。阶段 7 审计时，legacy 测试树共有
117 个文件：95 个路径在 ns-3.48 主线原位保留，22 个路径因输入边界或轨道实现
改变而由当前测试替代。主线另有 82 个 ns-3.48 专用测试资产。

原位保留并不表示照抄实现：shell runner 已从 `waf` 和旧完整 scenario 配置适配为
`./ns3`、`para.cc` 和 constellation 输入；renderer/GIF 测试改为读取真实 topology
trace XYZ。fixture 内容由 `legacy-workload-manifest.json` 固定哈希。

## 具名集成入口

legacy 的四个 smoke 和两个 full runner 已恢复原路径。统一的两个 `run-all.sh` 只
按固定顺序调用具名 runner。ns-3.48 新增 `run-topology-smoke.sh`，负责 online
export-only 与独立 topology generator 的逐字节等价。`interval_analysis.py` 和
`orbit_visualization.py` 也保留原模块入口，但分别改为消费 C++ exporter 生成的
v0.3 trace；它们不调用 Hypatia。

## 22 个替代路径

| legacy 路径 | v0.3 结论与当前证据 |
|---|---|
| `analysis/topology_interval/test_downsample_scenario.py` | 文件职责改为 trace，替代为 `test_downsample_trace.py` |
| `analysis/topology_interval/test_report.py` | 冻结的旧报告不迁移；边状态与 ECMP 数值由同目录两个当前测试覆盖 |
| `analysis/topology_interval/test_route_probe.py` | 旧独立 route-audit executable 删除；由 `routing-compatibility-test.cc` 与 factory 测试覆盖真实 C++ 候选/选路 |
| `analysis/topology_interval/test_run_interval_study.py` | 旧完整 scenario 实验编排删除；由当前 interval integration smoke 与 replay equivalence 覆盖 |
| `generation/scenario/_helpers.py` | 公共构造移到 `tests/support/fixtures.py` |
| `generation/scenario/test_check_scenario.py` | 完整 scenario 合同由 input-bundle schema 测试替代 |
| `generation/scenario/test_configuration.py` | runtime 配置移入 para；非轨道组合由 input-bundle 测试替代 |
| `generation/scenario/test_generate_scenario.py` | 不再生成完整运行 JSON；由 input-bundle 组合测试替代 |
| `generation/topology/__init__.py` | 旧 Python topology 测试包删除；当前 checker 测试位于 `unit/` |
| `generation/topology/test_check_export.py` | 替代为 `test_topology_generation_checker.py` |
| `generation/topology/test_common_output.py` | 原子输出/manifest 由 checker 测试和 C++ exporter 测试覆盖 |
| `generation/topology/test_configuration.py` | 替代为 constellation schema、C++ constellation reader 与 para 测试 |
| `generation/topology/test_dynamic_export.py` | 传播和导出统一到 C++ exporter；由 exporter 与 replay-equivalence 测试覆盖 |
| `generation/topology/test_dynamic_isls.py` | 候选 ISL 与距离门控由 online-controller C++ 测试覆盖 |
| `generation/topology/test_hypatia_smoke.py` | Hypatia 明确不迁移；由 ns-3.48 轨道基础 C++ 测试替代 |
| `generation/topology/test_mean_motion.py` | Python 轨道公式删除；由唯一 C++ 轨道核心测试覆盖 |
| `generation/topology/test_orbit_positions.py` | XYZ 由 C++ 轨道基础和 exporter 测试覆盖 |
| `generation/topology/test_resolve_constellation.py` | 替代为 C++ constellation reader 与 JSON schema 测试 |
| `generation/topology/test_satcompute_schema.py` | 拆为 constellation 与 topology-trace schema 测试 |
| `generation/topology/test_static_topology.py` | 静态模式是共享 exporter 的单切片参数，由 exporter C++ 测试覆盖 |
| `generation/topology/test_walker_tles.py` | TLE/Hypatia 生成明确删除；Walker 参数由当前原生轨道测试覆盖 |
| `visualization/orbit/test_scenario_reader.py` | 数据源改为 manifest trace，替代为 `test_trace_reader.py` |

这 22 项同时固化在 `test_legacy_test_audit.py`；测试会检查旧实现未回流且所有替代
证据仍存在。由此，legacy 测试树不存在未作结论的文件。
