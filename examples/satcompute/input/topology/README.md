# 拓扑输入

SatCompute 只接受分离式 JSON 卫星全量快照：

```text
json/examples/xw-66sat/
```

每个时间片由同名时间的 `nodes_<time>s.json` 和 `topology_<time>s.json`
组成。CSV 建图、增量 patch、地面站和 cluster 格式均不属于项目输入契约。
字段和命名规则见 [`json/README.md`](json/README.md)。
