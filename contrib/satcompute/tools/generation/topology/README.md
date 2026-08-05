# ns-3.48 共享拓扑生成器

`satcompute-topology-generator` 是链接 `libsatcompute` 的普通项目 executable，
直接调用在线平台同一份 `OnlineOrbitConstellation`、固定 plus-grid candidate、
距离门控、fixed/distance 时延和 `CircularOrbitTraceExporter`。本目录不包含
Hypatia、TLE vendor、SGP4 或任何 Python 轨道公式。

生成 1 秒切片、保留 20 秒网络周期语义的示例：

```bash
./ns3 run "satcompute-topology-generator \
  --runName=synthetic-66-distance \
  --constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.json \
  --simulationDuration=100 \
  --topologyExportInterval=1 \
  --networkUpdateInterval=20 \
  --delayMode=distance --fixedDelay=0 \
  --maxIslDistance=6174589 \
  --islBandwidthBps=2000000000 \
  --outputDir=/tmp/synthetic-66-trace"

python3 contrib/satcompute/tools/generation/topology/check_export.py \
  --trace-dir=/tmp/synthetic-66-trace
```

输出目录直接包含 `manifest.json`、`nodes_<time>s.json` 和
`topology_<time>s.json`。manifest 是权威文件清单，可作为后续故障生成的确定性
输入。相同参数下，该工具与平台 `--exportOnly=true` 生成的 trace 必须逐字节
一致。

Python `check_export.py` 只校验 closed-world 字段、稳定 ID、时间、配对文件和
SHA-256，不计算轨道。
