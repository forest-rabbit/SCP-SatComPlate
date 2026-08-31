# 星座结构输入

本目录只保存“这是什么星座”，不保存“这次实验怎么运行”。当前平台一次只接受
一个 ns-3.48 `LeoOrbitalShell`。解析器只允许 UTF-8、空行、行首 `#` 注释、零或
一行精确表头，以及恰好一行六列数值；任何其他行都会带路径和行号拒绝。

CSV 固定为六列：

| 列 | 单位/类型 | 约束 |
|---|---|---|
| `altitudeKm` | km，浮点 | 有限且 `> 0` |
| `inclinationDegrees` | 度，浮点 | `[0, 180)` |
| `numberOfPlanes` | 正整数 | `> 0` |
| `numberOfSatellitesPerPlane` | 正整数 | `> 0` |
| `phasingFactor` | 整数 | `[0, numberOfPlanes-1]` |
| `raanSpanDeg` | 度，浮点 | `(0, 360]` |

总卫星数不能超过 99999。`raanSpanDeg=180` 可表示 Walker Star，`360` 可表示
Walker Delta；实际位置由 ns-3.48 原生 helper 与 mobility 计算。

`maxIslDistance` 还必须满足 80 km 最低射线高度。校验使用与 ns-3.48
`LeoCircularOrbitMobilityModel` 相同的 `6,371,000 m` 球形地球半径：

```text
R_orbit = R_earth + altitude
R_clearance = R_earth + 80000 m
maxIslDistance <= floor(2 * sqrt(R_orbit^2 - R_clearance^2))
```

因此默认 780 km shell 的上限为 `6,171,353 m`。该值与旧版 Hypatia/WGS72
半径得到的 `6,174,589 m` 不同，当前主线以 ns-3.48 的实际坐标几何为准。

当前提供三套单层 Walker Star 输入：

| 文件 | 高度 | 倾角 | 轨道面 x 每面卫星 | 总数 | 用途 |
|---|---:|---:|---:|---:|---|
| `synthetic-66.csv` | 780 km | 86.4 deg | 6 x 11 | 66 | 默认功能与回归场景 |
| `synthetic-351.csv` | 1015 km | 98.98 deg | 27 x 13 | 351 | Telesat T1 规模验证 |
| `synthetic-720.csv` | 1200 km | 87.9 deg | 18 x 40 | 720 | OneWeb 规模验证 |

默认 `synthetic-66.csv` 的内容为：

```text
altitudeKm,inclinationDegrees,numberOfPlanes,numberOfSatellitesPerPlane,phasingFactor,raanSpanDeg
780.0,86.4,6,11,1,180
```

它产生 6 个轨道面、每面 11 星，共 66 颗卫星。稳定 ID 按 plane-major 顺序编号，
候选链路构造见 [`topology/README.md`](../../../topology/README.md)。

仿真时长、网络更新时间、拓扑切片间隔、距离门限、时延方式、链路容量、路由、
随机数和输出目录必须留在 `para.cc`/CLI，不能在 CSV 中重复。
