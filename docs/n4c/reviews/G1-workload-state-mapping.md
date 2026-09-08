# G1：工作量与恢复状态映射审阅包

日期：2026-09-08。状态：**G1 v2 修订及本地验证完成，待再次审阅；未进入 G2。**
依据工作区任务书 Codex_N4C_G1_Review_Workload_State_Revision_v2.md。
代码和两次原始预览固定到 **702f53eee9ee6af0b6db4f348c95e50e81fd6c8e**，
生成时工作区干净；后续报告提交不改变代码或结果。测试通过不代表 G1 已获批准。

## 1. 范围与清理

N4C 只冻结任务工作量与恢复状态映射，不生成正式 L1/remote/tail backup object，
不搜索 (n, delta)。相关执行与实际状态大小从 N5A 开始，频率优化从 N5B 开始。
本轮保留输入、WU、完整可变状态、sigma/H、合法应用边界及5/10/20%预算守恒验证。

相对首版清理了：

- 1.0%–10.0%、步长0.1个百分点的搜索网格，以及 checkpoint_grid_rows。
- checkpoint-grid.csv 及其频率间隔统计；改为12行 state-budget-checks.csv。
- 自动展开的多档速率敏感性表；预览只报告本次显式参数，仍允许 CLI 改参数。
- 将诊断对象 CheckpointBudget / checkpoint_budgets 改为
  StateBudgetPoint / state_budget_points，避免与 N5 真实 L1 混淆；字节分账输出改用 h_bytes。

上一轮未提交的 checkpoint_size_analysis.py 已按用户要求撤回，本轮没有恢复它。
不存在 n 枚举、batch/tail统计、10/100/500 MB分桶、成本分段或频率优化器。
旧三个预览目录已完整移至 output/history/n4c-g1-pre-v2/，可恢复但不是v2证据。
旧代码仍可从 Git 历史追溯；没有删除其他模块、修改用户任务书或清理未合并分支。

## 2. 分支、改动文件与外部影响

平台分支为 feature/n4c-workload-mapping，本轮起点94a3dd4bf，main基线c40e3f56a。
按任务书提交并推送原分支供审阅；不创建/合并 PR、不移动 tag、不运行阶段 CI。
TaskModeling保持main / 0dbc0c7b6281219e1356151fd640336cde885e7d，工作区干净。

| 修改位置 | 内容 |
|---|---|
| contrib/satcompute/tools/generation/task_workload_model.py | 图像WU重标、通用状态预算点命名；保留整数/有理数账本 |
| contrib/satcompute/tools/generation/preview-n4c-workload.py | 新速率、LLM token范围、时长/需求统计、三粒度诊断输出 |
| contrib/satcompute/tests/unit/test_n4c_workload_model.py | 新尺度、完整字节不变、sigma更新、合法边界与纳秒测试 |
| contrib/satcompute/tests/unit/test_n4c_workload_preview.py | 1500项预算、短任务区间端点、不同种子、守恒和CLI确定性 |
| generation/tests 的 README | 入口与范围说明 |
| docs/n4c/plan-and-baseline.md、workload-mapping.md、本报告 | v2合同、参数解释与验收结果 |

只使用现有 Python 标准库与项目 .venv，不下载/运行 Qwen、tokenizer 或图像算法。
没有修改正式生成器、TaskTrace、ComputeProfile、para.cc、故障模型、旧 fixture、上游 src
或 CI；正常平台运行不会新增 CSV。本轮不是1500任务网络或故障实验。

## 3. 图像 WU normalization

三类统一a_z=1，W=ceil(3*S*a_z/2000)，参考算力100,000 WU/s，无最小时长。
不依赖ID、排名、任务集合或输入顺序，不用rho/RESULT/压缩率推导工作量。

| 输入（十进制） | WU | 参考纯计算时间（s） |
|---|---:|---:|
| 10 MB | 15,000 | 0.15 |
| 50 MB | 75,000 | 0.75 |
| 100 MB | 150,000 | 1.5 |
| 500 MB | 750,000 | 7.5 |
| 1 GB | 1,500,000 | 15 |

相同输入与表示下，重标不改变完整状态Byte、rho、H或RESULT；sigma随WU增大而
约为旧值2/3。整数取整后使用精确K_variable/W，不直接乘显示值2/3。
15 s延续审阅中的目标；相对已提交首版20,000 WU/s的50 s，实际时长确实缩短了。

