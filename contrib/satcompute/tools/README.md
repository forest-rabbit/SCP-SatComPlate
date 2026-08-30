# 辅助工具

`tools/` 只放不属于 ns-3 仿真核心、但会被输入准备或测试流程直接调用的脚本。
当前没有独立的场景生成器或拓扑生成器：卫星坐标和链路状态必须由
`satcompute --topologyOnly=1` 使用 ns-3.48 的轨道模型生成。

```text
tools/
├── generation/
│   └── generate-task-workload.py      从节点切片和算力配置生成确定性 TaskTrace
└── validation/
    ├── check-flow-drop-reasons.py      检查失败运行的 FlowMonitor 丢包证据
    └── f1-calibration.cc               调用平台纯模型生成 F1 标定证据
```

典型准备流程是：

```text
星座 CSV + para.cc/CLI
          │
          ▼
 topologyOnly 节点/链路切片
          │
          ├──► F2 轨道暴露标定（下一阶段）
          │
          ▼
节点切片 + ComputeProfile ──► TaskTrace ──► 正式网络仿真
```

F1 校准 executable 与 SatCompute 一同构建，不运行网络仿真，也不复制温度/概率
公式。它输出候选时间常数和 30 个固定 ns-3 run 的 Monte Carlo 证据；正式
generate/replay 不读取校准输出。

工具的详细输入、参数和输出分别见 [任务生成器](generation/README.md)和
[验证工具](validation/README.md)。Python 脚本只使用标准库。
