/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_PARA_H
#define SATCOMPUTE_PARA_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ns3
{

class CommandLine;

/** 参数值不满足 SatCompute 平台合同时抛出的异常。 */
class SatComputeConfigError : public std::invalid_argument
{
  public:
    using std::invalid_argument::invalid_argument;
};

/**
 * SatCompute 的平台运行参数。
 *
 * 人工设置的时间参数统一使用秒；解析完成后再转换为 ns-3 Time。
 * 星座 JSON 只描述星座物理结构，运行策略和数据文件路径均由本结构管理。
 */
struct SatComputeConfig
{
    std::string runName;                  ///< 本次运行的稳定名称。
    double simulationStartSeconds;        ///< 仿真开始时刻，单位为秒。
    double simulationDurationSeconds;     ///< 仿真持续时间，单位为秒。

    std::string constellationConfig;      ///< 星座物理结构 JSON 路径。
    std::string topologySource;           ///< 拓扑来源：online 或 replay。
    std::string topologyDirectory;        ///< replay 拓扑切片目录。
    std::string islCandidateStrategy;     ///< 固定候选 ISL 生成策略。
    bool seamEnabled;                     ///< 是否允许跨 seam 候选链路。
    double maxIslDistanceMeters;          ///< ISL 最大允许距离，单位为米。
    std::string delayMode;                ///< 时延模式：fixed 或 distance。
    double fixedDelaySeconds;             ///< fixed 模式单向链路时延，单位为秒。
    double networkUpdateIntervalSeconds;  ///< 在线网络状态更新时间，单位为秒。

    uint64_t islBandwidthBps;     ///< 每条 ISL 的数据速率，单位为 bit/s。
    uint16_t islMtuBytes;         ///< 每个 ISL 设备的 MTU，单位为字节。
    uint32_t islQueueBytes;       ///< 每个 ISL 队列容量，单位为字节。
    uint32_t receiverRcvBufBytes; ///< UDP 接收缓冲区，单位为字节。

    std::string routingMode;            ///< IPv4 路由模式。
    std::string routingRecomputePolicy; ///< 路由重算策略。
    uint64_t ecmpHashSeed;               ///< 逐流 ECMP 与 HRW 的 hash seed。

    std::string transferTrace;         ///< NetworkTransfer 输入 JSON 路径。
    std::string computeProfile;        ///< 卫星算力资源 JSON 路径。
    std::string taskTrace;             ///< 任务输入 JSON 路径。
    std::string transferChunkMode;     ///< 传输分包策略。
    uint32_t transferPayloadBytes;     ///< fixed 分包的 UDP payload 字节数。
    std::string taskCompletionPolicy;  ///< 任务完成策略：strict 或 report。

    bool topologyExportEnabled;           ///< 是否导出坐标和拓扑切片。
    double topologyExportIntervalSeconds; ///< 拓扑导出间隔，单位为秒。
    bool includeFinalTopologyState;       ///< 是否额外导出仿真终点状态。
    std::string outputDirectory;          ///< 结构化结果输出目录。
    std::string transferLogMode;          ///< 传输日志级别。
    std::string taskLogMode;              ///< 任务日志级别。
    std::string diagnosticMode;           ///< 失败诊断模式。

    uint32_t randomSeed;       ///< ns-3 全局随机 seed。
    uint64_t randomRun;        ///< ns-3 独立运行编号。
    int64_t randomStreamStart; ///< SatCompute 预留随机 stream 起点。
};

/**
 * 返回平台唯一的一组内置默认参数。
 *
 * @return 尚未经过命令行覆盖和语义校验的默认配置。
 */
SatComputeConfig GetDefaultSatComputeConfig();

/**
 * 向 ns-3 命令行解析器注册全部 SatCompute 平台参数。
 *
 * @param commandLine 待注册选项的命令行解析器。
 * @param config 接收命令行覆盖值的配置对象。
 */
void AddSatComputeCommandLineOptions(CommandLine& commandLine, SatComputeConfig& config);

/**
 * 校验平台参数之间的基础约束，不读取输入文件。
 *
 * @param config 待校验的平台配置。
 * @throws SatComputeConfigError 参数不满足平台合同时抛出。
 */
void ValidateSatComputeConfig(const SatComputeConfig& config);

/**
 * 将人工输入的秒数转换为整数纳秒。
 *
 * @param seconds 非负、有限的秒数。
 * @param fieldName 用于错误消息的参数名。
 * @return 四舍五入到最近纳秒的整数值。
 * @throws SatComputeConfigError 输入无效或超过 int64 纳秒范围时抛出。
 */
int64_t SatComputeSecondsToNanoseconds(double seconds, std::string_view fieldName);

/**
 * 将十进制秒字符串精确转换为整数纳秒，不经过浮点数。
 *
 * @param token JSON 数字形式或切片文件名中的十进制秒。
 * @param fieldName 用于错误消息的参数名。
 * @param positive 是否要求结果严格大于零。
 * @return 精确的非负整数纳秒。
 * @throws SatComputeConfigError 数字无效、精度小于 1 ns 或溢出时抛出。
 */
int64_t SatComputeDecimalSecondsToNanoseconds(std::string_view token,
                                             std::string_view fieldName,
                                             bool positive = false);

} // namespace ns3

#endif // SATCOMPUTE_PARA_H
