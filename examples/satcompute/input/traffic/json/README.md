# NetworkTransfer JSON 输入

程序只读取 `--transferTrace` 显式指定的一个 JSON 文件，不会自动加载本目录下的
其他输入。

## workload

- `workload-5000-varied.json`：5000 条不同大小的规模输入，同时作为 CI 的规模
  回归；
- `mixed-large-local.json`：10 条 128 MiB–1 GiB 的本地完整压力输入，不在每次
  CI 中运行。

## test

- `canonical-order-a.json`、`canonical-order-b.json`：验证记录顺序规范化和余数
  包；
- `diamond-4-static-transfers.json`：验证静态 ECMP 双路径与重复确定性；
- `diamond-4-dynamic-transfers.json`：验证链路变化后的 `2 → 1 → 2` 路由候选；
- `mixed-large-ci.json`：验证至少 10 条大流量以及全部 size-aware 分包档位。

进入 N1 后继续保留这些 N0 输入和
`tools/check-ecmp-output.py`，用于确认任务计算、调度等新增逻辑没有破坏 N0
网络传输基线。快速用例继续进入每次 CI；完整本地压力输入按需运行。只有当某项
N0 行为被明确废弃，并已有替代验证时，才应同时删除其输入和检查代码。
