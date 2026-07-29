# SatCompute 统一场景生成器

本目录把已经冻结的拓扑后端和计算资源部署组织成一次可复现的场景生成。
它只生成卫星节点、星间链路和静态计算能力，不生成任务、业务流量、故障、
checkpoint、备份或恢复事件。

## 配置分层与保留边界

仓库中有两类配置，它们处于不同接口层，并不是两套拓扑算法：

- `topology/config/synthetic-66.json` 是 PR1–PR4 拓扑后端的独立输入和回归
  fixture。轨道解析、静态生成器、动态生成器以及拓扑单元测试都可以直接使用
  它，因此当前不能删除。
- `scenario/config/synthetic-66-compute-22.json` 是上层完整场景输入。它在星座
  和 ISL 参数之外增加快照调度、时延、带宽与计算节点部署。

统一生成器会把 scenario 中属于拓扑后端的字段规范化为一个临时配置，调用
既有拓扑代码，随后删除临时配置。正式输出中不会出现第三份配置，也没有复制
轨道传播、候选 ISL 或距离门控算法。

两份已提交样例中相同的 66 星参数是有意保留的接口边界。后续可以审计是否只
保留一个上层场景样例，但不能因此删除底层配置解析能力或底层测试 fixture。

JSON 标准不支持注释。为保证配置仍可被严格解析，字段说明集中记录在本文，
不要在 JSON 文件中加入 `//` 或 `/* ... */`。

## 快速使用

从仓库根目录生成默认的 66 星、22 个计算节点、0–1000 秒动态场景：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.scenario.generate_scenario \
  --config contrib/satcompute/tools/generation/scenario/config/synthetic-66-compute-22.json \
  --output-dir /tmp/satcompute-scenario
```

生成器在正式发布前已运行拓扑 checker 和场景 checker。也可以独立复查：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.scenario.check_scenario \
  --input-dir /tmp/satcompute-scenario
```

成功时 stdout 是一行紧凑 JSON；失败时 stderr 以 `ERROR:` 开头并返回非零
状态。

## 配置合同

所有对象均为闭世界：缺少字段或加入未知字段都会失败。`bool` 不能代替整数，
数值必须满足下述范围。当前 schema 固定为 `0.1`。

### 根字段

| 字段 | 含义 | 约束 |
| --- | --- | --- |
| `schema_version` | 场景配置版本 | 必须为字符串 `"0.1"` |
| `scenario_name` | 场景稳定名称 | 只能使用字母、数字、点、下划线和连字符，且首字符为字母或数字 |
| `constellation` | 星座物理参数 | 见下表 |
| `topology` | ISL、快照和链路参数 | 见下表 |
| `compute` | 计算节点与基础服务率 | 见下表 |

### `constellation`

| 字段 | 含义 | 单位与可选值 |
| --- | --- | --- |
| `constellation_name` | 星座稳定名称 | 安全 token；写入 provenance |
| `constellation_pattern` | Walker 轨道面分布 | `walker-star` 使用 180° RAAN span；`walker-delta` 使用 360° |
| `num_orbits` | 轨道面数量 `P` | 正整数 |
| `satellites_per_orbit` | 每轨道面 slot 数 `S` | 正整数；总星数为 `P × S`，且不超过 99999 |
| `altitude_km` | 近圆轨道高度 | km；有限正数 |
| `inclination_deg` | 轨道倾角 | 度；`0 <= value < 180` |
| `phase_diff` | 相邻奇数轨道面是否偏移半个 slot | `true` 为 alternating-half-slot，`false` 为 aligned |

Walker Star/Delta 描述的是 RAAN 覆盖范围，不是圆轨道与椭圆轨道的区别。
当前 Hypatia 适配统一使用偏心率 `0.0000001` 的近圆轨道，以避免
PyEphem 无法传播严格零偏心率。

### `topology`

| 字段 | 含义 | 单位与可选值 |
| --- | --- | --- |
| `mode` | 快照生成模式 | `static` 或 `dynamic` |
| `schedule` | 采样时刻 | 结构由 `mode` 决定，见下文 |
| `isl_candidate_strategy` | 候选 ISL 图 | 当前只允许 `plus-grid` |
| `seam_enabled` | 是否连接最后与第一个轨道面 | 布尔值；不影响同轨前后邻居 |
| `max_isl_distance_m` | 候选 ISL 最大可用距离 | m；正整数，超出后该时刻链路不活动 |
| `delay_mode` | 链路单向传播时延 | `fixed` 或 `distance` |
| `fixed_delay_us` | fixed 模式的统一时延 | µs；非负整数；distance 模式必须为 `null` |
| `link_bandwidth_kbps` | 每条活动 ISL 的数据率 | Kbps；正整数；C++ 读取后乘 1000 转为 bps |

