# 正式任务生成器

本目录只维护当前 **66 星、800 任务、1300 秒** 场景；不再提供历史候选或 F3 victim 搜索。
生成任务不运行网络、故障、图像算法或 LLM，不导入 SCP-TaskModeling 仓库代码。

| 文件 | 职责 |
|---|---|
| `generate-task-workload.py` | 固定任务构成、到达时刻、原生位置驱动的热点放置及生成摘要 |
| `task_workload_model.py` | S/W/K/RESULT、rho/sigma/H、合法应用边界和字节守恒纯函数 |

## 唯一正式场景

原有 240 dense、240 sparse、240 compression、80 LLM 不变。704 个普通图像使用
TN(240,130;50,1000) 十进制 MB；另有固定 ID 的 10×500 MB、5×1 GB。
500 MB 锚点为 compression 8/dense 2，1 GB 为 compression 4/dense 1。
普通图像可自然超过 500 MB，必须按锚点 ID 区分，不能仅按大小分类。

任务到达窗口为 1..1050 s，仿真至 1300 s；全部 66 星各 100000 WU/s。
当前种子下 INPUT=194119753287 B、RESULT=100166291859 B、WU=352513119。
任务 120 是独立标明的 800 MB 受控大小覆盖，仍用共享模型得到 WU/RESULT；不是 TN 抽样结果。
其到达和端点以及其他 799 任务保持不变；无任务 801，不要求预热，也不改变故障模型或随机流。
`CONTROLLED_F3_INPUT_BYTES` 固定已筛选的大小，摘要保留覆盖前字节数及 B 组筛选说明。
当前输入见[场景索引](../../input/experiments/leo-66/README.md)，原 G3 冻结报告保留历史 800 任务结果。

| 输入参数 | 默认 / 含义 |
|---|---|
| `--workload-seed` | `n4c-g1-66`，任务类别、尺寸、合成 token 与到达时刻 |
| `--placement-seed` | `n4c-g3-hotspot`，计算节点抽样及源/结果节点确定性排序 |
| `--nodes-file` | 必填，原生 `nodes_0s.json`，稳定 ID 必须为 0..65 |
| `--position-slices` | 必填，原生 ECEF 节点切片目录，覆盖到达窗口，间隔 1 s |
| `--compute-profile` | 必填，正式场景中的 `compute-profile.json` |
| `--output-task-trace` | 必填，不存在的新文件；不得覆盖正式输入 |
| `--output-workload-summary` | 必填，另一个新文件；生成摘要不是平台完整配置 |

`hotspot_weight=64` 是热点选择权重，不是卫星编号/数量；背景权重为 1。
`regional_candidate_limit=1` 表示北美、欧洲、东亚各最多一个热点候选，按区域归一化
中心距离选取；无候选时保留背景节点。使用到达时刻之前、年龄小于 1 s 的原生位置。
源/结果节点按已有分配计数、放置哈希、节点 ID 排序，与计算节点不同。

两种生成种子与 ns-3 的 `randomSeed=1/randomRun=11` 独立。改变种子会得到不同任务；
正式对照必须使用已提交输入，不重新搜索故障数量。受控 F3 固定读取独立 manifest，
不参与任务生成或在线保护决策。原哈希 key 和锚点排名间隙均保留，不使用 SHA-256。

## 从已有位置切片重新生成

以下命令只运行 Python；`orbit/topology` 是预先导出的原生节点切片目录：

```bash
.venv/bin/python contrib/satcompute/tools/generation/generate-task-workload.py \
  --nodes-file=orbit/topology/nodes_0s.json --position-slices=orbit/topology \
  --compute-profile=contrib/satcompute/input/experiments/leo-66/compute/compute-profile.json \
  --output-task-trace=output/final-generated/task-trace.json \
  --output-workload-summary=output/final-generated/workload-summary.json
```

切片由平台 topology-only 模式统一导出，不用 Python 重新实现轨道。首次需要时可运行：

```bash
./ns3 run "satcompute --topologyOnly=1 --simulationDuration=1051 \
  --constellationConfig=contrib/satcompute/input/experiments/leo-66/topology/constellation.csv \
  --orbitStartOffset=0 --networkUpdateInterval=1 --topologySliceInterval=1 \
  --computeProfile=none --taskTrace=none --faultMode=none --linkMetrics=0 \
  --outputDir=output/leo-66-orbit"
```

