/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "para.h"

namespace ns3
{

SatComputeConfig
GetDefaultSatComputeConfig()
{
    SatComputeConfig config;

    // 运行窗口：所有人工配置都使用秒，解析后再统一转换为 ns-3 Time。
    config.runName = "synthetic-66-fixed";
    config.simulationStartSeconds = 0.0;
    config.simulationDurationSeconds = 1000.0;

    // 拓扑来源：默认由 ns-3.48 原生圆轨道模型在线计算。
    config.constellationConfig =
        "contrib/satcompute/input/topology/constellations/synthetic-66.json";
    config.topologySource = "online";
    config.topologyDirectory = "";

    // 候选卫星身份固定；距离门限只控制候选链路是否有效。
    config.islCandidateStrategy = "plus-grid";
    config.seamEnabled = false;
    config.maxIslDistanceMeters = 6174589.0;

    // 默认实验使用固定时延和 20 秒网络更新；distance 实验由 CLI 覆盖。
    config.delayMode = "fixed";
    config.fixedDelaySeconds = 0.008;
    config.networkUpdateIntervalSeconds = 20.0;

    // ISL 与接收端资源沿用 ns-3.33 平台的默认合同。
    config.islBandwidthBps = 2000000000ULL;
    config.islMtuBytes = 1500;
    config.islQueueBytes = 1500000;
    config.receiverRcvBufBytes = 131072;

    // 路由只在有效链路集合变化时重算；距离时延变化不触发 hop 路由重算。
    config.routingMode = "global-capacity-aware-hrw";
    config.routingRecomputePolicy = "on-topology-change";
    config.ecmpHashSeed = 1;

    // 空 workload 路径表示纯拓扑运行；数据 JSON 与平台参数保持分离。
    config.transferTrace = "";
    config.computeProfile = "";
    config.taskTrace = "";
    config.transferChunkMode = "fixed";
    config.transferPayloadBytes = 1024;
    config.taskCompletionPolicy = "strict";

    // 拓扑切片精度独立于网络更新频率，输出默认写到工作树之外。
    config.topologyExportEnabled = true;
    config.topologyExportIntervalSeconds = 1.0;
    config.includeFinalTopologyState = true;
    config.outputDirectory = "/tmp/satcompute-output";
    config.transferLogMode = "summary";
    config.taskLogMode = "summary";
    config.diagnosticMode = "off";

    // 固定 seed、run 和 stream 起点可复现相同的随机过程。
    config.randomSeed = 1;
    config.randomRun = 1;
    config.randomStreamStart = 0;

    return config;
}

} // namespace ns3
