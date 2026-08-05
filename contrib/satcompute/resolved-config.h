/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_RESOLVED_CONFIG_H
#define SATCOMPUTE_RESOLVED_CONFIG_H

#include "topology/orbit/constellation-definition.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace ns3
{

struct SatComputeConfig;

/** 平台参数和星座结构无法解析为唯一内部配置时抛出的异常。 */
class ResolvedSatComputeConfigError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** 已转换为整数纳秒的仿真窗口。 */
struct ResolvedSimulationConfig
{
    int64_t startTimeNs; ///< 逻辑仿真开始时刻。
    int64_t durationNs;  ///< 仿真持续时间。
};

/** 已解析的拓扑和链路运行参数。 */
struct ResolvedNetworkConfig
{
    std::string topologySource;                          ///< online 或 replay。
    std::optional<std::filesystem::path> replayDirectory; ///< replay 切片目录。
    std::string islCandidateStrategy;                    ///< 固定候选 ISL 策略。
    bool seamEnabled;                                    ///< seam 候选链路开关。
    long double maxIslDistanceM;                         ///< 有效 ISL 距离门限。
    std::string delayMode;                               ///< fixed 或 distance。
    std::optional<int64_t> fixedDelayNs;                 ///< fixed 单向时延。
    int64_t networkUpdateIntervalNs;                     ///< 在线网络更新时间。
    uint64_t linkBandwidthBps;                           ///< ISL bit/s 数据率。
    uint16_t islMtuBytes;                                ///< ISL MTU。
    uint32_t islQueueBytes;                              ///< ISL 队列字节容量。
    uint32_t receiverRcvBufBytes;                        ///< UDP 接收缓冲区。
};

/** 已解析的 IPv4 路由参数。 */
struct ResolvedRoutingConfig
{
    std::string mode;            ///< 五种受支持路由模式之一。
    uint64_t hashSeed;           ///< 逐流 ECMP 与 HRW seed。
    std::string recomputePolicy; ///< 路由重算策略。
};

/** 已规范化为绝对路径的独立业务输入。 */
struct ResolvedWorkloadConfig
{
    std::optional<std::filesystem::path> transferTrace;  ///< NetworkTransfer JSON。
    std::optional<std::filesystem::path> computeProfile; ///< 算力资源 JSON。
    std::optional<std::filesystem::path> taskTrace;      ///< TaskTrace JSON。
    std::string transferChunkMode;                       ///< fixed 或 size-aware。
    uint32_t transferPayloadBytes;                       ///< fixed payload 上限。
    std::string taskCompletionPolicy;                    ///< strict 或 report。
};

/** 独立于网络 tick 的拓扑状态导出参数。 */
struct ResolvedTraceExportConfig
{
    bool enabled;           ///< 是否输出坐标和拓扑切片。
    int64_t intervalNs;     ///< 导出间隔。
    bool includeFinalState; ///< 是否输出仿真终点状态。
    std::string format;     ///< 当前固定为 json-slices。
};

/** 运行日志和失败诊断参数。 */
struct ResolvedLoggingConfig
{
    std::string transferLogMode; ///< transfer 日志级别。
    std::string taskLogMode;     ///< task 日志级别。
    std::string diagnosticMode;  ///< 失败诊断模式。
};

/** 可复现运行所需的 ns-3 随机数边界。 */
struct ResolvedRandomnessConfig
{
    uint32_t seed;       ///< ns-3 全局 seed。
    uint64_t run;        ///< ns-3 独立 run 编号。
    int64_t streamStart; ///< SatCompute stream 起点。
};

/**
 * SatCompute 唯一的内部运行配置。
 *
 * 该对象由 typed para/CLI 与一个 constellation-only JSON 一次性解析产生；
 * 后续组件只读取本对象，不再自行解释输入参数。
 */
struct ResolvedSatComputeConfig
{
    std::string schemaVersion;                 ///< effective config 合同版本。
    std::string runName;                       ///< 本次运行名称。
    ResolvedSimulationConfig simulation;       ///< 仿真窗口。
    ConstellationDefinition constellation;     ///< 星座物理结构。
    ResolvedNetworkConfig network;             ///< 拓扑和链路参数。
    ResolvedRoutingConfig routing;             ///< IPv4 路由参数。
    ResolvedWorkloadConfig workloads;          ///< 独立业务输入与策略。
    ResolvedTraceExportConfig traceExport;     ///< 状态导出参数。
    ResolvedLoggingConfig logging;             ///< 日志和诊断参数。
    ResolvedRandomnessConfig randomness;       ///< 随机数配置。
    std::filesystem::path outputDirectory;     ///< 结构化输出目录。
};

/**
 * 将 para/CLI 和星座结构一次性解析为内部配置。
 *
 * @param config 已完成命令行覆盖的 typed 平台参数。
 * @return 只含绝对路径和整数纳秒的内部配置。
 * @throws ResolvedSatComputeConfigError 参数、文件或跨字段约束无效时抛出。
 */
ResolvedSatComputeConfig ResolveSatComputeConfig(const SatComputeConfig& config);

} // namespace ns3

#endif // SATCOMPUTE_RESOLVED_CONFIG_H
