# 在线轨道状态与拓扑切片导出

SatCompute 0.3 使用与在线仿真相同的 ns-3.48 圆轨道模型、固定候选 ISL、距离
门控和时延核心，生成确定性的 JSON 切片。输出是只读状态边界，可用于审计、
回放、后续故障模型生成以及未来后端/前端适配。本模块不打开 socket，也不定义
前端传输协议。

三个 closed-world 输出合同分别是：

- `nodes-slice.schema.json`：稳定卫星 ID 和 ECEF `x/y/z` 坐标；
- `topology-slice.schema.json`：当前有效的固定候选 ISL、距离、传播时延和带宽；
- `manifest.schema.json`：运行来源、切片间隔、输入哈希和权威有序文件清单。

人工输入的仿真时间和间隔都以秒写入 `para.cc` 或同名 CLI，平台只在启动时进行
一次精确的有符号 64 位整数纳秒转换。导出序列始终包含起点、严格早于仿真结束
的各个正间隔点；当 `includeFinalTopologyState=true` 时，再包含一个不重复的精确
终点。文件名使用规范十进制秒，例如 `nodes_0s.json`、`nodes_1s.json` 和
`nodes_2.5s.json`。

## 导出间隔与网络更新间隔

`topologyExportInterval` 和 `networkUpdateInterval` 是两个独立输入。每个导出切片
都使用 `state_semantics=orbit-policy-evaluation`：在该切片时刻计算连续轨道位置、
固定 plus-grid 候选、距离门控和链路时延，但不表示仿真网络在该时刻执行了接口
或路由更新。

例如，导出可每 1 秒执行一次，而仿真网络每 20 秒更新一次。1–19 秒文件是供审计
或故障生成使用的高精度策略状态；在线网络仍保持 0 秒 tick 应用的状态，直到
20 秒 tick。0 秒、20 秒等共同时间点必须由同一个轨道对象和拓扑策略给出一致
结果。

## 运行方式

普通 online 仿真可同时输出网络、路由、业务结果和拓扑切片：

```bash
./ns3 run "satcompute \
  --runName=online-trace \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.json \
  --topologySource=online \
  --simulationDuration=2.5 \
  --delayMode=distance \
  --fixedDelay=0 \
  --networkUpdateInterval=2 \
  --topologyExportEnabled=true \
  --topologyExportInterval=1 \
  --outputDir=/tmp/satcompute-online-trace"
```

相同参数也可只生成轨道和拓扑状态，不安装网络设备、路由或业务：

```bash
./ns3 run "satcompute \
  --runName=online-trace \
  --constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.json \
  --topologySource=online \
  --simulationDuration=2.5 \
  --delayMode=distance \
  --fixedDelay=0 \
  --networkUpdateInterval=2 \
  --topologyExportEnabled=true \
  --topologyExportInterval=1 \
  --outputDir=/tmp/satcompute-export-only \
  --exportOnly=true"
```

`--exportOnly=true` 只允许用于启用切片导出的 online 运行，并且不能与
`--validateOnly=true` 同时使用。除输出目录和 `exportOnly` 外参数相同时，普通
运行与 export-only 运行生成的 `topology-trace/` 内容逐字节一致。

消费者必须从 `topology-trace/manifest.json` 开始，验证其中记录的每个 SHA-256，
并且只处理清单列出的文件。如果输出目录残留旧文件，manifest 仍是唯一权威清单；
正式实验应尽量使用新的输出目录。

坐标单位为 ECEF 米，链路距离为米，时延为整数纳秒，带宽为 bit/s。fixed 模式
使用 `fixedDelay`；distance 模式以原始 ECEF 距离除以 299792458 m/s，并按精确
半值向上规则舍入到最近纳秒，与在线网络控制器完全一致。
