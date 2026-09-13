# N5C recent-U：时间尺度验证

状态：用户后续接受 Rational-U 计划，停止本轮五组正式仿真。代码、小测试及两组旧方案
完整等价门禁已完成（full/noU 各 35 个 CSV/JSON 一致）；run 11–15 的 recent-U
进程在暂停后定向结束，部分输出保留，但均未完成，不作为性能证据。
同一 FULL 快照中 recent-U 约 97.44% 的候选 U 为零，选点与 noU 约 94.13% 一致，
因此不作为当前 workload 的最终候选。下面保留原计划；新执行范围见 Rational-U 报告。

Gate A 显示累计 U 与 noU 各有胜负；随后用户明确授权验证时间局部性。
这不是把 Gate A 改写为“已证明 recent-U 更好”。当前 FULL 默认及历史证据保持不变。

## 本轮范围与顺序

1. 增加独立 `recent-U` 变体及只读事件区间账本；窗口等于当前主任务的精确剩余纯计算时间。
   普通/恢复真实忙时进入分子，预留等待不进入；分母为同窗口存活观测时间，F3 后截止。
   不增加手调窗口、衰减、权重或未来信息；不改变 ON 选点、R/M、Frequency、Recovery、RNG 或路由。
2. 先小测、构建，再对旧 full/noU 运行等价门禁。基线通过后复用原五轮证据；不覆盖旧目录。
3. 新增五组 Deferred `recent-U`（seed 1，run 11–15；800 任务/1300 s），逐 run 比较
   完成数、配对 catch、实际 WU/eq-WU、流量、busy、assignment 与存储集中度。
   同时审计 recent-U 零值比例及同快照 noU 排名，检查是否退化为 noU。

继续 `feature/n5c-u-refinement` / PR #101；PR #100 保留。
本轮不进入 Eager、不更换正式场景、不跑额外 GitHub CI、不合并或切换默认方案。
执行证据写入 ignored 的 `output/n5c-recent-u/`，结果完成后回填本文。