## 4. 四类时长、总工作量与计算服务需求

默认种子n4c-g1-66，1500项，数量450/450/450/150，总INPUT精确81,750,000,000 B。
保留15个1 GB、30个500 MB图像：dense为4/7个，compression为11/23个。

| 类型 | 数量 | 总 WU | 总服务需求（s） | 时长 min / median / p95 / max（s） |
|---|---:|---:|---:|---|
| dense-image | 450 | 36,681,620 | 366.81620 | 0.12404 / 0.54645 / 1.06633 / 15 |
| sparse-inference | 450 | 27,113,641 | 271.13641 | 0.12404 / 0.62226 / 1.04466 / 1.09882 |
| compression | 450 | 58,830,256 | 588.30256 | 0.12404 / 0.65475 / 7.5 / 15 |
| 三类图像合计 | 1350 | 122,625,517 | 1226.25517 | — |
| llm | 150 | 114,089,400 | 1140.89400 | 5.033 / 7.633 / 9.6858 / 9.979 |
| 全部 | 1500 | 236,714,917 | 2367.14917 | — |

LLM占全部纯计算服务需求 **48.19696%**。3:3:3:1是任务数量比例，不是计算服务需求比例。
时长按平台整数纳秒公式计算；默认速率下总需求正好等于总WU/100000。
median/p95采用线性插值描述分位数，不是置信区间。

下表每格为“数量（占该行任务数比例）”。前三列是累计阈值，不能将六列直接相加。
其余区间明确采用 [2,5)、[5,10]、>10 秒。

| 类型 | <0.5 s | <1 s | <2 s | 2–5 s | 5–10 s | >10 s |
|---|---:|---:|---:|---:|---:|---:|
| dense-image | 194（43.11%） | 401（89.11%） | 439（97.56%） | 0（0%） | 7（1.56%） | 4（0.89%） |
| sparse-inference | 187（41.56%） | 405（90.00%） | 450（100%） | 0（0%） | 0（0%） | 0（0%） |
| compression | 166（36.89%） | 371（82.44%） | 416（92.44%） | 0（0%） | 23（5.11%） | 11（2.44%） |
| 三类图像合计 | 547（40.52%） | 1177（87.19%） | 1305（96.67%） | 0（0%） | 30（2.22%） | 15（1.11%） |

保留短任务是本轮约定，不补时、不删样本。按ID轮转到66个假想节点的总需求仅用于
离线估算；单节点min/median/p95/max为12.61157/34.74117/58.8991725/76.45373 s。
这不是实测利用率、无排队保证或N4C网络验收结果。

## 5. 完整恢复状态、rho、sigma与H

以下三类图像均为100 MB输入，H使用原测量标签；LLM为768 B请求、P=200/G=7300。
实际1500项使用十进制task ID作为标签，图像H为45–52 B；不统一写成65/85 B。

| 类型 | WU | K_variable（B） | rho_variable | sigma_variable（B/WU） | H（B） | RESULT（B） |
|---|---:|---:|---:|---:|---:|---:|
| dense-image | 150,000 | 100,000,762 | 1.00000762 | 666.6717467 | 65 | 100,000,000 |
| sparse-inference | 150,000 | 186,906 | 0.00186906 | 1.24604 | 85 | 186,906 |
| compression | 150,000 | 54,248,130 | 0.5424813 | 361.6542 | 65 | 54,248,130 |
| llm | 750,000 | 860,160,000 | 不定义 | 1146.88 | 0 | 29,200 |

rho只含payload+index；sigma只含可变状态，不重复加H。sparse/compression的规范RESULT
本来就包含描述符，数值可等于K_variable，但二者不能在所有类型中混同。完整参数和来源见
[工作量模型](../workload-mapping.md)；图像为已有参考比例外推，不是1500项新测量。

LLM沿用Qwen3-0.6B结构：28层、8个KV heads、head_dim=128，元素2 B是场景假设。
2*28*8*128*2=114688 B/token，W=100*(P+G)，K_KV=114688*(P+G)。
本轮150项实际N为5033–9979，KV为577,224,704–1,144,471,552 B。
INPUT仍是325–845 B的小型序列化请求，合计86,892 B；没有固定Byte/token等式。
sigma精确为28672/25=1146.88 B/WU；prompt只计一次，未完成token不增加KV预算。

