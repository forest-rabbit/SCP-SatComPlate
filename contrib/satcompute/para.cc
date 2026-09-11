/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 集中定义 SatCompute 的默认运行参数；所有参数均可由入口中的同名 CLI 覆盖。

#include "para.h"

namespace ns3
{

SatComputeConfig
GetDefaultSatComputeConfig()
{
    SatComputeConfig config;

    // simulation

    // --simulationDuration：仿真持续时间，单位为秒，必须是有限正数。
    config.simulationDurationSeconds = 1300.0;

    // --randomSeed 和 --randomRun：固定 ns-3 随机过程以复现实验。
    config.randomSeed = 1;
    config.randomRun = 11;

    // topology

    // --constellationConfig：ns-3.48 LeoOrbitalShell 六列 CSV。
    config.constellationConfig =
        "contrib/satcompute/input/experiments/leo-66/topology/constellation.csv";

    // --orbitStartOffset：仿真 0 秒对应的轨道 epoch 偏移，单位为秒。
    config.orbitStartOffsetSeconds = 0.0;

    // --maxIslDistance：候选链路的最大有效距离，单位为米。
    config.maxIslDistanceMeters = 6'171'353.0;

    // --networkUpdateInterval：在线链路状态和时延的更新周期，单位为秒。
    config.networkUpdateIntervalSeconds = 20.0;

    // --topologyOnly：只推进原生轨道并输出拓扑切片，不创建网络与任务对象。
    config.topologyOnly = false;

    // --topologySliceInterval：topologyOnly 切片间隔，单位为秒。
    config.topologySliceIntervalSeconds = 1.0;

    // --includeFinalTopologyState：是否额外输出仿真终点的拓扑状态。
    config.includeFinalTopologyState = true;

    // link

    // --delayMode：fixed 使用固定时延，distance 按当前卫星距离计算时延。
    config.delayMode = "fixed";

    // --fixedDelay：fixed 模式的单向链路时延，单位为秒；默认 1 ms。
    config.fixedDelaySeconds = 0.001;

    // --islBandwidthBps：每条 ISL 的数据速率，单位为 bit/s。
    config.islBandwidthBps = 10'000'000'000;

    // --islMtuBytes：size-aware 最大 payload 加 IPv4/UDP 头，单位为字节。
    config.islMtuBytes = 64'028;

    // --islQueueBytes：每个 ISL 队列的总容量，单位为字节。
    config.islQueueBytes = 1'500'000;

    // routing

    // --routingMode：支持 global-first、逐流 hash、HRW、size-aware HRW 和
    // capacity-aware HRW；有效链路集合不变时不重算 hop 路由。
    config.routingMode = "global-capacity-aware-hrw";

    // --ecmpHashSeed：逐流 ECMP 和 HRW 的确定性 hash seed。
    config.ecmpHashSeed = 1;

    // workload

    // --computeProfile：每颗卫星的算力资源 JSON；必须与 taskTrace 同时提供。
    config.computeProfile =
        "contrib/satcompute/input/experiments/leo-66/compute/compute-profile.json";

    // --computeDeadlineFactor: compute-stage deadline budget / reference service time.
    config.computeDeadlineFactor = 1.3;

    // --taskTrace：任务到达、输入字节、计算量和输出字节 JSON。
    config.taskTrace =
        "contrib/satcompute/input/experiments/leo-66/workload/task-trace.json";

    // --transferChunkMode：默认按传输大小选择 1024、8192 或 64000-byte payload。
    config.transferChunkMode = "size-aware";

    // --transferPayloadBytes：fixed 模式 UDP payload 上限，单位为字节。
    config.transferPayloadBytes = 1'024;

    // --receiverRcvBufBytes：每个任务传输 UDP 接收 socket 的缓冲区，单位为字节。
    config.receiverRcvBufBytes = 131'072;

    // --taskCompletionPolicy：strict 对未完成任务返回非零，report 只报告结果。
    config.taskCompletionPolicy = "report";

    // fault

    // --faultMode: none disables faults; generate samples and executes online.
    config.faultMode = "generate";

    // --faultTrace: generated output; empty uses outputDir/fault-trace.json.
    config.faultTrace = "";
    // 验收专用冻结事件输入；正常 none/generate 运行必须留空。
    config.validationFaultTrace = "";

    // --faultProbabilityAudit：按需采集概率记录；正常运行默认关闭。
    config.faultProbabilityAudit = false;

    // protection

    // --protectionMode：默认 off 保持 N4 行为；fixed 启用真实检查点与一次故障恢复。
    // recompute 不做常态保护，故障后通过 FFP 选择可行节点、原始 INPUT 重传、从零重算。
    // one-plus-one 在首次 TASK_RUNNING 一次申请真实完整副本；无可行资源则不重试。
    config.protectionMode = "off";

    // --placementMode：fa-ffp 保留历史行为；ffp/lrl 是最小筛选，fa-lrl 是可行性筛选加负载排序。
    config.placementMode = "fa-ffp";
    // --remoteBusyRecoveryPolicy：仅 remote 计算忙但检查点可读时生效。
    // relocate 迁移状态后继续；recompute 放弃检查点，从原始 INPUT 重算；off 忽略本参数。
    config.remoteBusyRecoveryPolicy = "relocate";
    // --inputStagingPolicy：eager 保持原始 INPUT 常态预置；deferred 仅用于 CompFRR，
    // 常态只传递状态，故障后向实际恢复星获取一次完整 INPUT，不改变 WU、状态量和成本档位。
    config.inputStagingPolicy = "eager";

    // --lrlRecoveryWeight：L = active backup assignments + weight * active recoveries。
    // G3 在看到 A/B/C 结果之前预先冻结为 1，不扫描、不按结果调整。
    config.lrlRecoveryWeight = 1;

    // --backupStorageBytesPerNode：独立额外备份池，10 GB 为实验参数而非实测容量。
    config.backupStorageBytesPerNode = 10'000'000'000;

    // --fixedProtectionDelta：进度比例，5% 只是执行验证配置，不代表算法最优值。
    config.fixedProtectionDelta = 0.05;

    // --fixedProtectionBatchN：每 4 个有效 L1 组成一个 remote batch。
    config.fixedProtectionBatchN = 4;

    // --compfrr-shadow：显式开启 G4 旁路评估，不影响任务、路由与故障抽样。
    config.compfrrShadow = false;

    // --compfrr-shadow-output：旁路 CSV 目录；空时使用 outputDir/shadow。
    config.compfrrShadowOutput = "";

    // output

    // --outputDir：结构化结果输出目录；默认写入 /tmp，避免污染工作树。
    config.outputDirectory = "/tmp/satcompute-output";

    // --taskLogMode：summary、verbose 或 silent。
    config.taskLogMode = "summary";

    // --diagnosticMode：off 关闭失败诊断，failure 在未完成时输出诊断文件。
    config.diagnosticMode = "off";

    // --linkMetrics：按需输出实际链路占用、队列和带宽预留，不改变业务行为。
    config.linkMetrics = true;

    // --linkMetricsInterval：统计窗口长度，单位为秒，与 networkUpdateInterval 独立。
    config.linkMetricsIntervalSeconds = 1.0;

    return config;
}

} // namespace ns3