`plus-grid` 为每颗卫星创建同轨前后邻居和相邻轨道面的同 slot 候选边，再由
`max_isl_distance_m` 在每个采样时刻判断是否活动。它不是路由算法；运行期
路由仍由 SatCompute 的 ns-3 路由层处理。

`distance` 使用单向传播时延：

```text
delay_us = round(distance_m / 299792458 × 1,000,000)
```

默认的 `link_bandwidth_kbps=2000000` 表示
`2,000,000 Kbps = 2,000,000,000 bps = 2 Gbps`，不是 2 Mbps，也不是
2,000,000 bps。

#### static schedule

```json
{
  "snapshot_time_s": 17
}
```

`snapshot_time_s` 是从固定 epoch 起算的轨道采样秒数，必须为非负整数。
C++ 静态输入仍发布为 `nodes_0s.json` 和 `topology_0s.json`；真实采样时刻
记录在 topology manifest 的 `duration_s`。

#### dynamic schedule

```json
{
  "start_time_s": 0,
  "duration_s": 1000,
  "step_s": 1
}
```

第一版固定 `start_time_s=0`。`duration_s` 为非负整数，`step_s` 为正整数，
且前者必须能被后者整除。采样区间两端都包含，因此快照数为
`duration_s / step_s + 1`；默认配置会生成 1001 对完整快照。

### `compute`

| 字段 | 含义 | 约束 |
| --- | --- | --- |
| `compute_node_count` | 具有计算能力的卫星数 `K` | `1 <= K <= P × S` |
| `placement_strategy` | 计算节点部署规则 | 当前只允许 `even-plane-slot` |
| `compute_rate_work_units_per_second` | 每个选中节点的基础计算服务率 | work units/s；正整数 |

`even-plane-slot` 先用累计取整差把 `K` 均衡分配到各轨道面：

```text
count[p] = floor((p + 1) × K / P) - floor(p × K / P)
```

再在每个轨道面内选择分层中点：

```text
slot[j] = floor((j + 0.5) × S / count[p])
node_id = p × S + slot[j]
```

66 星、22 个计算节点的每轨数量固定为 `[3,4,4,3,4,4]`，节点 ID 为：

```text
1, 5, 9, 12, 15, 17, 20, 23, 26, 28, 31,
34, 38, 42, 45, 48, 50, 53, 56, 59, 61, 64
```

默认单节点服务率为 `1,500,000 work units/s`，因此静态聚合基础服务率为
`22 × 1,500,000 = 33,000,000 work units/s`。运行期队列、繁忙状态和剩余
算力不写入该 profile，由仿真状态决定。

## 输出合同

```text
scenario-output/
├── topology/
│   ├── nodes_<time>s.json
│   ├── topology_<time>s.json
│   └── manifest.json
├── resources/
│   └── compute-profile.json
└── scenario-manifest.json
```

`topology/` 完全沿用现有 C++ 输入 schema。`compute-profile.json` 只包含选中
节点，按 `node_id` 严格递增。`scenario-manifest.json` 嵌入规范化后的场景
配置，并记录拓扑、资源、版本、数量、单位和以下四项的非递归聚合哈希：

- topology manifest SHA-256；
- topology 数据聚合 SHA-256；
- compute profile SHA-256；
- scenario config SHA-256。

checker 会重算所有哈希、部署结果、算力总和、节点归属、快照统计和 provenance，
并拒绝多余文件、符号链接或非规范 JSON 合同。

## 原子输出与目录选择

生成器先写 `<output-dir>.tmp`，所有后端生成和 checker 均成功后才原子发布。
它拒绝覆盖非空正式目录；失败时删除临时目录，不留下部分场景。

审查、CI 和一次性测试应写入 `/tmp`。正式实验应写入仓库外的持久目录，例如：

```text
/home/emsky/experiments/SatCompute/n2/scenario-001
```

不要提交生成的场景、拓扑快照、manifest、缓存或任何 `output/` 目录。

## 验证

运行场景层测试：

```bash
uv run --locked python -m unittest discover \
  -s contrib/satcompute/tools/generation/scenario/tests \
  -p 'test_*.py' -v
```

底层拓扑测试仍需独立保留：

```bash
uv run --locked python -m unittest discover \
  -s contrib/satcompute/tools/generation/topology/tests \
  -p 'test_*.py' -v
```
