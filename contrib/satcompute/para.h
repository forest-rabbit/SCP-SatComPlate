/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_PARA_H
#define SATCOMPUTE_PARA_H

#include <cstdint>
#include <string>

namespace ns3
{

/**
 * SatCompute 的平台运行参数。
 *
 * 人工设置的时间参数统一使用秒。星座 CSV 只描述星座物理结构，运行策略和
 * 业务输入路径均由本结构管理。
 */
struct SatComputeConfig
{
    // simulation
    double simulationDurationSeconds; ///< 仿真持续时间，单位为秒。
    uint32_t randomSeed;               ///< ns-3 全局随机 seed。
    uint64_t randomRun;                ///< ns-3 独立运行编号。

    // topology
    std::string constellationConfig;        ///< 原生 LEO shell CSV 路径。
    double orbitStartOffsetSeconds;         ///< 轨道 epoch 相对偏移，单位为秒。
    double maxIslDistanceMeters;            ///< ISL 最大允许距离，单位为米。
    double networkUpdateIntervalSeconds;    ///< 网络状态应用周期，单位为秒。
    bool topologyOnly;                      ///< 是否只生成轨道和拓扑切片。
    double topologySliceIntervalSeconds;    ///< 拓扑切片间隔，单位为秒。
    bool includeFinalTopologyState;         ///< 是否额外导出仿真终点状态。

    // link
    std::string delayMode;        ///< 时延模式：fixed 或 distance。
    double fixedDelaySeconds;     ///< fixed 模式单向链路时延，单位为秒。
    uint64_t islBandwidthBps;     ///< 每条 ISL 的数据速率，单位为 bit/s。
    uint16_t islMtuBytes;         ///< 每个 ISL 设备的 MTU，单位为字节。
    uint32_t islQueueBytes;       ///< 每个 ISL 队列容量，单位为字节。

    // routing
    std::string routingMode; ///< IPv4 路由模式。
    uint64_t ecmpHashSeed;   ///< 逐流 ECMP 与 HRW 的 hash seed。

    // workload
    std::string computeProfile;       ///< 卫星算力资源 JSON 路径。
    std::string taskTrace;            ///< 任务输入 JSON 路径。
    std::string transferChunkMode;    ///< 任务输入/结果传输的分包策略。
    uint32_t transferPayloadBytes;    ///< fixed 分包的 UDP payload 字节数。
    uint32_t receiverRcvBufBytes;     ///< UDP 接收缓冲区，单位为字节。
    std::string taskCompletionPolicy; ///< 任务完成策略：strict 或 report。

    // fault
    std::string faultMode;  ///< none、generate 或 replay。
    std::string faultTrace; ///< generate 输出或 replay 输入的统一故障轨迹。

    // output
    std::string outputDirectory; ///< 结构化结果输出目录。
    std::string taskLogMode;     ///< 任务日志级别。
    std::string diagnosticMode;  ///< 失败诊断模式。
};

/**
 * 返回平台唯一的一组内置默认参数。
 *
 * @return 尚未经过命令行覆盖和语义校验的默认配置。
 */
SatComputeConfig GetDefaultSatComputeConfig();

} // namespace ns3

#endif // SATCOMPUTE_PARA_H
