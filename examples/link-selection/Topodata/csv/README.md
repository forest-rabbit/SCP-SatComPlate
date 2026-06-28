# 传统 CSV 拓扑说明

本目录用于保存传统 CSV 拓扑输入。当前默认文件为：

```text
topo(324).csv
```

使用传统 CSV 拓扑时运行：

```bash
./waf --run "link-test --useJsonTopo=false"
```

该模式主要用于兼容旧实验流程。新增拓扑数据优先使用 `Topodata/json/` 下的 JsonTopo 格式。
