# N5 / N5R 分支与 PR 收口计划（只读）

日期：2026-09-14。当前 `fix/checkpoint-maintenance-semantics`，HEAD `5fc83fd08c76448fe6380f1c014ee9bb2211a2b3`。审计前 worktree 干净；本轮只新增六份审计产物，尚未提交/推送。下面是后续建议，不是操作记录。

已只读核对本地 `git branch/log/merge-base`、GitHub PR 列表以及远端 heads。未 fetch、checkout、merge、修改 PR、创建 tag 或删除分支。提交短 ID 仅标识 Git 历史，不涉及增加 SHA-256 安全保护。

## 1. 分支事实

`main@009788ca9` 与远端一致，是 n5 的祖先和 N4 发布线；它不属于待归档/关闭的对象。

| 分支 | 当前 tip | 远端状态 | 包含关系 | 建议分类 |
| --- | --- | --- | --- | --- |
| n5 | `17c4414dc` | 与本地一致 | 已包含 CB-Sat #98，尚未包含下面 N5C/两项修复 | MERGE_LATER（整合目标） |
| feature/n5c-backup-placement-v4 | `f5a479ae3` | 与本地一致 | 当前 corrected tip 的祖先，不是 n5 的祖先 | MERGE_LATER；整链整合后可 SUPERSEDED |
| feature/n5c-u-refinement | `c1a8704cd` | 与本地一致 | 包含上一行；是当前 tip 祖先，不在 n5 中 | MERGE_LATER；整链整合后可 SUPERSEDED |
| fix/recovery-deadline-feasibility | `08236b8af` | 本次远端 heads 查询未发现同名分支，仅本地 | 在 U refinement 之后，已包含于 maintenance tip | SUPERSEDED（仅指被后续本地修复覆盖；n5 尚未整合，不可删） |
| fix/checkpoint-maintenance-semantics | `5fc83fd08` | 与本地一致 | corrected 基线，包含两项修复和 N5C 链 | MERGE_LATER |
| feature/pre-n5c-cb-sat | `9a2f029cf` | 与本地一致 | 已是 n5 与当前 tip 的祖先；仍为 #99 的 base | KEEP_ARCHIVE_BRANCH，至少保留到 #99 处置完成 |
| feature/pre-n5c-compfrr-v7-jit | `2ff3c5c98` | 与本地一致 | 不在当前 tip 中，独立历史实验线 | KEEP_ARCHIVE_BRANCH；不并入 N5C/N5R 主线 |
| legacy/ns-3.33 | `f4c7bff66` | 历史保留分支 | 不参与此次结构整理 | KEEP_ARCHIVE_BRANCH，保留旧平台历史 |

JIT 与当前 CB 分支的对称差提交数为 2 / 5；因此不能把 JIT 当作“当前 CB 加一个开关”的干净直系子分支直接快进，也不能因 #98 合并就删除它。

当前实际祖先顺序如下，箭头表示包含关系，不表示各提交之间没有其他提交：

```text
main 009788ca9
  → n5 17c4414dc                [CB-Sat #98 已集成]
    → N5C placement f5a479ae3   [#100]
      → U refinement c1a8704cd [#101]
        → recovery fix 08236b8af
          → maintenance fix 5fc83fd08  [本轮审计基线]
```

结论：不能从现在的 `n5@17c4414dc` 直接创建 N5R 然后声称是在当前修复基础上整理；这样会漏掉 deadline / INPUT_PATH_UNAVAILABLE fallback 和 ON maintenance 修复链。

## 2. PR 事实及建议

