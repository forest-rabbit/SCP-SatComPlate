# N1 ECMP Git 引用清理审计

## 1. 审计边界

```text
Functional audit base: 35a6258de3ac9438551e3dba254a4129089cf5d0
PR #9 closeout main: f9732aa4d32f09174635554c3a310be31bec2271
Date: 2026-07-28
Remote: origin / forest-rabbit/SatCompute
Rule: classify first, create and verify the final tag, then delete exact refs
```

本审计只清理 Git 分支和阶段性标签，不删除任何源码、上游 ns-3 内容、PR、提交
或 GitHub Actions 记录。普通 merge/rebase 分支必须同时满足“对应 PR 已合并”
“相对 `origin/main` 的独有提交数为 0”才可进入删除集合。squash-merged 分支的
提交按定义不会成为 `main` 的祖先，必须改为验证“对应 PR 已 squash 合并”且
“分支与合并后 `main` 的 Git tree 对象完全相同”。

## 2. 远端分支

| 远端分支 | 审计时 head | PR | 独有提交 | 分类与处置 |
| --- | --- | --- | ---: | --- |
| `ci/split-satcompute-regression` | `ec9d973b514e051388349e172a6e630f510cb7a3` | [#8](https://github.com/forest-rabbit/SatCompute/pull/8) | 0 | `MERGED_REDUNDANT`，最终标签验证后删除 |
| `docs/n1-closeout` | `e1212f43fff5fdc9a6891be65a91fd3df65f5563` | [#3](https://github.com/forest-rabbit/SatCompute/pull/3) | 0 | `MERGED_REDUNDANT`，最终标签验证后删除 |
| `feature/n1-stress-closure` | `66f8d770fa24326282b3518e438355ac72e2471c` | [#2](https://github.com/forest-rabbit/SatCompute/pull/2) | 0 | `MERGED_REDUNDANT`，最终标签验证后删除 |
| `feature/n1-task-compute` | `fb4ccd2971b9043d157b1c095c1fa1e9a3d5d609` | [#1](https://github.com/forest-rabbit/SatCompute/pull/1) | 0 | `MERGED_REDUNDANT`，最终标签验证后删除 |
| `feature/pre-n2-drop-reason` | `c4ef4ec6ad545a5462a17105656f4c809e97d72c` | [#4](https://github.com/forest-rabbit/SatCompute/pull/4) | 0 | `MERGED_REDUNDANT`，最终标签验证后删除 |
| `feature/pre-n2-size-aware-hrw` | `72fe3d0cbe02f72cf7e357566c19fafd1d7a20b2` | [#6](https://github.com/forest-rabbit/SatCompute/pull/6) | 0 | `MERGED_REDUNDANT`，最终标签验证后删除 |
| `feature/pre-n2-stable-ecmp` | `ec81c5ade5e263a2c97dd989720cd84a025aa717` | [#5](https://github.com/forest-rabbit/SatCompute/pull/5) | 0 | `MERGED_REDUNDANT`，最终标签验证后删除 |
| `refactor/pre-n2-code-cleanup` | `f6668d52d655aa023a75d908819f1d934e17e1b6` | [#7](https://github.com/forest-rabbit/SatCompute/pull/7) | 0 | `MERGED_REDUNDANT`，最终标签验证后删除 |
| `docs/n1-ecmp-closeout` | `c272f0b0072b9538a832f68203f53d563f883fc0` | [#9](https://github.com/forest-rabbit/SatCompute/pull/9) | 4 | `SQUASH_MERGED_TREE_EQUIVALENT`，最终标签验证后删除 |
| `main` | `f9732aa4d32f09174635554c3a310be31bec2271` | — | — | `KEEP_CANONICAL` |

PR #9 使用 squash merge，因此 `docs/n1-ecmp-closeout` 的四个临时提交不是
`main` 的祖先。删除前实测两端 tree SHA 均为
`35f75d016d8d48fc5b85de85426b92709e630583`，且 `git diff --quiet` 返回 0；
其最终内容已完整进入 `main`。本审计修订分支
`docs/n1-ecmp-ref-audit-fix` 必须使用普通 merge，并在删除前满足独有提交数为
0。最终远端 heads 集合应只有 `refs/heads/main`。

## 3. 未合并的本地调查分支

`investigate/n1-fqcodel-ecmp` 位于
`24d4d3ed6c31c09ff3b5d09fa24b68ad28a5c527`，相对 `main` 有四个独有提交：

```text
167f227510be5d39f020f5d935969f19e5fad8b5  expose FlowMonitor drop reasons
bc14f8af4a2378328bbcf6aa7e81679d97535082  isolate FqCoDel queue-disc drops
da98d5ef11bb2528300c1469beca65058eeff1e0  replay N1 FqCoDel loss window
24d4d3ed6c31c09ff3b5d09fa24b68ad28a5c527  summarize FlowMonitor drop reasons
```

该分支分类为 `ABANDONED_EXPERIMENT`，不合并。原因不是其目标无效，而是主线
已通过 PR #4 及后续 metrics 重组保留了经审查的 DropReason、FqCoDel replay、
失败目录和 checker 合同。四个输入 fixture 在目录扁平迁移前后具有完全相同的
Git blob：

| fixture | 调查分支 blob | 当前主线 blob |
| --- | --- | --- |
| `fqcodel-bottleneck/nodes_0s.json` | `b9da563484bac264022e2597c9626feb27ce4840` | `b9da563484bac264022e2597c9626feb27ce4840` |
| `fqcodel-bottleneck/topology_0s.json` | `1651cc8aed0170cec780979fa6e1c67fafa24658` | `1651cc8aed0170cec780979fa6e1c67fafa24658` |
| `fqcodel-bottleneck-transfers.json` | `c4db504bdae4dd4d9e0fd7638fb21b0a05ac17fa` | `c4db504bdae4dd4d9e0fd7638fb21b0a05ac17fa` |
| `n1-75-fqcodel-replay.json` | `b0003d0c31327d3203b229eec2e7bd42f1ba7d0e` | `b0003d0c31327d3203b229eec2e7bd42f1ba7d0e` |

因此删除该本地分支不会丢失仍被采用的输入或诊断合同；未采用的历史实现仍可由
本审计中的提交 SHA 定位，但不再以长期 ref 保留，也不承诺在 Git 垃圾回收后仍
可达。

## 4. 标签

表中的目标均为 annotated tag 解引用后的提交；审计时全部是 `main` 的祖先。

| 标签 | 目标提交 | 分类与处置 |
| --- | --- | --- |
| `customer-jsontopo-v1` | `5e6e744f47ac3a3cc123048e30b89af404e99d96` | `MIGRATION_CHECKPOINT`，删除 |
| `customer-jsontopo-v2` | `19bb06d244d8473052bb13b483e1349d2fd6aac3` | `MIGRATION_CHECKPOINT`，删除 |
| `customer-jsontopo-v2.1` | `0c98f378aaf4fdc7237c6893f90e5bc61b624b77` | `MIGRATION_CHECKPOINT`，删除 |
| `n0-complete` | `d67ca0a164db5b1eacca04d4fcca3a1a295ca8cf` | `KEEP_MILESTONE` |
| `n1-pr1-final` | `fb4ccd2971b9043d157b1c095c1fa1e9a3d5d609` | `INTERMEDIATE_REVIEW`，删除 |
| `n1-pr2-review-final` | `66f8d770fa24326282b3518e438355ac72e2471c` | `INTERMEDIATE_REVIEW`，删除 |
| `n1-complete` | `8d6d79ff4a70ef5290ee85f424e4b59bfc038412` | `KEEP_MILESTONE` |
| `pre-n2-routing-final` | `fd393bda4d93c94fe711bc971cdf796c28bd2200` | `INTERMEDIATE_REVIEW`，由最终标签替代后删除 |
| `n1-ecmp-complete` | 最终收尾 merge commit | `CREATE_AND_KEEP_MILESTONE` |

最终 tags 集合应严格为：

```text
n0-complete
n1-complete
n1-ecmp-complete
```

## 5. 执行门禁

清理按以下顺序执行，任何一步失败都停止后续删除：

1. 收尾 PR 的 Fast Smoke 成功并合并；
2. 收尾 `main` 提交的自动 Full Regression 成功；
3. 同一提交的独立 `workflow_dispatch` Full Regression 成功；
4. 在该提交创建并推送 annotated tag `n1-ecmp-complete`；
5. 从本地和远端读取标签，确认对象类型、目标提交及 `main` 祖先关系；
6. 重新抓取远端 refs：逐项确认旧 head 未意外变化，确认 squash 分支 tree
   相同，并确认本审计修订分支的独有提交数为 0；
7. 删除精确列出的旧远端分支、旧本地分支和中间标签；
8. 再次读取远端 heads/tags，验证最终集合，不使用通配符或批量推断删除。

PR 和 Actions URL 是 GitHub 对象，不随分支或标签删除而消失。普通合并分支与
旧标签的目标提交继续由 `main` 历史可达；squash 分支的最终 tree 已进入
`main`，但临时提交不作为长期 ref 保留。明确归类为 `ABANDONED_EXPERIMENT`
的本地实现同样不再保留 ref。
