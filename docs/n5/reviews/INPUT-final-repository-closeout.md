# INPUT 仓库收口

## 范围与起点

本轮只统一参数、保留生产测试并清理研究工具，不改 SER、Frequency、Placement、
Recovery、故障随机流或正式场景；不重跑 1300 s 性能矩阵。默认 `inputPolicy=eager`。

起点：`feature/compfrr-input-admission-runtime@461d76713`，工作区干净。
`n5@aa7a49a1c`；worthiness=`34177d0cf` 是 runtime 的祖先。
CB-Sat=`9a2f029cf` 已经由 #98 整合进入 n5；JIT=#99（base CB-Sat）尚未合并，
head=`2ff3c5c98` 有 5 个独有提交，关闭前须制作并验证 Git bundle。
main=`009788ca9`、legacy/ns-3.33=`f4c7bff66` 保持不变。

## 分阶段验收

1. 固定仓库内 SER fixture：405 个网络候选逐任务结果，68 SEND；另 4 个 LocalDelivery。
2. 单一 INPUT 参数与最小生产因果快照，验证三模式旧新语义等价。
3. 按 import/CMake/调用关系删除离线工具，保留通用 lifecycle/accounting validator。
4. Canonical 文档收口，完整 build/Python/C++/smoke/regression。
5. 推送并创建到 n5 的 PR；最终提交一次阶段 CI，通过后 merge commit 整合。
6. 验证归档/祖先后关闭 #99、依次删除 JIT、CB-Sat、worthiness、runtime 分支。

主动 INPUT 的真实流生命周期、receiver join、USED、合法 refetch 与实际字节账本必须
完全保持。只移除旧 CLI 和开发用 `input-start-snapshots.json`，保留生产 CSV schema。
大快照的数据采集位置仍为 actual pair 固定且 post-batch revalidation 成功之后、
START_CHECKPOINT 执行之前。清理不能删除 SER 实际依赖的因果输入。

## 状态

G0 已重新核验：7 个远端分支与任务书一致，唯一 open PR 为 #99。
G1a：已提取 tracked causal fixture（409 行），6 项纯 SER 测试通过；405 个网络候选
逐任务完全匹配（68 SEND），4 个 LocalDelivery。测试不再因 ignored output 缺失而跳过。
旧三模式小场景输出已保留在 `output/input-repo-closeout/before-mapping/`。
G1b：完整构建通过；17 项 INPUT Python tests、12 项场景测试（1 项既有切片缺失跳过）、
22 个 INPUT lifecycle 场景（374 checks）通过。Eager/Deferred/Selective 对应旧组合的
33/34/37 份小场景输出完全一致；SELECTIVE 只删除开发大快照。
完整 corrected-chain 小型门禁：1,984 份 CSV/JSON（1,723 CSV）相同，唯一授权删除为
旧 SER fixture 的 `input-start-snapshots.json`；生产 CSV schema 与值均不变。
因果 snapshot 现位于 `policy/compfrr/input/selective-input-snapshot.*`，只含生产实际使用量。
维护账本 validator 已脱离大快照与旧 runner，改用真实 checkpoint START 和 committed
Frequency actual pair/time 交叉验证；物理准入与概率窗口继续由 C++ 接线测试检查。
执行中。最终 PR、CI、删除 manifest 与 gate 结果在对应步骤完成后补充。
NEXT PLANNED WORK：Multi-tree baseline integration（需要单独批准）。
