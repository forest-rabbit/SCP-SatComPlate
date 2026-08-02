# N2 capacity-weighted HRW 定向验证

## 结论

新增 opt-in `global-capacity-weighted-hrw`，保留
`global-size-aware-hrw` 和严格 `global-capacity-aware-hrw` 作为独立对照。
本轮只完成小型动态竞争回归，不运行 66/351/720 星压力实验。

两次 capacity-weighted 重放均完成 4/4 transfers、3,200,000/3,200,000
application bytes 和 50/50 UDP packets；FlowMonitor loss、全部显式
DropReason、ISL queue drop、QueueDisc drop、UDP socket drop 及未归因 loss
均为 0。两次运行的确定性指标文件逐字节一致。

## 路由合同

该模式仍只使用 ns-3 `Ipv4GlobalRouting` 生成的等价最短下一跳，不增加更长
路径。每个节点首次处理活动 flow 时按 HRW 排序，只在前两名间比较：

```text
(candidate_reserved_bytes + current_flow_declared_bytes)
/ candidate_output_data_rate_bps
```

比较采用无乘法溢出、无浮点舍入的精确非负分数比较。带宽来自当前快照写入
`PointToPointNetDevice::DataRate` 的 bps 值。候选有效时保持 node+flow
sticky；快照更新后由 ns-3 重算路由，旧候选在下一次查路时若已消失，则先释放
预留，再从新候选重选。发送端继续使用 `first-hop-serialization`，不执行整
路径准入、容量锁定或等待队列。

## 动态 fixture

`capacity-weighted-path-failure` 包含 5 颗卫星。0 到 4 初始有三条两跳最短路：

```text
0--1--4   入口 1 Mbps，下游 10 Mbps
0--2--4   500 Kbps
0--3--4   500 Kbps
```

另有 1--2 的 10 Mbps 绕行 ISL。1 秒快照只关闭 1--4，因此源节点的 ECMP
候选由 3 个变为 2 个；已经进入 0--1 的包到达节点 1 后可以经 1--2--4
继续转发。4 秒恢复 1--4，源节点候选恢复为 3 个。

四条 flow 中前三条在 0.1 秒同时到达，第四条在 4.1 秒到达；大小分别为
1,280,000、640,000、640,000、640,000 bytes，固定 64,000-byte payload，
合计 50 包。定向检查结果如下：

- raw-byte size-aware 基线让 flow 3 保持较慢链路上的 HRW 第一名；
  capacity-weighted 将其选到有预留但归一化负载更低的高速 HRW 第二名；
- 1 秒更新后，源节点的 flow 1 和 3 均释放失效候选并在剩余两个候选中重选；
- 节点 1 对已经到达的 flow 1 同样释放 1--4 assignment，并改走 1--2；
- 4 秒恢复后，flow 1/2/3 保持各自仍有效的 sticky 选择，不主动迁回；
- 4.1 秒的新 flow 4 使用恢复后的高速路径；
- 结束时 active flow、assignment 和 reserved bytes 均为 0。

## 边界

本 fixture 验证的是动态路径失效和绕行，不等于“整颗中间卫星硬失效且零
丢包”。若快照同时关闭故障卫星的全部入边，更新瞬间已经在这些 ISL 上传输的
UDP 包可能因 `INTERFACE_DOWN` 丢失；后续包可以由重算的 ECMP 绕开，但当前
传输层没有重传。若故障节点本身是任务源或目的计算节点，还需要任务恢复与
备份语义，不能仅靠 ECMP 解决。

该模式也不读取真实 queue backlog，不对完整路径做容量准入，不保证任意负载
下零丢包。本轮结果只建立功能合同；正式压力效果必须由后续独立实验判断。

## 本地验证

```bash
./waf build
contrib/satcompute/tests/integration/smoke/run-capacity-aware-smoke.sh
```

新检查器为
`contrib/satcompute/tools/validation/check-capacity-weighted-output.py`，验证
异构带宽选择、3→2→3 候选变化、源端和中间节点重选、恢复后 sticky、完整
传输、零丢包及重复运行确定性。
