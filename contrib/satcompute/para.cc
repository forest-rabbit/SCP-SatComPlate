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
    config.simulationDurationSeconds = 1000.0;

    // --randomSeed 和 --randomRun：固定 ns-3 随机过程以复现实验。
    config.randomSeed = 1;
    config.randomRun = 1;

    // topology

    // --constellationConfig：ns-3.48 LeoOrbitalShell 六列 CSV。
    config.constellationConfig =
        "contrib/satcompute/input/topology/constellations/synthetic-66.csv";

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

    // --fixedDelay：fixed 模式的单向链路时延，单位为秒。
    config.fixedDelaySeconds = 0.008;

    // --islBandwidthBps：每条 ISL 的数据速率，单位为 bit/s。
    config.islBandwidthBps = 2'000'000'000;

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
    config.computeProfile = "";

    // --taskTrace：任务到达、输入字节、计算量和输出字节 JSON。
    config.taskTrace = "";

    // --transferChunkMode：默认按传输大小选择 1024、8192 或 64000-byte payload。
    config.transferChunkMode = "size-aware";

    // --transferPayloadBytes：fixed 模式 UDP payload 上限，单位为字节。
    config.transferPayloadBytes = 1'024;

    // --receiverRcvBufBytes：每个任务传输 UDP 接收 socket 的缓冲区，单位为字节。
    config.receiverRcvBufBytes = 131'072;

    // --taskCompletionPolicy：strict 对未完成任务返回非零，report 只报告结果。
    config.taskCompletionPolicy = "strict";

    // output

    // --outputDir：结构化结果输出目录；默认写入 /tmp，避免污染工作树。
    config.outputDirectory = "/tmp/satcompute-output";

    // --taskLogMode：summary、verbose 或 silent。
    config.taskLogMode = "summary";

    // --diagnosticMode：off 关闭失败诊断，failure 在未完成时输出诊断文件。
    config.diagnosticMode = "off";

    return config;
}

} // namespace ns3
