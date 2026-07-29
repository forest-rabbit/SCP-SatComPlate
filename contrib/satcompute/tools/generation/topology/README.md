# SatCompute 卫星拓扑生成

本目录把轨道传播、候选星间链路、动态可用性判断和 SatCompute JSON
导出组织成一条可复现流水线。它只处理卫星节点和星间链路，不生成地面站、
馈电链路、簇、业务流量或自定义路由。

## 目录

```text
generation/topology/
├── common/                 公共配置、原子输出、哈希和 JSON Schema
├── config/                 可提交的小型星座配置
├── orbit/hypatia/          vendored Hypatia 轨道后端
├── dynamic/                plus-grid 候选图、距离门控和动态导出
├── static/                 单时刻静态拓扑导出
├── tests/                  正向、负向和确定性测试
└── check_export.py         独立 PR4 输出检查器
```

Hypatia 在这里仅是轨道后端：它负责生成/读取 TLE，并计算指定时刻的卫星
位置。候选图、动态 ISL、SatCompute JSON、检查器和 C++ 加载均属于外层
拓扑生成流程。

## 环境

仓库用 `.python-version` 固定 Python 3.10.12，用 `pyproject.toml` 固定
uv 0.11.25，并由 `uv.lock` 锁定依赖：

```bash
uv lock --check
uv sync --locked
```

下面的命令均从仓库根目录运行，并使用 `uv run --locked`。

## synthetic-66 配置合同

`config/synthetic-66.json` 描述一个 6 个轨道面、每面 11 星的合成
Walker Star 星座。它不是对真实 Iridium 星座的复刻。

当前候选策略只接受 `plus-grid`：

- 每颗卫星连接同一轨道面内的前后相邻卫星；
- 每颗卫星连接相邻轨道面的相同 slot；
- `seam_enabled=false` 时，不连接最后一个与第一个轨道面；
- `seam_enabled=true` 时，加入该组 seam 候选边。

对默认的 `seam_enabled=false`，边界轨道面卫星的候选总度数为 3，内部
轨道面卫星为 4，共 121 条无向候选边。启用 seam 后，每颗卫星的度数均为
4，共 132 条候选边。这些值由候选图计算并写入 manifest，不作为动态输出
哈希常量硬编码。

解析星座合同并生成确定性 TLE：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.orbit.hypatia.resolve_constellation \
  --config contrib/satcompute/tools/generation/topology/config/synthetic-66.json \
  --output-dir /tmp/satcompute-resolved-66
```

## PR3：动态 ISL 中间结果

动态生成器在 `0..duration_s` 的闭区间内按 `step_s` 传播卫星位置，并用
`max_isl_distance_m` 对固定 plus-grid 候选边做距离门控：

```text
positions(t)
→ plus-grid candidate graph
→ range availability
→ active undirected ISLs E(t)
→ added/removed transitions
```

生成 120 秒、60 秒间隔的中间结果：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.dynamic.generate_dynamic_isls \
  --config contrib/satcompute/tools/generation/topology/config/synthetic-66.json \
  --duration-s 120 \
  --step-s 60 \
  --output-dir /tmp/satcompute-dynamic-isls
```

PR3 目录严格包含：

```text
candidate-isls.json
isl-snapshots.jsonl
manifest.json
```

这是便于审计的中间格式，不能直接交给 SatCompute C++ 读取。

## PR4：canonical SatCompute JSON

### 静态拓扑

静态生成器在指定轨道时刻采样，但为了满足 C++ scanner 合同，始终发布为
一对 `0s` 文件：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.static.generate_static_topology \
  --config contrib/satcompute/tools/generation/topology/config/synthetic-66.json \
  --time-s 0 \
  --delay-mode distance \
  --link-bandwidth-kbps 10000000 \
  --output-dir /tmp/satcompute-static-topology
```

输出严格为：

```text
nodes_0s.json
topology_0s.json
manifest.json
```

### 动态拓扑

动态 exporter 把 PR3 的每个整数秒时刻转换成成对的完整快照。即使节点
集合没有变化，也会为每个时刻重复生成 `nodes_<time>s.json`：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.dynamic.export_satcompute \
  --source-dir /tmp/satcompute-dynamic-isls \
  --delay-mode distance \
  --link-bandwidth-kbps 10000000 \
  --output-dir /tmp/satcompute-dynamic-topology
```

fixed 模式改用固定单向时延，并且必须显式给出该值：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.dynamic.export_satcompute \
  --source-dir /tmp/satcompute-dynamic-isls \
  --delay-mode fixed \
  --fixed-delay-us 8000 \
  --link-bandwidth-kbps 10000000 \
  --output-dir /tmp/satcompute-fixed-topology
```

PR4 数据采用现有 C++ Schema：

- `node_id` 为连续的 `0..N-1`，`node_type` 为 `sat`；
- `node1_id < node2_id`，每条无向 ISL 只出现一次；
- `type` 为 `sat`；
- `delay` 的单位是微秒（µs）；
- `link_bandwidth` 的单位是 Kbps，C++ 读取后乘以 1000 转为 bps。

`fixed` 直接使用 `fixed_delay_us`。`distance` 使用单向传播时延：

```text
delay_us = round(distance_m / 299792458 × 1,000,000)
```

这里不乘 2。两种模式的所有活动 ISL 都使用命令行指定的统一
`link_bandwidth_kbps`。

## 独立检查

生成器会在正式发布前内部运行 checker；也可以独立复查输出：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.check_export \
  --input-dir /tmp/satcompute-dynamic-topology \
  --expected-node-count 66 \
  --source-dynamic-dir /tmp/satcompute-dynamic-isls
```

checker 校验文件配对和时间、稳定节点集合、canonical 链路、类型与单位、
manifest 统计、聚合 SHA-256，以及可选的 PR3 来源哈希、端点和 distance
时延。成功时 stdout 输出紧凑 JSON 摘要，失败时返回非零状态。

## 输出位置

审查、CI 和临时测试统一写入 `/tmp`。正式实验可以显式写到仓库外的持久
目录。生成器拒绝覆盖非空目录，并先写 `<output-dir>.tmp`，只有内部检查
成功后才原子发布。

不要提交大规模生成结果、TLE、manifest、动态快照、缓存或任何 `output/`
目录。仓库只保留小型配置、代码和测试 fixture。

## 验证

运行拓扑工具全部测试：

```bash
uv run --locked python -m unittest discover \
  -s contrib/satcompute/tools/generation/topology/tests \
  -p 'test_*.py' -v
```

运行 Hypatia 位置 smoke：

```bash
uv run --locked python -m \
  contrib.satcompute.tools.generation.topology.orbit.hypatia.smoke_positions \
  --json
```