然后把生成命令的 `orbit/topology` 替换为 `output/leo-66-orbit/topology`。
N4 收尾只复用已有切片，不重新执行该导出，位置切片不进入正式 input 目录。
新摘要使用职责明确的名称；已提交旧摘要/manifest 是原始来源记录，不为清理改写。
相同输入双次生成以及与正式 TaskTrace 的逐字节比较，见[测试说明](../../tests/README.md)。

## 工作量、状态与结果字节合同

图像参考测量来自 SCP-TaskModeling 提交 `0dbc0c7b6281219e1356151fd640336cde885e7d`：

| profile | 参考 S（B） | payload（B） | index（B） | RESULT（B） | 固定头结构（B） |
|---|---:|---:|---:|---:|---:|
| dense-image | 52428800 | 52428800 | 400 | 52428800 | 44 |
| sparse-inference | 26246291 | 48256 | 800 | 49056 | 48 |
| compression | 52428800 | 28440844 | 800 | 28441644 | 44 |

```text
W = ceil(3*S/2000)
K_variable = floor(S*reference_variable_bytes/reference_input_bytes)
K_payload = floor(S*reference_payload_bytes/reference_input_bytes)
K_index = K_variable-K_payload
RESULT = floor(S*reference_output_bytes/reference_input_bytes)
rho_variable = reference_variable_bytes/reference_input_bytes
sigma_variable = K_variable/W
H = fixed_header_bytes + UTF-8 byte length of task label
```

dense/compression 的 S 是四波段 uint16 原始数组，sparse 是编码图像文件大小。
按参考整数比例外推，不用展示舍入的 rho 计算；不按任务排名、压缩率或故障数量修改 W。
50/100/500/1000 MB 对应参考计算 0.75/1.5/7.5/15 s，无最短时长填充。
这是状态预算，不是每个合成任务真实序列化的测量。极小输入可能产生 0 RESULT，
纯模型允许，但正式 TaskTrace 不接受；不得静默补成 1 B。

K 包含变量索引、不含重复 H，H 也不含 IP/UDP 协议头。TaskModeling 的含头
`sigma_bytes_per_work_unit` 与这里的 `sigma_variable_bytes_per_work_unit` 不混用。
参考标签的 H 为 65/85/65 B，使用十进制任务 ID 时为对应固定头加 ID 字节数。

## LLM 与合法进度

只使用 Qwen3-0.6B 的公开结构参数（配置 revision
`c1899de289a04d12100db370d81485cdf75e47ca`）：28 层、8 KV heads、
head_dim=128、2 B/元素、最大缓存 40960 token。
当前使用 **400 WU/token**，`P=128..256` 不变，`G=N-P`。
保留原 80 个 LLM 的工作量预算后，`N` 按 W/400 确定性平衡取整：先取下整，再按余数降序、
task ID 升序分配剩余 token。全场景总 WU 严格不变（352513119），LLM 总 WU 为 61333200，
总 token 为153333；单任务相对原工作量最多±200 WU，即在100000 WU/s下最多±2 ms。
这不是固定 token 数后将工作量乘四。生成器中旧 `100*(5000..10000)` 只定义已接受的 WU
分布预算，不是当前 token 映射；不能整除400的总预算会显式拒绝，不能静默改变总 WU。
`K=N*2*28*8*128*2=N*114688 B`，sigma=286.72 B/WU，H=0 是预算简化。
当前 LLM 总 KV=17585455104 B，较旧场景降为四分之一；三类图像的输入、WU和状态不变。
INPUT 是小型合成 JSON 请求的真实 UTF-8 字节数；P/G 不是 tokenizer 实测；
RESULT 为 `4*G` B 的 uint32 token ID 预算。权重假定预部署，不加入 INPUT。
不下载模型，不模拟真实 prefill/decode、EOS 或分页，也不声称 KV 足以恢复真实程序。

`legal_unit_ends` 定义当前应用边界：dense/compression 为 524288 B tile（含尾块）；
sparse 按参考 100 文件/26246291 B 推算数量后等分，余数优先给前面文件；
LLM 为完整 token。这些是布局假设，不是实测 DOTA 文件边界。

服务时间为 `ceil(W*1e9/rate)` ns。图像累计状态为 `floor(K*w/W)`，LLM 仅累计
完整 token。预算点先向后继应用边界对齐，再映射 WU；同 WU 合并到最后合法位置。
5/10/20% 划分的增量 WU/变量字节之和必须守恒，重复 H 单独统计。
这些纯函数不执行 checkpoint；G4 使用独立[旁路验证器](../validation/compfrr-shadow/README.md)，
真实保护执行仍属于 N5。
