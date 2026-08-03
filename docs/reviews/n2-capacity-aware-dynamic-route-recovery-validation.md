# N2 capacity-aware 动态路径恢复验证

日期：2026-08-02

## 范围

本轮没有增加新路由模式。已删除短期引入的
`global-capacity-weighted-hrw`，并直接将现有
`global-capacity-aware-hrw` 从静态完整路径准入迭代为动态完整路径
重准入。本轮只处理传输竞争和自然动态拓扑，不引入计算节点或
传输节点故障。

## 运行时合同

- 拓扑快照应用、ns-3 全局路由重算和 route epoch 推进后，主动检查
  所有活动预留路径。
- 路径仍有效时保持 sticky；不因其他路径更空闲而主动迁移。
- 路径失效时暂停 sender，一次释放旧路径的全部 assignment 和速率预留，
  然后在当前 ECMP 最短路图上重新准入。
- 暂时无路或无剩余容量时保持等待；在下一 route epoch 或其他 flow 完成
  后重试，不中断仿真。
- 在途包到达旧路径中间节点时，可以使用确定性 HRW 过渡转发，但不创建
  局部路径预留。

## 定向场景

`capacity-aware-route-recovery` 使用 5 颗卫星、三条源到目的并行等跳路径。
三条初始 flow 占用全部路径容量；1 秒时最快路径的下游 ISL 关闭，
4 秒恢复。一个 UDP 包在切换时仍处于旧上游链路中，第四条 flow 在
4.1 秒到达并继续等待容量。

## 结果

```text
dynamic replay A: COMPLETE, transfers 4/4, tx/rx/lost 50/50/0
dynamic replay B: COMPLETE, transfers 4/4, tx/rx/lost 50/50/0
route invalidation: transfer 1, epoch 1, nodes 0 and 1
route recovery:     transfer 1, epoch 2, nodes 0 and 1
transition fallback: exactly one in-flight packet at node 1, epoch 1
final state: active=0, assignments=0, reserved_bytes=0
```

两次运行的逐流、转发、预留事件、传输摘要和丢包诊断文件字节级一致。

## 边界

本轮没有增加 ACK/NACK、重传、非最短绕行、节点故障或计算任务迁移。
因此本结果证明控制面不再因 `candidate-invalid` 中止、容量账本一致，
不构成任意断链时序下 UDP 零丢包的普遍保证。
