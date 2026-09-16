# N5 Stage 2B 历史诊断归档

状态：ARCHIVED HISTORICAL DIAGNOSTIC；DO NOT MERGE / REBASE / CHERRY-PICK INTO MAIN。

- archive branch：`archive/n5-stage2b-residual-deadline-audit`（本地归档）。
- archive snapshot commit：`de728cd4e8334a4eb354cc061b57f4a4e6d13f9c`。
- source commit：`b69299e6a04cf2b57a1445175ea6c38d84d9bae1`，原 candidate-coverage 分支。
- 本清单为后续独立文档提交，避免让快照自身引用尚不存在的 commit。
- 当时执行为上述 source 加开发补丁；归档提交不是新的仿真执行版本，不改写 execution.json。
- 运行身份：1 Gbps、seed=1、run=11、800 tasks、1300 s、CompFRR-P Cumulative、
  Selective、Relocate、online generate；只增加默认关闭的候选日志。
- 结果：785/800，367 START；S/N 在 residual 11 tasks 上均选择 193/244/513。
- 本次归档只保存原文件和摘要副本，无新的模型或 production 演进；
  17 项 residual Python tests 通过，既有 build/649 文件等价证据见 small-gates.json。
- 不开启 PR，不合入 main，不运行 CI 或重跑历史实验。归档后回到 n5-complete。

## 文件分类

A：17 个 tracked modifications 均为 Stage 2B 采集/配置/CMake/测试/说明接线。
B：10 个原新增诊断代码、脚本、测试和报告；新增 7 个小摘要副本随快照提交。
C：原始 raw output 全部 ignored，继续留在原路径，不删除、不移动、不改内容或时间戳。
D：未发现来源不明的非 ignored 改动；既有其他实验 output、.venv、.vscode、
旧 ignored generation/topology 均保持原状，不属于本次归档内容。
E：build、cmake-cache、.lock-ns3_linux_build、__pycache__ 只作本地可重建缓存，不入 Git。

### 快照跟踪文件（34）

- `AGENTS.md`
- `contrib/satcompute/CMakeLists.txt`
- `contrib/satcompute/metrics/README.md`
- `contrib/satcompute/metrics/core/frequency-metrics.cc`
- `contrib/satcompute/metrics/diagnostics/residual-deadline-audit.cc`
- `contrib/satcompute/metrics/diagnostics/residual-deadline-audit.h`
- `contrib/satcompute/protection/README.md`
- `contrib/satcompute/protection/policy/compfrr/compfrr-controller.cc`
- `contrib/satcompute/protection/policy/compfrr/compfrr-controller.h`
- `contrib/satcompute/protection/policy/compfrr/frequency/compfrr-frequency-policy.cc`
- `contrib/satcompute/protection/policy/compfrr/frequency/compfrr-frequency-policy.h`
- `contrib/satcompute/protection/protection-config.cc`
- `contrib/satcompute/protection/protection-para.cc`
- `contrib/satcompute/protection/protection-para.h`
- `contrib/satcompute/satcompute.cc`
- `contrib/satcompute/tests/README.md`
- `contrib/satcompute/tests/integration/regression/audit-residual-deadline.py`
- `contrib/satcompute/tests/integration/regression/audit-residual-selective-s-vs-n.py`
- `contrib/satcompute/tests/integration/regression/run-residual-deadline-development.py`
- `contrib/satcompute/tests/support/protection/multitree_comparison_audit.py`
- `contrib/satcompute/tests/support/protection/residual_deadline_audit.py`
- `contrib/satcompute/tests/support/protection/selective_sn_residual_audit.py`
- `contrib/satcompute/tests/unit/frequency-runtime-test.cc`
- `contrib/satcompute/tests/unit/protection-config-test.cc`
- `contrib/satcompute/tests/unit/test_residual_deadline_audit.py`
- `contrib/satcompute/tests/unit/test_residual_selective_sn_audit.py`
- `docs/n5/reviews/CompFRR-1G-residual-deadline-decomposition-stage2b.md`
- `docs/n5/reviews/stage2b-archive/classification-summary.json`
- `docs/n5/reviews/stage2b-archive/execution-equivalence.json`
- `docs/n5/reviews/stage2b-archive/representative-cases.md`
- `docs/n5/reviews/stage2b-archive/residual-selective-s-vs-n-summary.json`
- `docs/n5/reviews/stage2b-archive/residual-selective-s-vs-n-tasks.csv`
- `docs/n5/reviews/stage2b-archive/residual-task-summary.csv`
- `docs/n5/reviews/stage2b-archive/small-gates.json`

## Raw evidence（全部 ignored，约 73 MiB）

根目录：`/home/emsky/project/SCP-SatComPlate/output/compfrr/1g-residual-deadline-audit/`。

- `instrumented-run11/`：原始仿真 CSV/JSON、execution.json、execution-result.json、source-diff.patch。
- `unit-off/`、`unit-on-verified/`：原 649 文件被动等价证据。
- `old-evidence-inventory.json`：原历史文件清单。
- 根目录中的分解及 S/N candidate CSV：未提交的大表；小摘要已复制到本分支
  `docs/n5/reviews/stage2b-archive/`，原件保留。
- Git 对摘要 CSV 按仓库规则标准化 CRLF/LF；原 raw CSV 的字节未改写。未新增 SHA-256。

关键原件大小/mtime（归档时）：

- `output/compfrr/1g-residual-deadline-audit/instrumented-run11/residual-deadline-candidates.json | 6349032 bytes | 2026-09-15 21:56:10.030926809 +0800`
- `output/compfrr/1g-residual-deadline-audit/instrumented-run11/source-diff.patch | 22744 bytes | 2026-09-15 21:36:30.339422406 +0800`
- `output/compfrr/1g-residual-deadline-audit/residual-deadline-decomposition.csv | 1149079 bytes | 2026-09-15 21:57:51.569403884 +0800`
- `output/compfrr/1g-residual-deadline-audit/residual-input-analysis-time.csv | 698243 bytes | 2026-09-15 21:57:51.596982248 +0800`
- `output/compfrr/1g-residual-deadline-audit/residual-selective-s-vs-n-candidates.csv | 400105 bytes | 2026-09-15 22:15:46.662265903 +0800`

## 恢复与边界

用 `git show archive/n5-stage2b-residual-deadline-audit:<path>` 读取文件，
或另建 archive worktree；不要覆盖正常 main 工作区。
原始执行涉及的旧 N5C/recent-U 名称仅属于该历史快照，不重新引入 main。
N6 必须从干净 main/n5-complete 开始，而不是从本归档分支开始。
