# Hypatia 轨道工具

## 功能边界

本目录提供 SatCompute 使用的最小、可复现轨道工具，只负责：

- 生成 TLE；
- 读取 TLE；
- 传播指定时刻的卫星位置；
- 将经纬高转换为 WGS72 三维笛卡尔坐标。

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
uv run --locked python \
  contrib/satcompute/tools/hypatia/smoke_positions.py --json
```

运行全部 Hypatia/vendor 单元测试：

```bash
uv run --locked python -m unittest discover \
  -s contrib/satcompute/tools/hypatia/tests \
  -p 'test_*.py' -v
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
uv run --locked python \
  contrib/satcompute/tools/hypatia/resolve_constellation.py \
  --output-dir /tmp/satcompute-synthetic-66
```

## 输出文件

PR2 只生成以下两个文件：

```text
tles.txt
resolved-manifest.json
```

`resolved-manifest.json` 记录物理输入、WGS72 派生量、固定上游来源、本地 vendor 集成模式、工具版本以及确定性 TLE/位置哈希。

## 当前阶段不包含的功能

PR2 不生成动态 ISL，不应用 seam、Earth occlusion 或 polar cutoff，不导出 SatCompute `nodes_*.json` / `topology_*.json` 快照，也不生成 1000 秒轨迹或执行切片间隔实验。

生成的 TLE、manifest、smoke 输出和 `.venv/` 都不得提交。

## 第三方来源与许可证

内置代码基于 Hypatia 仓库的 `satgenpy` 组件裁剪并适配：

```text
Repository: https://github.com/snkas/hypatia.git
Commit: 0ac531c313eba2335f6344b46347140c3a0d4230
License: MIT
```

完整 MIT License 位于 `vendor/hypatia_minimal/LICENSE`；上游文件到本地文件的映射、裁剪范围和适配说明位于 `THIRD_PARTY_NOTICES.md`；机器可读来源合同位于 `vendor/hypatia_minimal/ORIGIN.json`。