LLM不是一次单纯WU换单位：相对首版N=1000–2000，N增大会增大完整KV。
本批LLM状态合计130,846,851,072 B；全部四类完整可变状态合计176,611,226,134 B，
RESULT合计45,768,636,818 B。这些不是内存实测或实际网络流量。
请求JSON中的G数字长度比首版合计增加40 B，整数分配器相应调整14个普通图像输入，
保持总INPUT不变。因此“不改变图像状态字节”是相同S下的模型性质，不是所有旧任务逐项不变。

本模型不下载权重、不运行推理或serializer。合成P/G不冒充tokenizer实测；prompt与
decode等成本、H_LLM=0、RESULT为4*G的uint32 token列表都是明确的仿真假设。
它们不能证明真实推理耗时或仅靠KV就能恢复。

## 6. 合法边界与预算验证

dense/compression沿用524,288 B的tile及末尾边缘块；sparse为完整文件边界，
预览按原100张/26,246,291 B的平均文件大小生成合成文件，非真实DOTA长度；LLM为整token。
向后继合法位置对齐，合并重复WU/进度，不允许零工作推进冒充新预算点。

全部1500任务各做5/10/20%检查，共4500组，验证WU、可变状态、重复H账本守恒及最终完成。
四个参考任务的12行诊断另存CSV；参考最大WU取整误差小于1 WU，LLM为0。
这不是L1记录输出，也不是频率参数选择，更没有remote/tail分布。

c_L(D_L)、c_R(D_R)只在合同中保留未来定义，不新增求值函数或毫秒参数。
旧5 ms/20 ms是数学示例，不是实测值。N5A负责固定n/delta/节点的备份恢复，
N5B负责频率优化及状态/成本分析，N5C负责节点选择和共享池。

## 7. 命令、测试和原始输出

在平台仓库根目录运行（输出目录必须未存在）：

~~~bash
source .venv/bin/activate
./ns3 build
PYTHONDONTWRITEBYTECODE=1 python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_n4c_workload*.py' -v
PYTHONDONTWRITEBYTECODE=1 python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
PYTHONDONTWRITEBYTECODE=1 python \
  contrib/satcompute/tools/generation/preview-n4c-workload.py \
  --output-dir output/n4c-g1-v2-20260908
PYTHONDONTWRITEBYTECODE=1 python \
  contrib/satcompute/tools/generation/preview-n4c-workload.py \
  --output-dir output/n4c-g1-v2-20260908-repeat
~~~

| 检查 | 结果 |
|---|---|
| 项目构建 | 通过；Ninja no work to do；examples/tests仍为OFF |
| G1 Python单元测试 | 17项通过，包含4500组预算检查及两个额外生成种子的LLM时长验证 |
| 项目全部Python单元测试 | 26项通过，旧生成器fixture未修改 |
| 两次独立CLI预览 | 五个业务文件逐字节一致，输出清单完全一致，拒绝覆盖测试通过 |
| 文档本地链接、变更检查 | 通过 |
| C++/smoke/完整regression、网络及故障实验、GitHub CI | 本轮未运行；未修改C++/运行时，后续阶段再做相应集成验收 |

项目环境为Python3.10.12、CMake3.31.10。两个输出的execution均记录上述完整代码提交、
worktree_dirty=false；命令路径等元数据不要求逐字节一致，不使用SHA256。

| 新输出文件 | 行数或用途 | 大小（B） |
|---|---|---:|
| summary.json | 数量、完整字节、时长、累计阈值、服务需求与占比 | 18,728 |
| task-budgets.csv | 1500项离线预算，无source/arrival/deadline | 297,603 |
| llm-requests.json | 150个实际序列化的小请求 | 95,642 |
| representative-budgets.csv | 21项代表预算 | 2,749 |
| state-budget-checks.csv | 12行5/10/20%守恒摘要 | 1,309 |
| execution.json | 代码、环境、命令及来源身份 | 元数据单独记录 |

原始输出由现有gitignore排除；仓库提交代码、参数合同和本报告，正常仿真仍不生成这些CSV。

## 8. 当前停止点

**停在G1，等待审阅者确认新WU尺度、参考算力、LLM token范围、无最小时长以及G1通过。**
不启动G2的正式TaskTrace/ComputeProfile/deadline接入，不调F1/F2/F3、不删replay，
也不展开N5。代码与证据仅说明本次候选实现一致，不能替代后续网络、故障或备份验收。
