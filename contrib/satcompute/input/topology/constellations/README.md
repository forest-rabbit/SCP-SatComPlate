# 星座结构输入

本目录只保存“这是什么星座”的物理结构 JSON，不保存一次仿真实验的运行参数。
平台从 `para.cc` 的 `constellationConfig` 默认路径读取文件，也可以由同名 CLI
选项覆盖。

`synthetic-66.json` 使用 6 个轨道面、每面 11 颗卫星、高度 780 km、倾角
86.4 度的 Walker Star 结构。稳定卫星 ID 按 plane-major 顺序生成：

```text
satellite_id = plane_index * satellites_per_orbit + slot_index
```

字段解释：

- `schema_version`：closed-world 合同版本，当前固定为 `0.1`；
- `constellation_name`：稳定且适合文件名的星座名称；
- `constellation_pattern`：`walker-star` 或 `walker-delta`；
- `num_orbits`：正整数轨道面数量；
- `satellites_per_orbit`：每个轨道面的正整数卫星数量；
- `altitude_m`：圆轨道高度，单位为米；
- `inclination_deg`：范围为 `[0, 180)` 的轨道倾角，单位为度；
- `phase_diff`：是否让奇数轨道面偏移半个槽位；
- `orbit_epoch_offset_s`：仿真 `t=0` 时已经传播的非负轨道时间，单位为秒。

仿真时长、网络/导出间隔、距离门限、fixed/distance 时延、带宽、路由、任务、
随机数和输出目录都属于 `para.h/.cc`，不得出现在本目录 JSON 中。读取器会拒绝
任何未知字段，不会用 `para.cc` 静默覆盖冲突值。完整机器可读约束位于
`topology/orbit/constellation.schema.json`。