| PR | 只读查询状态 | base ← head | 后续建议 |
| --- | --- | --- | --- |
| [#100](https://github.com/forest-rabbit/SCP-SatComPlate/pull/100) | OPEN，非 Draft | n5 ← feature/n5c-backup-placement-v4 | MERGE_LATER；若采用整链 PR，确认内容进入 n5 后再 CLOSE_KEEP_HISTORY |
| [#101](https://github.com/forest-rabbit/SCP-SatComPlate/pull/101) | OPEN，Draft | feature/n5c-backup-placement-v4 ← feature/n5c-u-refinement | MERGE_LATER；不得在 base 先删除后才处理；整链方案可后续 CLOSE_KEEP_HISTORY |
| [#99](https://github.com/forest-rabbit/SCP-SatComPlate/pull/99) | OPEN，非 Draft | feature/pre-n5c-cb-sat ← feature/pre-n5c-compfrr-v7-jit | 建议用户批准后 CLOSE_KEEP_HISTORY；保留 JIT 分支，不并入正式主线 |
| [#98](https://github.com/forest-rabbit/SCP-SatComPlate/pull/98) | MERGED，2026-09-13 09:24:44 UTC | n5 ← feature/pre-n5c-cb-sat | 已完成，不重复 merge；只修正未来文档状态 |

查询时没有看到两个 fix 分支对应的现有 PR。后续创建前仍须重新只读查询，不能以本次快照假设它们一直不存在。

## 3. 推荐 closeout 路线：整合 corrected 链，再开 N5R

1. 用户确认架构审计并授权实施/分支操作。先重新核对 worktree、PR head/base、远端 tip，保护用户新增改动；本轮六份审计文件是否提交与推送也等待明确指令。
2. 以包含 recovery + maintenance 的 corrected tip 为整合候选，汇总 #100/#101 与两项修复的已有审阅/证据。不得只 cherry-pick 末尾修复而遗漏其前置实现，不混入 JIT。
3. 可建立一个 corrected-chain → n5 的整合 PR。这个 PR 应明确 supersede #100/#101 的哪部分，而不是绕过审阅；保留原 PR 的审阅链接、实验身份和历史结果边界。
4. 按既定阶段节奏完成必要 small semantic gate，再在批准的整合阶段运行一次 CI；不是每个结构提交都启动 CI，也不在 GitHub 跑整套 1300 s 正式矩阵或 ns-3 全局 examples/tests。若项目阶段规则将 CI 放在 n5→main 门禁，则使用该门禁，不重复造一套逐 PR CI。本轮不触发。
5. 用户认可 gate 与 PR 后合入 n5，验证 corrected tip 的代码已包含。优先采用保留祖先关系的整合方式；若选择 squash，不能用 `merge-base --is-ancestor` 为假就断言丢失，须补 patch/content 与语义 gate 对照，未经确认不清理。
6. 只有在内容已进入 n5 后，才将被替代的 #100/#101 关闭为保留历史；链接整合 PR，不抹除历史审阅。不要提前关闭后再发现未合入。
7. 从新的 corrected n5 建议创建 `refactor/protection-architecture-consolidation`，作为 N5R 单一实施分支。按照主报告的分步提交和 small equivalence gate 进行；不趁整理选择新的 U 胜者或改变正式参数。

如果用户希望先在 corrected tip 上完成 N5R，也可以从该 tip 建分支，明确它叠在尚未合入 n5 的修复之上；但 PR 描述必须保留依赖链，不能伪称 base 已整合。推荐先整合的原因是减少重构 diff 与历史修复 diff 混合。

## 4. 备选路线：保留现有 stacked PR

若希望保留每层 PR 的审阅/合并记录，可以按以下顺序而非整链 PR：

1. 完成 #100 的审批并合入 n5。
2. 核对 #101 的 diff，必要时在授权后将 base 改为新 n5，完成审批与合并；不改变 U 参数或补跑正式矩阵。
3. 为 recovery / maintenance 修复创建一份或两份依赖明确的 PR，保证 INPUT 路径早退和 maintenance 全部进入 n5。
4. 验证整合结果与当前 corrected baseline 的 small 语义等价，再从新 n5 开 N5R。

两条路线二选一，不重复合并相同实现。不可为了“清理分支”先删除 #101 的 base；合并方式若产生非直系 history，也先做包含性检查再处理余下 PR。

## 5. 清理与保留 gate

| 目标 | 何时才可执行 |
| --- | --- |
| 两个 fix 本地分支 | corrected 代码已合入批准目标、没有未提交改动、没有其他分支/PR 依赖；远端不存在的不得报告为已删远端 |
| N5C placement/U 远端与本地分支 | #100/#101 已合并或明确 superseded、后继 PR 不再以它们为 base、代码/证据可追溯并获用户授权 |
| CB 分支 | #98 已合并仍不充分；先确认 #99 不再依赖其 base，且历史复现有保留位置 |
| JIT 分支 | 本计划建议保留为 archive，不自动删除；关闭 #99 也不等于删分支 |
| N5R 分支 | 全部约定的结构整理、small gate、阶段审阅/CI 与合并完成后再清理 |
| main / legacy / 发布 tag | 不属于本轮清理；不新打 n5-complete、不宣称 N5/N6 发布 |

历史正式矩阵不在本次 closeout 中刷新。旧结果能否沿用取决于维护轨迹与完整资源账本等价，不能靠 PR 合并状态或 completion 相同推断。N6/N7 的正式对照、INPUT 后续研究与最终发布安排仍另行批准。
