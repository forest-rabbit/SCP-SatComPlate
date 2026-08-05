# 星座结构输入

本目录只保存“这是什么星座”的物理结构，不保存一次仿真实验的运行参数。
平台直接采用 ns-3.48 `LeoOrbitNodeHelper` 支持的 CSV 格式，当前 SatCompute
一次只接受一个轨道壳层。默认文件为 `synthetic-66.csv`。

CSV 的一行轨道参数依次为：

1. `altitudeKm`：圆轨道高度，单位为千米；
2. `inclinationDegrees`：轨道倾角，单位为度，范围为 `[0, 180)`；
3. `numberOfPlanes`：轨道面数量；
4. `numberOfSatellitesPerPlane`：每个轨道面的卫星数量；
5. `phasingFactor`：可选的 Walker Delta 相位因子，默认值为 `0`；
6. `raanSpanDeg`：可选的 RAAN 跨度，`180` 对应 Walker Star，`360` 对应
   Walker Delta，默认值为 `360`。

文件可以包含注释和表头；平台会先校验，再把同一份文件交给 ns-3.48 原生
轨道节点 helper。稳定卫星 ID 按原生 helper 的节点创建顺序编号为
`0 ... N-1`，固定 plus-grid 候选链路按轨道面优先顺序解释这些 ID。

仿真时长、网络更新间隔、距离门限、时延方式、带宽、路由、任务、随机数和
输出目录仍由平台参数负责，不能写入星座 CSV，因此两类输入不会重复或冲突。
