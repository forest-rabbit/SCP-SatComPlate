# N4C 工作量与恢复状态候选

本页是 G1 待审阅的候选合同，不代表参数已获批准或接入正式 TaskTrace。
实现位于 `contrib/satcompute/tools/generation/task_workload_model.py`，只使用标准库。
TaskModeling 保持不变；平台不导入其代码，不运行图像实验、tokenizer 或 LLM。

## 1. 参数来源与字节表示

三类图像的参考数据固定到 TaskModeling
[`0dbc0c7`](https://github.com/forest-rabbit/SCP-TaskModeling/tree/0dbc0c7b6281219e1356151fd640336cde885e7d)。
代码保留整数分子/分母，不能拿 README 中保留小数位的 rho 当计算真值。

| 类型 | 输入 S（B） | payload（B） | index（B） | RESULT（B） | 固定头结构（B） |
|---|---:|---:|---:|---:|---:|
| dense-image | 52,428,800 | 52,428,800 | 400 | 52,428,800 | 44 |
| sparse-inference | 26,246,291 | 48,256 | 800 | 49,056 | 48 |
| compression | 52,428,800 | 28,440,844 | 800 | 28,441,644 | 44 |

- 稠密和压缩的 S 为四波段 uint16 原始数组；稀疏推理的 S 为编码图像文件字节。
- `rho_variable=(payload+index)/S`。dense 的 RESULT 是原始输出数组；sparse 的
  RESULT 是样本描述符与 OBB 记录；compression 的 RESULT 是 segment descriptor
  与 JP2 payload。后两类 RESULT 恰好等于 K_variable，不意味着这两个概念可通用替换。
- 图像 `H=固定头结构+task_label 的 UTF-8 长度`，沿用已有 serializer 字段长度假设。
  原参考标签下是 65/85/65 B；预览使用十进制 task ID 标签，因此通常为 45--52 B，
  不是每项都固定65或85 B。没有在平台中执行 serializer 或引入校验算法。
- dense 的比例由当前表示决定；另外两类随内容变化。本轮用参考均值做扩展预算，
  不是重新测量1500个任务，也不是跨地域真实分布。原始计量边界见
  [N5 前置基础](../n5-prerequisites.md)。

## 2. 图像的固定工作量与字节预算

```text
W = ceil(input_bytes * a_z / 1000)
K_variable = floor(input_bytes * reference_variable_bytes / reference_input_bytes)
K_payload  = floor(input_bytes * reference_payload_bytes / reference_input_bytes)
K_index    = K_variable - K_payload
RESULT     = floor(input_bytes * reference_output_bytes / reference_input_bytes)
sigma_variable = K_variable / W
```

三类 `a_z` 首轮均为1。10 MB、100 MB、500 MB、1 GB 对应10,000、100,000、
500,000、1,000,000 WU，全部是十进制单位。WU 不依赖 task ID、同类排名或其他任务。
调整 a_z 或节点速率要重新审阅负载；rho、结果大小和压缩率不控制计算强度。

这是输入规模的均值外推，尤其非参考大小的 index 预算不代表真实逐项序列化的索引
长度。若未来更换恢复格式，必须重新确定 metadata/索引合同，不把本预算当现成 blob。
极小输入的输出预算可能为0；纯模型允许该事实，正式 TaskTrace 目前拒绝0 RESULT。
本次1500任务预览均有正 RESULT；第二阶段不得把不可运行预算静默填成1 B。

`sigma_variable_bytes_per_work_unit` 是平台的新可变系数；TaskModeling 的
`sigma_bytes_per_work_unit` 仍表示含重复头的 total-byte 系数，不改名、不混用。
不重复计入 H，也不把 IP/UDP 头计入应用状态。

## 3. LLM：公开配置公式，不是真实模型运行

参考 [Qwen3-0.6B 配置](https://huggingface.co/Qwen/Qwen3-0.6B/blob/c1899de289a04d12100db370d81485cdf75e47ca/config.json)
固定到 `c1899de289a04d12100db370d81485cdf75e47ca`，本阶段只核对公开配置文本。

| 参数 | 首轮值 | 单位/来源 |
|---|---:|---|
| `layers` | 28 | 公开层数 |
| `kv_heads` | 8 | 公开 GQA KV head 数，不是16个 query head |
| `head_dim` | 128 | 每个 head 的维度 |
| `bytes_per_element` | 2 | FP16/BF16 状态表示假设 |
| `max_cached_tokens` | 40960 | 配置上下文长度；候选实际只用1000--2000 |
| `work_units_per_token` | 100 | 仿真参数；正整数，保证每个 token 对应完整 WU |
| `header_bytes` | 0 | 原始 KV 预算的显式简化，不是实测头长度 |

```text
N = prompt_tokens + generation_tokens
每 token KV 字节 = 2 * layers * kv_heads * head_dim * bytes_per_element
                 = 114688 B = 112 KiB
W = N * work_units_per_token
K_variable = N * 114688
sigma_variable = 114688 / work_units_per_token
```

首轮把 prompt 和 decode token 视为等成本、顺序产生缓存的合成计算单元，**不声称
真实 prefill/decode 同样快，也不模拟实际 forward、EOS、采样或张量分页**。
prompt 的初始 KV 只累计一次，后续每次仅增加新完成 token 的 KV。没有额外在
每段重复添加 prompt 缓存，不把 token 的输入字节当作 KV 字节，不定义图像式 rho。

INPUT 是工具实际构造的合成请求 JSON 的 UTF-8 字节，不运行 tokenizer；P/G 只是
合成属性，不宣称是该文本的实测 token 数。RESULT 首轮明确采用 `uint32` 生成 token
ID 序列，因此为 `4 * generation_tokens` B。这是预算表示，不生成或声称存在模型输出。
权重视为预部署资源，不加入任何请求 INPUT。

H_LLM=0 只表示当前不建模非 KV 元数据成本，不能证明真实请求只靠 KV 即可恢复。
本阶段验证公式/账本，不验证真实 checkpoint serializer、推理耗时或恢复输出一致性。

## 4. 纳秒时间与合法 checkpoint 边界

服务时间严格沿用 C++ `ComputeService::CalculateServiceTimeNs`：

```text
service_time_ns = ceil(W * 1000000000 / node_rate_work_units_per_second)
```

模型校验整数类型（不接受 bool）、正数与 uint64/int64 溢出；先以整数/有理数计算，
只在展示分位数和秒时转换为浮点。相同比例缩放 W 和速率可以不改变时间，但若只改
图像速率或 LLM 的 WU/token，就会改变相应负载或 sigma，不能说仅做单位重命名。

图像累计预算采用 `K(w)=floor(K_variable*w/W)`，相邻合法保存点取差；总字节因此
与5/10/20%分批方式无关。LLM 则先算已完成整 token 数，再累计 KV，未完成的 token
不产生可保存状态。零可变字节的区间允许存在，但独立记录仍计入配置的 H。

合法单位：

- dense/compression：524,288 B 的 tile；扩展数组可有不足一块的边缘 tile。
- sparse：真实应用应传入每张编码图的字节数，支持不等长文件。预览仅按参考100张/
  26,246,291 B 推算文件数，再均分成合成文件（整数余数优先分给前面的文件），不能
  把它描述成实测 DOTA 文件边界。
- LLM：已完成的整 token；任务开始为0，全部缓存 token 完成为100%。

名义进度使用千分比整数；10..100、步长1就是1.0%..10.0%、步长0.1个百分点。
**规划**向后继合法边界对齐；未来实际执行必须等到该边界已完成才保存。重复位置合并，
不能无工作推进仍生成新 checkpoint。极小 WU 使多个边界落在同一整数 WU 时，合并到
该组最后的合法位置，保证最终覆盖100%，而不是提前输出全部任务状态。

每条计划记录保留名义进度、实际 extent/WU、增量 WU、累计与增量可变字节、H。
`delta_total_bytes=delta_variable_bytes+H`；这些是预算行，不是 BACKUP_START 或已提交
checkpoint。首轮不创建任何恢复、复制或网络执行事件。

## 5. 离线1500任务预览与全部可调参数

新工具 `preview-n4c-workload.py` 为 G1 检查提供属性预算，不输出 TaskTrace 或星座。
它复用正式生成器现有 FNV 和有界整数分配函数，不复制它的旧类别/WU排名规则。

| 设置 | 默认候选与含义 |
|---|---|
| `--output-dir` | 必填的新目录；已存在则拒绝，保护原始证据 |
| `--seed` | `n4c-g1-66`，稳定属性生成种子，不是 ns-3 seed/run |
| `--reference-rate` | 20000 WU/s；仅用于离线时间计算，不改正式 ComputeProfile |
| `--llm-work-units-per-token` | 100 WU/token；统一系数，不逐任务反算 |
| 类型/预算 | 450/450/450/150；总 INPUT 81,750,000,000 B |
| 大任务 | 15个1 GB、30个500 MB；dense 分4/7个，compression 分11/23个 |
| 普通图像 | FNV整数权重10..100，有界分配1,048,576..300,000,000 B |
| 原始数组对齐 | dense/compression 每项对齐8 B；余数分配给 sparse 编码文件，保持总预算 |
| 合成 LLM | P=128..256，N=P+G=1000..2000；请求文本长度独立生成，不冒充 tokenizer |
| 节点需求预览 | ID 0..65 按任务ID轮转，只计算服务需求，无地理位置/到达/排队/deadline |

后六项是这批工具中的明确候选常量，不是新增全场景配置文件。后续正式生成器在 G2
消费审核结果，不将相同参数再同时存入 para.cc 和另一份全局 config。

输出目录包含：

| 文件 | 用途 |
|---|---|
| `summary.json` | 总字节/WU、分类型时长、各节点需求、算力与 token/WU 敏感性 |
| `task-budgets.csv` | 1500行属性与预算；没有 source/result/arrival/deadline，不可直接运行 |
| `llm-requests.json` | 每个合成请求的独立序列化文本，用来核对 INPUT 字节 |
| `representative-budgets.csv` | 三类图像参考大小及10/100/500/1000 MB、LLM1000/1500/2000 token |
| `checkpoint-grid.csv` | 四类参考任务的全部91个搜索间隔，加20%对照；字节与进度取整检查 |
| `execution.json` | 代码提交、工作区是否有改动、Python、命令和来源身份；不参与业务确定性比较 |

所有 CSV 都只在显式调用该工具时写出，正式平台默认行为完全不变。summary 与业务表
相同 seed/参数逐字节一致；execution 的命令路径等元数据单独比较，不加入安全散列。

```bash
source .venv/bin/activate
PYTHONDONTWRITEBYTECODE=1 python \
  contrib/satcompute/tools/generation/preview-n4c-workload.py \
  --output-dir output/n4c-g1-preview

PYTHONDONTWRITEBYTECODE=1 python -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_n4c_workload*.py' -v
```

`output/` 被现有 gitignore 排除，无新增依赖或环境同步步骤。完整的1500任务网络
基线和故障多seed标定分别属于 G2/G3，本命令不执行这些实验。
