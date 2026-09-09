# N4 最终 Release Validation

**RELEASE VALIDATION = PASS；N4 发布待主线集成、阶段 CI 与人工审阅；N5 NOT STARTED。**
日期：2026-09-09。本次不是重新标定，也不是 N5 真实备份性能实验。

## 运行身份

- 运行 commit：`a78ee0b4d646f5d88195eb80e55bc61b273c0275`；启动时工作区干净。
- 分支：`feature/n4-final-closeout`；G3 [PR #90](https://github.com/forest-rabbit/SCP-SatComPlate/pull/90)
  merge `233bb757ee0da228fe9d77b6cfac2929f9d6f352`，G4
  [PR #91](https://github.com/forest-rabbit/SCP-SatComPlate/pull/91)
  merge `5e8df4b094b8f1260c477c5900f58e7cee38bdb1`。
- 正式输入：[LEO-66](../../../contrib/satcompute/input/experiments/leo-66/README.md)。
  66 星、800 任务、1050/1300 s、100000 WU/s、10 Gbps、fixed 单向 1 ms、seed1/run11、deadline1.3。
- 与已接受 G4 参考 `output/n4c-g4-shadow-20260909/fault-11/` 对比；原始输出未修改。

```bash
.venv/bin/python contrib/satcompute/tests/integration/regression/run-final-scenario.py \
  --output-dir=output/n4-release-validation --audit --shadow
```

唯一一次正式 1300 s generate+F1/F2/F3+audit+shadow，退出码0，实际用时500.645 s。
未追加 none、多 seed 或参数扫描。既有 CI 功能回归保留，不计入这一次正式场景运行。

## 真实业务与概率

| 指标 | 结果 |
|---|---:|
| 完成 / 失败任务 | 717 / 83 |
| F1 / F2 / F3 START | 84 / 2 / 1 |
| F1 / F2 / F3 直接 RUNNING victim | 82 / 0 / 1 |
| 完成 / 取消 transfer | 1517 / 83 |
| 丢包 / 仿真截断 | 0 / 0 |
| 故障引起路由重算 | 1 |
| 概率匹配记录 | 3338；无缺失、无上下文不一致 |

F1、F2、q_comp_1s、P_fail_before_finish 四项概率的 MAE/RMSE/max absolute error 均为0，
校验容差1e-12。F3 仍为 node62、1027.055770726 s、task120，故障和 victim 集未变。

22 份真实业务文件均与参考一致；CSV 逐字节比较，JSON 结构比较。
`run-summary.json` 只剔除 wall_clock_ns/s，并对 compute_profile_path、task_trace_path
两项迁移路径做显式对应：先从已审阅提交 `8c3c27724a4723e9bba9abb2f99349566254af3d`
取出原路径的输入字节，与当前正式文件逐字节核对，再替换路径字符串比较。
未忽略其他字段，未修改任何原始结果，未使用 SHA-256。

## Shadow 与资源账本

387 START、382 ON；82 个 F1/F2 direct victim 中77个故障时 ON、5个 INITIALIZING、0个 OFF。
5份 shadow 原始文件（4 CSV + assumptions JSON）与参考逐字节一致；
独立频率枚举、虚拟字节/队列守恒及全部 G4 派生统计均保持一致。

| 指标 | ALL-OFF 重计算 | CompFRR shadow |
|---|---:|---:|
| 全生命周期 waste（WU_eq） | 25625072.100480 | 1354096.228004 |
| 正常维护额外 WU | 0 | 494810 |
| 故障恢复 waste（WU_eq） | 25625072.100480 | 859286.228004 |
| catch-up mean / P50 / P90（s） | 3.12501 / 2.85322 / 6.47742 | 0.10479 / 0.05928 / 0.25850 |

recovery-only saving=96.647%，net lifecycle saving=94.716%。F3 单列，不纳入主要恢复收益；
本轮没有 F2 直接 victim，不能据此推断 F2 保护覆盖率。

## 门禁与证据位置

定向构建、16个C++测试程序、29项Python测试（原生切片双次复现实际执行、无skip）、
全部小型smoke（含8任务shadow零副作用）、CLI、fixture/path与文档链接检查通过。
迁移的正式/测试 JSON 与 CSV 均保持字节一致；运行模型、生成器数学、故障参数与随机流不改。

本地 `output/n4-release-validation/` 保存 execution.json、execution-result.json、run.log、time.txt、
全部原始业务/概率/shadow结果，以及 g4-equivalence.json、g4-summary.json、
probability-comparison.csv/json 和 release-validation.json。输出由gitignore排除，不随代码提交。

G4 仍是理想资源下的解析/旁路验证，没有实际 checkpoint、备份流量、CPU/存储预留或恢复执行。
原平台的 FAILED 任务没有被救回；N5 正式运行不得依赖 G4 shadow API。
阶段 CI 与最终人工审阅完成后才允许主线集成、创建 n4-complete 并清理内部分支/tag。
