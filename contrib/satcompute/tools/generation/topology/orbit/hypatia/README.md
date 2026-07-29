# Hypatia 轨道后端

本目录是 SatCompute 拓扑生成器使用的最小、可复现 Hypatia 后端。它只负责：

- 生成并读取确定性的 Walker Star/Delta TLE；
- 在指定时刻传播卫星位置；
- 将经纬高转换为 WGS72 三维笛卡尔坐标；
- 提供窄接口 `generate_tles(...)`、`read_tles(...)` 和
  `satellite_position_at(...)`。

运行时不会 clone 或加载完整 Hypatia checkout。实际使用的
Hypatia/satgenpy 最小源码位于：

```text
vendor/hypatia_minimal/
```

轨道配置采用 Hypatia 兼容的近圆偏心率 `0.0000001`，因为严格为零时
PyEphem 无法传播。Walker Star 使用 180° RAAN span，Walker Delta 使用
360° RAAN span；二者沿用同一套确定性 TLE 序列化和位置传播路径。

运行位置 smoke：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.orbit.hypatia.smoke_positions \
  --json
```

解析默认 66 星配置并生成 TLE 与来源 manifest：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.orbit.hypatia.resolve_constellation \
  --config contrib/satcompute/tools/generation/topology/config/synthetic-66.json \
  --output-dir /tmp/satcompute-resolved-66
```

本后端不包含动态 ISL 策略、路由、转发、地面站、SatCompute JSON Schema
或 C++ 加载逻辑；这些内容由上层 `generation/topology/` 流水线负责。

## 第三方来源与许可证

内置代码基于 Hypatia 仓库的 `satgenpy` 组件裁剪并适配：

```text
Repository: https://github.com/snkas/hypatia.git
Commit: 0ac531c313eba2335f6344b46347140c3a0d4230
License: MIT
```

完整许可证位于 `vendor/hypatia_minimal/LICENSE`。上游文件映射、裁剪范围
和适配说明位于 `THIRD_PARTY_NOTICES.md`，机器可读来源合同位于
`vendor/hypatia_minimal/ORIGIN.json`。
