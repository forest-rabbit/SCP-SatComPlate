# 辅助工具

`tools/` 只放不属于 ns-3 仿真核心、但会被输入准备或测试流程直接调用的脚本。
当前没有独立的场景生成器或拓扑生成器：卫星坐标和链路状态必须由
`satcompute --topologyOnly=1` 使用 ns-3.48 的轨道模型生成。

```text
tools/
├── generation/
│   └── generate-task-workload.py      从节点切片和算力配置生成确定性 TaskTrace
└── validation/
    └── check-flow-drop-reasons.py      检查失败运行的 FlowMonitor 丢包证据
```

典型准备流程是：

```text
星座 CSV + para.cc/CLI
          │
          ▼
 topologyOnly 节点/链路切片
          │
          ├──► 未来的故障生成器
          │
          ▼
节点切片 + ComputeProfile ──► TaskTrace ──► 正式网络仿真
```

工具的详细输入、参数和输出分别见 [任务生成器](generation/README.md)和
[失败输出检查](validation/README.md)。脚本只使用 Python 标准库。
