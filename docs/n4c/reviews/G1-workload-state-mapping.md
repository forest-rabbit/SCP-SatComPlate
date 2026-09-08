# G1：工作量与恢复状态映射审阅包

日期：2026-09-08。状态：**第一阶段实现与本地验证完成，G1 待用户审阅；尚未批准。**
本报告的代码与原始预览固定到 `d8eab893a572391ef6fa3410c01db813eb300612`；
报告所在的后续文档提交不改变代码或参数。测试通过不等于工作量的物理解释已获认可。

## 1. 本批次目标与实际完成范围

完成 N4C-0 相关基线/接口盘点及 N4C-1 图像 WU/状态、LLM 公式、sigma/H 分账、
合法进度与算力候选预览。遵循最新对话，仅修改平台仓库；TaskModeling 只作为已有
测量来源。不下载模型、不运行 tokenizer/推理，也没有新增依赖。

这批只交付离线预算工具，不改变正式生成器、TaskTrace、ComputeProfile、para.cc、
故障参数或运行行为。未执行 checkpoint 传输、deadline/地理负载生成或 N5 算法。

## 2. 仓库、分支、提交与 PR

| 仓库 | 分支/基线 | 本批状态 |
|---|---|---|
| SCP-SatComPlate | 从 `main` 的 `c40e3f56a` 建立 `feature/n4c-workload-mapping` | 代码提交 `d8eab893a`；证据生成时工作区干净 |
| SCP-TaskModeling | `main` / `0dbc0c7b6281219e1356151fd640336cde885e7d` | 未修改、未新建分支；工作区仍干净 |

尚未推送或创建 PR，不合并未批准的 G1 候选。完整最新 replay 可在平台基线
`c40e3f56a` 查阅；`n4b-complete` 指向 `67c08de43`，不包含其后 PR #83 的 F2 修订。
本批没有移动 tag、删除历史代码或修改里程碑。

## 3. 修改文件与核心接口

| 位置 | 内容 |
|---|---|
| `tools/generation/task_workload_model.py` | 图像/LLM immutable budget、整数 WU/时间、variable-only sigma、合法边界和 checkpoint 字节差分 |
| `tools/generation/preview-n4c-workload.py` | 1500项属性预算、实际合成请求字节、算力对照和全部搜索网格账本 |
| `tests/unit/test_n4c_workload_model.py` | 公式、参考值、非法值、整数溢出、GQA/dtype、进度/字节守恒 |
| `tests/unit/test_n4c_workload_preview.py` | 精确数量/预算、稳定性、全部任务三粒度守恒、CLI防覆盖 |
| generation/tests README、`docs/README.md` | 入口、按需输出和阶段边界 |
| `docs/n4c/plan-and-baseline.md`、`workload-mapping.md` | 对话修订、基线、完整参数和来源合同 |

前三组相对路径均位于 `contrib/satcompute/`。核心消费对象是 `TaskBudget`，不是新的
TaskTrace schema；正式 C++ 类型/deadline/在线风险接口仍待 G2 实现。

## 4. 公式、单位、参数与来源

图像使用 `W=ceil(S*a_z/1000)`，三类候选 `a_z=1`。状态参考来自已完成的三类测量，
按整数分子/分母外推并取整；`sigma_variable=K_variable/W`，H 单独计费。
不修改或误用 TaskModeling 原来的 total-byte sigma。完整定义与来源链接见
[工作量模型](../workload-mapping.md)。

下表的图像为100 MB预算，H 使用原测量标签；LLM为 P=200、G=1300 的示例。
这是公式计算，不是新图像/LLM实测。

| 类型 | INPUT（B） | WU | K_variable（B） | H（B/次） | sigma_variable（B/WU） | RESULT（B） |
|---|---:|---:|---:|---:|---:|---:|
| dense-image | 100,000,000 | 100,000 | 100,000,762 | 65 | 1000.00762 | 100,000,000 |
| sparse-inference | 100,000,000 | 100,000 | 186,906 | 85 | 1.86906 | 186,906 |
| compression | 100,000,000 | 100,000 | 54,248,130 | 65 | 542.4813 | 54,248,130 |
| llm | 768 | 150,000 | 172,032,000 | 0 | 1146.88 | 5,200 |

