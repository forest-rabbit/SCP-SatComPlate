# 测试专用输入

本目录保存单元、smoke、回归及共享标定使用的固定数据，不是正式论文实验输入。
正式实验唯一入口为 [LEO-66](../../input/experiments/leo-66/README.md)。

| 路径 | 用途 |
|---|---|
| `constellation/` | 4 星、16 星小型网络功能测试 |
| `topology/leo-66.csv` | 66 星测试/标定副本；与正式 shell 字节相同，但测试不依赖正式目录 |
| `topology/nodes_0s.json` | 小型静态节点 fixture |
| `task/` | 输入合法性、排序、计算与任务生命周期；compute-profile-22/66 各为 1500000 WU/s |
| `task/20tasks/` | 100 s、20 任务完整闭环 |
| `fault/f1/`、`fault/f2/`、`fault/f3/`、`fault/joint/` | 保留的 F1/F2/F3/联合功能回归输入及说明 |

历史 fixture 不随正式实验的 100000 WU/s、1 ms 或 seed/run 调整；调用测试显式指定自身参数。
固定 JSON 内容原样保留，旧任务 profile 生成入口已退出主线，不能用正式生成器重建这些 fixture。
351/720 星仅供 F2 规模标定，位于 `tools/validation/f2/fixtures/topology/`。
原生 0..1050 s 切片不提交，生成器复现测试由 `SATCOMPUTE_POSITION_SLICES` 指定已有目录。
