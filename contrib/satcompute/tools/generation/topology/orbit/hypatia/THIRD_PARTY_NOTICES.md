# 第三方代码说明

## Hypatia / satgenpy

- 上游项目：`Hypatia`
- 上游仓库：`https://github.com/snkas/hypatia.git`
- 固定 commit：`0ac531c313eba2335f6344b46347140c3a0d4230`
- 上游组件：`satgenpy`
- 许可证：MIT
- 许可证文件：`vendor/hypatia_minimal/LICENSE`
- 机器可读来源记录：`vendor/hypatia_minimal/ORIGIN.json`

SatCompute 仅迁入轨道工具需要的最小代码，并将第三方代码与项目自有代码分开：

| 上游文件 | 本地文件 | 状态 | 适配内容 |
| --- | --- | --- | --- |
| `satgenpy/satgen/tles/generate_tles_from_scratch.py` | `vendor/hypatia_minimal/tle_generator.py` | adapted | 删除未使用的手工生成路径，保留 SGP4、WGS72、固定 epoch、连续编号和 checksum，并增加 Walker Star 的 180° RAAN span |
| `satgenpy/satgen/tles/read_tles.py` | `vendor/hypatia_minimal/tle_reader.py` | adapted | 只保留 TLE 读取、连续 `node_id`、统一 epoch 与 `ephem.readtle` |
| `satgenpy/satgen/distance_tools/distance_tools.py` | `vendor/hypatia_minimal/coordinates.py` | partial | 只保留 `geodetic2cartesian`，未迁入地面站、测地距离或链路距离逻辑 |

这些文件是基于固定版本的 Hypatia/satgenpy 裁剪并适配的第三方派生代码，不是 SatCompute 原创。各派生源码保留了 ETH Zurich 的版权和 MIT 许可证声明。
