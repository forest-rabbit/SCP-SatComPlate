# Hypatia 轨道工具

## 功能边界

本目录提供 SatCompute 使用的最小、可复现轨道工具，只负责：

- 生成 TLE；
- 读取 TLE；
- 传播指定时刻的卫星位置；
- 将经纬高转换为 WGS72 三维笛卡尔坐标；
- 生成固定 plus-grid 候选图并按距离判断动态 ISL 可用性。

它不会导入完整的 `satgen` 包，不包含 Hypatia 的 ns-3 模型，也不包含地面站、路由、转发、NetworkX、geopy 或事后分析逻辑。

## 固定运行环境

仓库根目录通过 `.python-version` 固定 Python 3.10.12，通过 `pyproject.toml` 固定 uv 0.11.25，并通过 `uv.lock` 锁定 Python 依赖。初始化环境：

```bash
uv sync --locked
```

`.venv` 由 uv 管理；可以执行 `source .venv/bin/activate`，但可复现命令和 CI 统一使用 `uv run --locked`。

## 内置的 Hypatia 最小子集

SatCompute 已内置实际需要的 Hypatia/satgenpy 最小源码，运行时不再 clone 或加载完整 Hypatia checkout。实现位于：

```text
vendor/hypatia_minimal/
```

`HypatiaAdapter` 保持 `generate_tles(...)`、`read_tles(...)` 和 `satellite_position_at(...)` 三个窄接口，直接调用本地 vendor 模块。Walker Delta 使用 360° RAAN span；Walker Star 使用相同的确定性 TLE 序列化路径和 180° RAAN span。

## 验证方法

运行六星位置 smoke：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.orbit.hypatia.smoke_positions \
  --json
```

运行全部 Hypatia/vendor 单元测试：

```bash
uv run --locked python -m unittest discover \
  -s contrib/satcompute/tools/generation/topology/tests \
  -p 'test_*.py' -v
```

运行小型动态 ISL smoke：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.dynamic.generate_dynamic_isls \
  --config contrib/satcompute/tools/generation/topology/config/synthetic-66.json \
  --duration-s 120 \
  --step-s 60 \
  --output-dir /tmp/satcompute-dynamic-isls
```

smoke 会生成并读取真实 TLE，在 0 秒和 60 秒采样全部卫星位置，并输出聚合 SHA-256。轨道使用 Hypatia 兼容的近圆偏心率 `0.0000001`，因为严格为零时 PyEphem 无法传播。

## 66 星合成星座合同

`synthetic-66` 是 Iridium 规模的合成 Walker Star 星座，不是真实 Iridium 星座：

```text
pattern       : walker-star
planes        : 6
slots/plane   : 11
altitude      : 780 km
inclination   : 86.4 degrees
RAAN          : 0, 30, 60, 90, 120, 150 degrees
phase scheme  : alternating half-slot
seam          : disabled
```

`max_isl_distance_m = 6174589` 是根据 780 km 轨道高度和 80 km 最低射线路径高度得到的几何上限，不是激光终端的实测量程。

解析该合同：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.orbit.hypatia.resolve_constellation \
  --output-dir /tmp/satcompute-synthetic-66
```

## 星座解析输出

PR2 只生成以下两个文件：

```text
tles.txt
resolved-manifest.json
```

`resolved-manifest.json` 记录物理输入、WGS72 派生量、固定上游来源、本地 vendor 集成模式、工具版本以及确定性 TLE/位置哈希。

## 动态 ISL 策略

动态 ISL 策略固定为 `plus-grid-range-gated`：

```text
positions(t)
→ fixed candidate graph
→ range availability filter
→ active undirected ISLs E(t)
→ added / removed transitions
```

候选图先为每颗卫星建立同轨环形相邻边，再连接相邻轨道面的相同 slot。`seam_enabled=false` 时不连接最后轨道面和第一个轨道面；`seam_enabled=true` 时加入该组 seam 候选边。seam 排除发生在候选图阶段，不属于运行时过滤原因。

活动判定使用卫星三维笛卡尔坐标的直线距离。距离小于或等于 `max_isl_distance_m` 时为 `ACTIVE`，超过时为 `OVER_MAX_DISTANCE`。当前没有其他过滤原因。

配置加载时还会验证 80 km clearance：以 WGS72 地球半径 `6,378,135 m` 为基准，配置的最大距离不得超过使星间射线路径保持在至少 `80,000 m` 高度的保守弦长。`synthetic-66` 在 780 km 轨道上的上限为 `6,174,589.541... m`，配置取其向下取整值 `6,174,589 m`。

## 动态 ISL 输出

`generate_dynamic_isls.py` 只生成：

```text
candidate-isls.json
isl-snapshots.jsonl
manifest.json
```

`candidate-isls.json` 记录 canonical 候选边及 `intra-plane` / `inter-plane` 类型；`isl-snapshots.jsonl` 逐时刻记录活动边、过滤边和 added/removed transition；`manifest.json` 记录配置、来源、统计量和确定性 SHA-256。

输出先写入 `<output-dir>.tmp/`，内部检查通过后再原子替换正式目录。默认拒绝覆盖非空目录。1000 秒/1 秒的审计输出只保存在本地临时目录，不得提交。

## 当前阶段不包含的功能

当前动态 ISL 只提供 plus-grid 候选图、距离门控和 transition 审计，不实现 polar cutoff、动态天线指向或最近邻重选，也不导出 SatCompute `nodes_*.json` / `topology_*.json` 快照。切片间隔实验和动态任务压力测试仍不在本阶段范围内。

生成的 TLE、manifest、动态快照、smoke 输出和 `.venv/` 都不得提交。

## 第三方来源与许可证

内置代码基于 Hypatia 仓库的 `satgenpy` 组件裁剪并适配：

```text
Repository: https://github.com/snkas/hypatia.git
Commit: 0ac531c313eba2335f6344b46347140c3a0d4230
License: MIT
```

完整 MIT License 位于 `vendor/hypatia_minimal/LICENSE`；上游文件到本地文件的映射、裁剪范围和适配说明位于 `THIRD_PARTY_NOTICES.md`；机器可读来源合同位于 `vendor/hypatia_minimal/ORIGIN.json`。
