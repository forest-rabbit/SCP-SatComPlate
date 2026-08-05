# 星座结构输入

本目录只保存“这是什么星座”，不保存“这次实验怎么运行”。当前平台一次只接受
一个 ns-3.48 `LeoOrbitalShell`，允许注释、描述性表头和恰好一行有效数值。

CSV 支持 4–6 列：

| 列 | 单位/类型 | 约束 | 省略默认值 |
|---|---|---|---:|
| `altitudeKm` | km，浮点 | 有限且 `> 0` | 必填 |
| `inclinationDegrees` | 度，浮点 | `[0, 180)` | 必填 |
| `numberOfPlanes` | 正整数 | `> 0` | 必填 |
| `numberOfSatellitesPerPlane` | 正整数 | `> 0` | 必填 |
| `phasingFactor` | 整数 | `[0, numberOfPlanes-1]` | `0` |
| `raanSpanDeg` | 度，浮点 | `(0, 360]` | `360` |

总卫星数不能超过 99999。`raanSpanDeg=180` 可表示 Walker Star，`360` 可表示
Walker Delta；实际位置由 ns-3.48 原生 helper 与 mobility 计算。

默认 `synthetic-66.csv` 为：

```text
altitudeKm,inclinationDegrees,numberOfPlanes,numberOfSatellitesPerPlane,phasingFactor,raanSpanDeg
780.0,86.4,6,11,1,180
```

它产生 6 个轨道面、每面 11 星，共 66 颗卫星。稳定 ID 按 plane-major 顺序编号，
候选链路构造见 [`topology/README.md`](../../../topology/README.md)。

仿真时长、网络更新时间、拓扑切片间隔、距离门限、时延方式、链路容量、路由、
随机数和输出目录必须留在 `para.cc`/CLI，不能在 CSV 中重复。
