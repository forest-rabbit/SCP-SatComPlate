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

    // --simulationDuration：仿真持续时间，单位为秒，必须是有限正数。
    config.simulationDurationSeconds = 1000.0;

    // --constellationConfig：ns-3.48 LeoOrbitalShell 六列 CSV。
    config.constellationConfig =
        "contrib/satcompute/input/topology/constellations/synthetic-66.csv";

    // --islCandidateStrategy：固定为 plus-grid；卫星邻居身份不随距离变化。
    config.islCandidateStrategy = "plus-grid";

    // --seamEnabled：是否建立首尾轨道面之间的固定候选链路。
    config.seamEnabled = false;

    // --maxIslDistance：候选链路的最大有效距离，单位为米。
    config.maxIslDistanceMeters = 6174589.0;

    // --delayMode：fixed 使用固定时延，distance 按当前卫星距离计算时延。
    config.delayMode = "fixed";

    // --fixedDelay：fixed 模式的单向链路时延，单位为秒。
    config.fixedDelaySeconds = 0.008;

    // --networkUpdateInterval：在线链路状态和时延的更新周期，单位为秒。
    config.networkUpdateIntervalSeconds = 20.0;

    // --islBandwidthBps：每条 ISL 的数据速率，单位为 bit/s。
    config.islBandwidthBps = 2000000000ULL;

    // --islMtuBytes：每个 ISL PointToPointNetDevice 的 MTU，单位为字节。
    config.islMtuBytes = 1500;

    // --islQueueBytes：每个 ISL 队列的总容量，单位为字节。
    config.islQueueBytes = 1500000;

    // --receiverRcvBufBytes：每个任务传输 UDP 接收 socket 的缓冲区，单位为字节。
    config.receiverRcvBufBytes = 131072;

    // --routingMode：支持 global-first、逐流 hash、HRW、size-aware HRW 和
    // capacity-aware HRW；有效链路集合不变时不重算 hop 路由。
    config.routingMode = "global-capacity-aware-hrw";

    // --ecmpHashSeed：逐流 ECMP 和 HRW 的确定性 hash seed。
    config.ecmpHashSeed = 1;

    // --computeProfile：每颗卫星的算力资源 JSON；必须与 taskTrace 同时提供。
    config.computeProfile = "";

    // --taskTrace：任务到达、输入字节、计算量和输出字节 JSON。
    config.taskTrace = "";

    // --transferChunkMode：fixed 使用统一 payload，size-aware 按任务传输大小分档。
    config.transferChunkMode = "fixed";

    // --transferPayloadBytes：fixed 模式 UDP payload 上限，单位为字节。
    config.transferPayloadBytes = 1024;

    // --taskCompletionPolicy：strict 对未完成任务返回非零，report 只报告结果。
    config.taskCompletionPolicy = "strict";

    // --topologyOnly：只推进原生轨道并输出拓扑切片，不创建网络与任务对象。
    config.topologyOnly = false;

    // --topologySliceInterval：topologyOnly 切片间隔，单位为秒。
    config.topologySliceIntervalSeconds = 1.0;

    // --includeFinalTopologyState：是否额外输出仿真终点的拓扑状态。
    config.includeFinalTopologyState = true;

    // --outputDir：结构化结果输出目录；默认写入 /tmp，避免污染工作树。
    config.outputDirectory = "/tmp/satcompute-output";

    // --taskLogMode：summary、verbose 或 silent。
    config.taskLogMode = "summary";

    // --diagnosticMode：off 关闭失败诊断，failure 在未完成时输出诊断文件。
    config.diagnosticMode = "off";

    // --randomSeed 和 --randomRun：固定 ns-3 随机过程以复现实验。
    config.randomSeed = 1;
    config.randomRun = 1;

    return config;
}

} // namespace ns3
