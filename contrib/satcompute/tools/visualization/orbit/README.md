# 可选卫星状态可视化

本目录恢复 ns-3.33 的三维查看器外形，但数据源改为 ns-3.48 平台或共享生成器
导出的 v0.3 topology trace。查看器只读取 manifest 中列出的
`nodes_<time>s.json` 与 `topology_<time>s.json`，其中卫星位置直接采用 ECEF
`x_m/y_m/z_m`；Python 不传播轨道、不调用 Hypatia/TLE/SGP4，也不改变仿真、
路由或任务状态。

这也是后续前端接口的离线参考消费者：核心状态为稳定 satellite ID、仿真时间、
ECEF XYZ 和活动 ISL。网络传输接口本阶段不实现。

## 运行

先生成 topology trace：

```bash
./ns3 run "satcompute-topology-generator \
  --runName=viewer-demo \
  --simulationDuration=20 \
  --topologyExportInterval=1 \
  --outputDir=/tmp/viewer-trace"
```

复制 `config/orbit-viewer.json` 到 `/tmp`，将 `enabled` 改为 `true`，再运行：

```bash
MPLBACKEND=Agg MPLCONFIGDIR=/tmp/satcompute-matplotlib \
python3 -m contrib.satcompute.tools.visualization.orbit.viewer \
  --trace-dir=/tmp/viewer-trace \
  --config=/tmp/orbit-viewer.json \
  --headless
```

去掉 `--headless` 可打开 Matplotlib 交互窗口，支持播放、暂停、时间滑块、旋转和
缩放。可选的计算节点分类来自独立 ComputeProfile，而不是 topology trace：

```text
--compute-profile=contrib/satcompute/input/topology/resources/workload/
                  xw-66sat-static-2g-compute-profile.json
```

未提供时全部卫星按普通卫星显示。查看器运行需要 Matplotlib、NumPy；GIF 额外
需要 Pillow。仿真、拓扑生成、checker 和默认关闭路径都不导入这些可选依赖。

## 数据与时间语义

- ECEF 坐标以米存储，渲染时只换算为千米；不重新计算卫星位置。
- `render_step_s` 是画面采样周期，不是网络更新时间或拓扑导出周期。
- 当画面时间位于两个导出切片之间，位置和链路均显示上一份切片，并同时标出
  当前画面时间与实际来源切片时间。
- 轨道参考环仅用同一切片、同一轨道面卫星的真实 XYZ 拟合，属于显示几何，
  不能作为轨道或链路输入。
- `show_links=true` 只显示切片中已经判定为 active 的 ISL。
- GIF 必须输出在 topology trace 目录之外，不进入 manifest，也不得提交仓库。

旧版基于 Hypatia 场景生成的 PNG 截图不迁移，因为它们不是 ns-3.48 共享轨道
核心的证据。需要截图时应从当前 trace 重新生成。

## 配置

`config/orbit-viewer.json` 是独立的闭世界显示配置，默认 `enabled=false`：

| 字段 | 含义 |
| --- | --- |
| `display_mode` | `auto`、`detailed` 或 `simplified` |
| `render_step_s` | 交互/headless 画面采样秒数 |
| `playback_interval_ms` | 相邻画面的墙钟间隔 |
| `show_earth` | 地球、经纬网与坐标标签 |
| `show_orbits` | 显示由当前 XYZ 拟合的参考环 |
| `show_links` | 显示活动 ISL |
| `show_node_labels` | detailed 模式显示 satellite ID |
| `detail_node_threshold` | `auto` 切换到 simplified 的规模阈值 |
| `export_gif` / `gif_path` | 可选 GIF 开关与外部输出路径 |
| `gif_frame_step_s` | GIF 独立抽帧秒数 |

`auto` 在卫星数不超过阈值时使用 detailed，否则使用 simplified。配置不属于平台
输入，不参与 topology manifest 哈希。