sparse/compression 的 RESULT 保留规范样本/segment 描述符，本来就可能等于
K_variable；不能据此把 RESULT 和恢复状态在所有任务中混为一谈。实际1500项的图像
标签是十进制 task ID，H 为45--52 B；65/85不是全局常量。

LLM 的公开配置固定到 [Qwen3-0.6B / c1899de](https://huggingface.co/Qwen/Qwen3-0.6B/blob/c1899de289a04d12100db370d81485cdf75e47ca/config.json)：
层数28、KV heads 8、head dim 128；按2 B元素假设得到114,688 B/token。
统一100 WU/token、H_LLM=0、uint32输出 token 列表均是场景假设，不是现实性能或
serializer测量。KV按完整 token 递增，prompt缓存不会重复加入每段。G1不验证真实恢复。

## 5. 命令、环境和原始证据

环境：项目 `.venv` 的 Python 3.10.12、CMake 3.31.10，GNU C++ 11.4.0、系统
Ninja 1.10.1。原有 CMake 缓存启用 satcompute，`NS3_EXAMPLES/NS3_TESTS=OFF`。
本批 `./ns3 build` 触发重新配置及491项构建任务，全部完成；没有启用或运行上游测试。

在平台仓库根目录运行：

```bash
source .venv/bin/activate
PYTHONDONTWRITEBYTECODE=1 python \
  contrib/satcompute/tools/generation/preview-n4c-workload.py \
  --output-dir output/n4c-g1-20260908
PYTHONDONTWRITEBYTECODE=1 python \
  contrib/satcompute/tools/generation/preview-n4c-workload.py \
  --output-dir output/n4c-g1-20260908-repeat
```

两个目录真实存在且由现有 gitignore 排除，`execution.json` 均记录代码提交
`d8eab893a`、`worktree_dirty=false`。复跑请换新目录，工具拒绝覆盖旧输出。
两个目录的五个业务文件逐字节一致，execution 中命令路径等元数据不要求一致：

| 文件 | 大小（B） |
|---|---:|
| `summary.json` | 29,728 |
| `task-budgets.csv` | 297,910 |
| `llm-requests.json` | 95,602 |
| `representative-budgets.csv` | 2,359 |
| `checkpoint-grid.csv` | 48,266 |

开发中间预览保留在 `output/n4c-g1-development-20260908/`，不是本报告冻结证据。
未使用 SHA256 或新增安全校验层。业务 CSV 仅显式调用工具时生成，正常仿真不输出。

## 6. 测试清单

| 检查 | 结果 |
|---|---|
| `./ns3 build`（项目 venv PATH） | 通过 |
| Python unit 全发现 `test_*.py` | 24项通过，其中新 G1 测试15项 |
| `tests/unit/run-cpp-tests.sh` | 13个项目 C++ executable 全部通过 |
| `tests/integration/smoke/run-all.sh` | routing/capacity/task/diagnostics/topology/link 六组通过 |
| 1500个属性样本，各做5/10/20%预算分割 | 4500组检查，WU/可变状态/重复H账本一致 |
| 四类参考任务，1.0%--10.0%步长0.1个百分点，另加20% | 368行网格；无重复位置、无零WU推进，整数/字节守恒 |
| 两次独立CLI预览 | 五个业务文件逐字节一致；拒绝覆盖测试通过 |
| Python AST、文档本地链接、`git diff --check` | 通过 |
| N4C 1500任务网络/无故障基线与故障多seed标定 | 未运行，分别属于 G2/G3 |
| 全 regression/N4B联合重验、GitHub CI | 本批未运行；保留为后续集成与阶段收口检查 |

代码纯函数用整数/有理数作为判定依据，分位数只用于展示。旧生成器的全部确定性
fixture检查仍通过；没有通过修改旧期望值获得通过，也没有关闭旧故障采样。

## 7. 关键结果与局限

默认候选为20,000 WU/s、100 WU/token：

| 类型 | 数量 | 总 WU | 服务时间 min / median / p95 / max（s） | 小于1 s数量 |
|---|---:|---:|---|---:|
| dense-image | 450 | 24,454,489 | 0.4135 / 1.8215 / 3.55445 / 50 | 90 |
| sparse-inference | 450 | 18,075,828 | 0.4135 / 2.0742 / 3.4822 / 3.66275 | 97 |
| compression | 450 | 39,220,237 | 0.4135 / 2.1825 / 25 / 50 | 84 |
| llm | 150 | 21,966,300 | 5 / 7.2525 / 9.68825 / 9.98 | 0 |

- INPUT 精确81,750,000,000 B；大任务为15个1 GB、30个500 MB。
  dense 分4/7个，compression 分11/23个，LLM不承担图像尾部预算。
- LLM INPUT合计86,852 B，单项324--845 B；总缓存1000--1996 token，原始KV预算
  114,688,000--228,917,248 B。150项参考时长全部满足5--10 s。
- 新总 WU 为103,716,854，RESULT为45,764,951,898 B；可变状态预算合计
  70,957,085,210 B。它们不是实际网络传输或内存占用统计，也不需要等于旧压力基线。
- 按任务ID轮转到66个假想计算节点，服务需求合计5185.8427 s；单节点
  min/median/p95/max为42.03865/75.43615/124.2399125/200.3959 s。
  需求除以66×1000 s容量为7.8573%，**不是实测节点利用率或无排队完成保证**。

配对候选均使用同一批 P/G 与输入字节；只统一调整算力及 LLM 系数：

| 候选 | 节点 WU/s | LLM WU/token | 1 GB图像时长 | 小于1 s图像任务 | LLM时长范围 | 总服务需求（s） |
|---|---:|---:|---:|---:|---|---:|
| 较慢算力 | 10,000 | 50 | 100 s | 34/1350 | 5--9.98 s | 9273.3704 |
| 默认预览 | 20,000 | 100 | 50 s | 271/1350 | 5--9.98 s | 5185.8427 |
| 较快算力 | 50,000 | 250 | 20 s | 848/1350 | 5--9.98 s | 2733.32608 |

summary 另保留固定100 WU/token只改速率的对照：10,000 WU/s下LLM为10--19.96 s，
50,000 WU/s下为2--3.992 s，明确标记不满足目标，没有偷偷逐任务调参。

动态风险的可观察性仍需审阅：默认候选271个图像任务短于1 s，普通任务中位数约2 s。
50 MiB稠密/压缩参考任务10%预算间隔约0.26215 s，稀疏参考约0.13125 s，1500-token
LLM约0.75 s；1 s风险更新不等于每个 checkpoint 都得到一次新风险值。不能据此宣称
已经充分验证动态频率。较慢候选增加观察时间，但尾部100 s会明显改变F1热负载。

1.1%名义间隔在100个tile参考任务上首先对齐2%，最大偏移约0.9个百分点；1500-token
LLM最大偏移约0.03333个百分点。图像累计WU向上取整误差小于1 WU；这些误差显式
保留在网格表。稀疏预览使用合成整图边界，不声称复现实际DOTA单图长度。

## 8. 与上次认可方案的差异及证据失效条件

相对原任务书，按对话移除了 TaskModeling 开发和真实 LLM 运行要求；图像实测参数
来源不变。新增的具体候选包括20,000 WU/s、100 WU/token、P/N范围、H_LLM=0、
uint32 RESULT、合成请求和稀疏文件粒度，均在本次 G1 提交确认，不称为此前已批准。

原有压力输入、F1/F2参数及历史报告保持原样。没有通过增减WU、挑选故障seed或删除
异常样本去接近150个失败。若调整本次映射/算力/字节表示，重跑受影响的离线预览；
G2/G3以新参数重新验证，不拿旧网络/故障结果给新输入背书。

## 9. 本次需要确认的候选

1. 三类图像先统一 `a_z=1`，保留参考rho及独立RESULT/H口径，是否接受？
2. 选择20,000 WU/s的默认预览，还是10,000 WU/s的较慢候选，或提出其他档位？
   不能只看LLM目标，还需明确普通图像与1 GB尾部的时长取舍。
3. LLM首轮采用等成本合成token、2 B元素、H=0、uint32 token RESULT，以及本次P/N范围，
   是否作为仿真抽象接受？不将其视为真实恢复实现的证明。
4. 图像合法tile/整图边界、稀疏预览的合成文件粒度与后继边界对齐规则，是否接受？

## 10. 到达的门禁与暂停状态

**已到 G1，等待用户审阅；未进入 N4C-2/3。** 未批准的候选不合入main、不打阶段tag，
不启动1500任务网络基线、地理热点和故障标定。分支保留供本批迭代；只有G1批准后
才进入第二阶段。后续合入/CI/标签/分支整理继续遵守阶段计划。
