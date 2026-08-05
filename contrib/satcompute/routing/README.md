# IPv4 路由模块

`routing/` 在 ns-3.48 全局最短路表之上提供五种 SatCompute IPv4 选择策略。它不
自行计算最短路：拓扑模块先原子更新链路状态并调用 ns-3 全局路由重算，本模块再
从目的卫星 host route 的等价候选中做逐流下一跳或完整路径选择。

当前任务传输使用 UDP 五元组，所有策略在固定拓扑、任务、hash seed 和同时事件
顺序下均可复现。模块不实现 IPv6 或 SRv6。

## 文件与职责

| 文件 | 职责 |
|---|---|
| `routing-policy-factory.h/.cc` | 根据公开模式创建 next-hop policy 或完整 path policy |
| `common/routing-mode.h/.cc` | 五种模式的字符串与 enum 唯一映射 |
| `common/ecmp-flow-key.h` | IPv4 五元组和 21-byte 大端 hash 编码 |
| `common/ecmp-route-candidate.h` | gateway、接口、目的和掩码组成的 canonical 候选 |
| `common/fnv1a64.h` | 共用的 FNV-1a-64 实现 |
| `algorithm/next-hop-policy.h` | 单节点等价下一跳策略接口 |
| `algorithm/global-first-policy.*` | 标记原生 ns-3 首条路由行为 |
| `algorithm/hash-per-flow-policy.*` | 固定逐流 FNV 取模 |
| `algorithm/hrw-per-flow-policy.*` | HRW 评分、选择和完整候选排名 |
| `algorithm/size-aware-hrw-policy.*` | HRW 前两名中的声明字节负载选择 |
| `algorithm/path-policy.h`、`capacity-aware-*` | ECMP 最短路图中的完整路径准入 |
| `state/flow-route-registry.*` | flow 生命周期、节点级粘滞选择和声明字节 reservation |
| `state/size-aware-load-*` | 节点/下一跳已保留字节账本及峰值 |
| `state/capacity-reservation-state.*` | transfer 完整路径与有向链路速率 reservation |
| `ns3/satcompute-ipv4-global-routing.*` | `Ipv4GlobalRouting` 适配、候选规范化、缓存与 trace |
| `ns3/satcompute-ipv4-global-routing-helper.*` | 通过标准 ns-3 helper 安装协议并推进 route epoch |

逻辑实现通常由同名 `.h/.cc` 组成；只有纯数据结构、接口和短模板保留为头文件。

## 公共候选与 hash 合同

每个目的 host route 的候选先按以下键升序排序并去重：

```text
(gateway, output_interface, destination, destination_mask)
```

flow key 为：

```text
(source_ipv4, destination_ipv4, protocol, source_port, destination_port)
```

hash 输入使用大端编码：8-byte `ecmpHashSeed` 加 13-byte IPv4 五元组，共 21 byte。
FNV-1a-64 可写为：

```text
h[0]   = 14695981039346656037
h[k+1] = ((h[k] XOR byte[k]) * 1099511628211) mod 2^64
```

HRW 再追加候选的 gateway、output interface、destination 和 mask 四个 32-bit 值，
形成 37-byte 输入。分数相同时，使用上面的 canonical 候选顺序打破平局。

## 五种模式

### global-first

`global-first` 不进入 SatCompute 的逐流候选选择，直接委托
`Ipv4GlobalRouting::RouteOutput/RouteInput`。它代表 ns-3 原生全局路由在当前表中的
首条路由行为，不维护 flow reservation。

### global-hash-per-flow

对 canonical 候选集合 `C`，选择：

```text
i = H(seed || five_tuple) mod |C|
c* = C[i]
```

因此同一 route epoch 内，同一五元组始终使用相同下一跳。这就是平台中的“固定
hash 逐流 ECMP”；它不是随机逐包 ECMP。候选集合变化后重新取模，可能使较多 flow
同时迁移。

### global-hrw-per-flow

为每个候选单独计算 Rendezvous/HRW 分数：

```text
score(f, c) = H(seed || five_tuple(f) || canonical_candidate(c))
c* = argmax score(f, c)
```

当候选增加或删除时，通常只有胜者受影响的 flow 需要迁移；恢复完全相同的候选集
会恢复原选择。该模式没有负载账本。

### global-size-aware-hrw

size-aware 先取得 HRW 排名最高的两个候选 `C2`，再查看当前节点上各候选已经
reservation 的声明传输字节：

```text
C2 = top-2 candidates by HRW score
c* = candidate in C2 with minimum reserved_declared_bytes(node, c)
```

如果两个候选负载相等，HRW 第一名获胜。首次选择后以 `(node, flow)` 保存粘滞
assignment；只要该候选仍存在，后续包继续复用，不根据瞬时负载来回切换。

reservation 使用整个 transfer 的声明字节数，而不是已经发送的字节。候选失效时
立即释放并确定性重选；size-aware sender 完成发送时释放该 flow 的全部 assignment。
该模式会改变后续 flow 的选择，但固定任务及同时事件顺序时结果仍是确定的。

### global-capacity-aware-hrw

capacity-aware 在 ns-3 当前 ECMP 最短路图中搜索源到目的的完整路径。对有向边 `e`
定义剩余速率，对完整路径 `p` 定义瓶颈：

```text
residual(e) = link_rate(e) - reserved_rate(e)
bottleneck(p) = min residual(e), for e in p
p* = argmax bottleneck(p)
```

只接纳瓶颈剩余速率大于 0 的路径；相同瓶颈使用逐节点 HRW 排名作为确定性 tie-break。
接纳后，以 `admitted_rate = bottleneck(p*)` 在路径每条有向边上预留速率，并用该
瓶颈速率进行 sender pacing。没有正容量完整路径时，transfer 保持 pending，不建立
部分路径 reservation。

活动边变化后，拓扑先原子完成全局路由更新，再调用 transfer engine。失效的完整
路径会被暂停、释放，并按 `(arrival_time_ns, transfer_id)` 顺序重新准入；transfer
由 receiver 确认完整接收后释放路径 reservation。已经在途
且暂时到达旧路径节点的数据包使用 HRW fallback 转发，但不会创建部分 reservation。

## route epoch、缓存与拓扑变化

逐流决策缓存键为：

```text
(route_epoch, has_five_tuple, five_tuple)
```

route epoch 只在 active 链路集合变化并完成一次全局路由重算后推进一次。distance
模式下仅传播时延变化不会推进 epoch，也不会清空 hop 路由缓存。候选集合恢复后，
无状态 hash/HRW 会按相同输入恢复相同选择；有状态策略还遵守当前活动 transfer 的
reservation 生命周期。

没有 UDP 五元组、没有目的 host route 或处理 multicast 时，适配器保留 ns-3
基础路由 fallback，不为无法识别的流量伪造 reservation。每次实际逐流选择会通过
trace 交给 `metrics/routing/ecmp-route-recorder.*`。

## 对应测试与输出

- `tests/unit/routing-policy-factory-test.cc` 检查模式与策略映射；
- `tests/integration/smoke/run-routing-smoke.sh` 和
  `run-capacity-aware-smoke.sh` 检查在线闭环；
- `tests/integration/regression/run-full-routing-regression.sh` 覆盖五种模式、重复运行
  确定性和 66 星拓扑；
- `metrics/README.md` 说明 `ecmp-route-events.csv`、size-aware reservation 与
  capacity-aware 汇总。
