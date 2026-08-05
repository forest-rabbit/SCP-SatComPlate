/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "para.h"

#include "ns3/command-line.h"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>

namespace ns3
{

namespace
{

[[noreturn]] void
FailConfig(std::string_view fieldName, std::string_view message)
{
    throw SatComputeConfigError(std::string(fieldName) + " " + std::string(message));
}

void
RequireNotEmpty(const std::string& value, std::string_view fieldName)
{
    if (value.empty())
    {
        FailConfig(fieldName, "must not be empty");
    }
}

void
RequireChoice(const std::string& value,
              std::string_view fieldName,
              std::initializer_list<std::string_view> choices)
{
    for (const std::string_view choice : choices)
    {
        if (value == choice)
        {
            return;
        }
    }
    FailConfig(fieldName, "has an unsupported value: " + value);
}

void
RequirePositiveSeconds(double value, std::string_view fieldName)
{
    if (SatComputeSecondsToNanoseconds(value, fieldName) <= 0)
    {
        FailConfig(fieldName, "must be greater than zero");
    }
}

int
ParseExponent(std::string_view token, std::size_t& position, std::string_view fieldName)
{
    if (position == token.size() || (token[position] != 'e' && token[position] != 'E'))
    {
        return 0;
    }
    ++position;
    bool negative = false;
    if (position < token.size() && (token[position] == '+' || token[position] == '-'))
    {
        negative = token[position] == '-';
        ++position;
    }
    if (position == token.size() || token[position] < '0' || token[position] > '9')
    {
        FailConfig(fieldName, "contains an invalid exponent");
    }
    int exponent = 0;
    while (position < token.size() && token[position] >= '0' && token[position] <= '9')
    {
        if (exponent > 100000)
        {
            FailConfig(fieldName, "contains an exponent outside the supported range");
        }
        exponent = exponent * 10 + (token[position] - '0');
        ++position;
    }
    return negative ? -exponent : exponent;
}

} // namespace

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
        "contrib/satcompute/input/topology/constellations/synthetic-66.csv";
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

void
AddSatComputeCommandLineOptions(CommandLine& commandLine, SatComputeConfig& config)
{
    commandLine.AddValue("runName", "Stable name recorded for this run", config.runName);
    commandLine.AddValue("simulationStart",
                         "Simulation start time in seconds",
                         config.simulationStartSeconds);
    commandLine.AddValue("simulationDuration",
                         "Simulation duration in seconds",
                         config.simulationDurationSeconds);

    commandLine.AddValue("constellationConfig",
                         "Path to the native LEO shell CSV",
                         config.constellationConfig);
    commandLine.AddValue("topologySource",
                         "Topology source: online or replay",
                         config.topologySource);
    commandLine.AddValue("topologyDir",
                         "Topology slice directory for replay",
                         config.topologyDirectory);
    commandLine.AddValue("islCandidateStrategy",
                         "Fixed candidate ISL strategy",
                         config.islCandidateStrategy);
    commandLine.AddValue("seamEnabled", "Enable seam candidate links", config.seamEnabled);
    commandLine.AddValue("maxIslDistance",
                         "Maximum valid ISL distance in meters",
                         config.maxIslDistanceMeters);
    commandLine.AddValue("delayMode", "Link delay mode: fixed or distance", config.delayMode);
    commandLine.AddValue("fixedDelay",
                         "Fixed one-way link delay in seconds",
                         config.fixedDelaySeconds);
    commandLine.AddValue("networkUpdateInterval",
                         "Network state update interval in seconds",
                         config.networkUpdateIntervalSeconds);

    commandLine.AddValue("islBandwidthBps", "ISL data rate in bit/s", config.islBandwidthBps);
    commandLine.AddValue("islMtuBytes", "ISL MTU in bytes", config.islMtuBytes);
    commandLine.AddValue("islQueueBytes", "ISL queue capacity in bytes", config.islQueueBytes);
    commandLine.AddValue("receiverRcvBufBytes",
                         "UDP receive buffer in bytes",
                         config.receiverRcvBufBytes);

    commandLine.AddValue("routingMode", "IPv4 routing policy", config.routingMode);
    commandLine.AddValue("routingRecomputePolicy",
                         "Route recomputation policy",
                         config.routingRecomputePolicy);
    commandLine.AddValue("ecmpHashSeed", "Per-flow ECMP and HRW hash seed", config.ecmpHashSeed);

    commandLine.AddValue("transferTrace", "NetworkTransfer JSON path", config.transferTrace);
    commandLine.AddValue("computeProfile",
                         "Satellite compute profile JSON path",
                         config.computeProfile);
    commandLine.AddValue("taskTrace", "Task trace JSON path", config.taskTrace);
    commandLine.AddValue("transferChunkMode", "Transfer chunking policy", config.transferChunkMode);
    commandLine.AddValue("transferPayloadBytes",
                         "Fixed UDP payload size in bytes",
                         config.transferPayloadBytes);
    commandLine.AddValue("taskCompletionPolicy",
                         "Task completion policy: strict or report",
                         config.taskCompletionPolicy);

    commandLine.AddValue("topologyExportEnabled",
                         "Export position and topology slices",
                         config.topologyExportEnabled);
    commandLine.AddValue("topologyExportInterval",
                         "Topology export interval in seconds",
                         config.topologyExportIntervalSeconds);
    commandLine.AddValue("includeFinalTopologyState",
                         "Export the simulation end state",
                         config.includeFinalTopologyState);
    commandLine.AddValue("outputDir", "Structured output directory", config.outputDirectory);
    commandLine.AddValue("transferLogMode", "Transfer log mode", config.transferLogMode);
    commandLine.AddValue("taskLogMode", "Task log mode", config.taskLogMode);
    commandLine.AddValue("diagnosticMode", "Failure diagnostic mode", config.diagnosticMode);

    commandLine.AddValue("randomSeed", "ns-3 global random seed", config.randomSeed);
    commandLine.AddValue("randomRun", "ns-3 independent run number", config.randomRun);
    commandLine.AddValue("randomStreamStart",
                         "First random stream reserved by SatCompute",
                         config.randomStreamStart);
}

void
ValidateSatComputeConfig(const SatComputeConfig& config)
{
    RequireNotEmpty(config.runName, "runName");
    RequireNotEmpty(config.constellationConfig, "constellationConfig");

    const int64_t startNs =
        SatComputeSecondsToNanoseconds(config.simulationStartSeconds, "simulationStart");
    RequirePositiveSeconds(config.simulationDurationSeconds, "simulationDuration");
    const int64_t durationNs =
        SatComputeSecondsToNanoseconds(config.simulationDurationSeconds, "simulationDuration");
    if (startNs > std::numeric_limits<int64_t>::max() - durationNs)
    {
        FailConfig("simulation", "start plus duration exceeds the int64 nanosecond range");
    }

    RequireChoice(config.topologySource, "topologySource", {"online", "replay"});
    if (config.topologySource == "online" && !config.topologyDirectory.empty())
    {
        FailConfig("topologyDir", "must be empty for online topology");
    }
    if (config.topologySource == "replay" && config.topologyDirectory.empty())
    {
        FailConfig("topologyDir", "is required for replay topology");
    }
    RequireChoice(config.islCandidateStrategy, "islCandidateStrategy", {"plus-grid"});
    if (!std::isfinite(config.maxIslDistanceMeters) || config.maxIslDistanceMeters <= 0.0)
    {
        FailConfig("maxIslDistance", "must be a finite positive number of meters");
    }
    RequireChoice(config.delayMode, "delayMode", {"fixed", "distance"});
    const int64_t fixedDelayNs =
        SatComputeSecondsToNanoseconds(config.fixedDelaySeconds, "fixedDelay");
    if (config.delayMode == "fixed" && fixedDelayNs <= 0)
    {
        FailConfig("fixedDelay", "must be greater than zero in fixed mode");
    }
    RequirePositiveSeconds(config.networkUpdateIntervalSeconds, "networkUpdateInterval");

    if (config.islBandwidthBps == 0)
    {
        FailConfig("islBandwidthBps", "must be greater than zero");
    }
    if (config.islMtuBytes < 68)
    {
        FailConfig("islMtuBytes", "must be at least 68");
    }
    if (config.islQueueBytes == 0)
    {
        FailConfig("islQueueBytes", "must be greater than zero");
    }
    if (config.receiverRcvBufBytes == 0)
    {
        FailConfig("receiverRcvBufBytes", "must be greater than zero");
    }

    RequireChoice(config.routingMode,
                  "routingMode",
                  {"global-first",
                   "global-hash-per-flow",
                   "global-hrw-per-flow",
                   "global-size-aware-hrw",
                   "global-capacity-aware-hrw"});
    RequireChoice(config.routingRecomputePolicy,
                  "routingRecomputePolicy",
                  {"on-topology-change"});

    const bool hasComputeProfile = !config.computeProfile.empty();
    const bool hasTaskTrace = !config.taskTrace.empty();
    if (hasComputeProfile != hasTaskTrace)
    {
        FailConfig("workloads", "computeProfile and taskTrace must be provided together");
    }
    if (!config.transferTrace.empty() && hasComputeProfile)
    {
        FailConfig("workloads", "transferTrace cannot be mixed with task inputs");
    }
    RequireChoice(config.transferChunkMode, "transferChunkMode", {"fixed", "size-aware"});
    if (config.transferPayloadBytes == 0 || config.transferPayloadBytes > 65507)
    {
        FailConfig("transferPayloadBytes", "must be in the range 1..65507");
    }
    if (config.transferChunkMode == "fixed" &&
        config.transferPayloadBytes + 28 > config.islMtuBytes)
    {
        FailConfig("transferPayloadBytes", "plus UDP/IPv4 headers exceeds islMtuBytes");
    }
    if (config.transferChunkMode == "size-aware" && config.islMtuBytes < 64028)
    {
        FailConfig("islMtuBytes", "must be at least 64028 for size-aware chunking");
    }
    RequireChoice(config.taskCompletionPolicy,
                  "taskCompletionPolicy",
                  {"strict", "report"});

    RequirePositiveSeconds(config.topologyExportIntervalSeconds, "topologyExportInterval");
    RequireNotEmpty(config.outputDirectory, "outputDir");
    RequireChoice(config.transferLogMode,
                  "transferLogMode",
                  {"summary", "verbose", "silent"});
    RequireChoice(config.taskLogMode, "taskLogMode", {"summary", "verbose", "silent"});
    RequireChoice(config.diagnosticMode, "diagnosticMode", {"off", "failure"});
    if (config.randomSeed == 0)
    {
        FailConfig("randomSeed", "must be greater than zero");
    }
    if (config.randomStreamStart < 0)
    {
        FailConfig("randomStreamStart", "must be non-negative");
    }
}

int64_t
SatComputeSecondsToNanoseconds(double seconds, std::string_view fieldName)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
    {
        throw SatComputeConfigError(std::string(fieldName) +
                                    " must be a finite non-negative number of seconds");
    }

    constexpr long double NANOSECONDS_PER_SECOND = 1000000000.0L;
    const long double nanoseconds =
        static_cast<long double>(seconds) * NANOSECONDS_PER_SECOND;
    const long double roundedNanoseconds = std::round(nanoseconds);
    if (roundedNanoseconds > static_cast<long double>(std::numeric_limits<int64_t>::max()))
    {
        throw SatComputeConfigError(std::string(fieldName) +
                                    " exceeds the int64 nanosecond range");
    }
    return static_cast<int64_t>(roundedNanoseconds);
}

int64_t
SatComputeDecimalSecondsToNanoseconds(std::string_view token,
                                     std::string_view fieldName,
                                     bool positive)
{
    if (token.empty() || token.front() == '-' || token.front() == '+')
    {
        FailConfig(fieldName, "must be a non-negative decimal number");
    }

    std::size_t position = 0;
    if (token[position] < '0' || token[position] > '9')
    {
        FailConfig(fieldName, "contains an invalid decimal number");
    }
    if (token[position] == '0' && position + 1 < token.size() &&
        token[position + 1] >= '0' && token[position + 1] <= '9')
    {
        FailConfig(fieldName, "contains a leading zero");
    }

    std::string digits;
    while (position < token.size() && token[position] >= '0' && token[position] <= '9')
    {
        digits.push_back(token[position]);
        ++position;
    }

    int fractionalDigits = 0;
    if (position < token.size() && token[position] == '.')
    {
        ++position;
        const std::size_t fractionStart = position;
        while (position < token.size() && token[position] >= '0' && token[position] <= '9')
        {
            digits.push_back(token[position]);
            ++fractionalDigits;
            ++position;
        }
        if (position == fractionStart)
        {
            FailConfig(fieldName, "contains an empty fractional part");
        }
    }

    const int exponent = ParseExponent(token, position, fieldName);
    if (position != token.size())
    {
        FailConfig(fieldName, "contains trailing characters");
    }

    const std::size_t firstNonzero = digits.find_first_not_of('0');
    if (firstNonzero == std::string::npos)
    {
        digits = "0";
    }
    else if (firstNonzero > 0)
    {
        digits.erase(0, firstNonzero);
    }

    const int64_t nanosecondPower =
        9 + static_cast<int64_t>(exponent) - static_cast<int64_t>(fractionalDigits);
    if (digits != "0" && nanosecondPower >= 0)
    {
        if (nanosecondPower > 19 ||
            digits.size() + static_cast<std::size_t>(nanosecondPower) > 19)
        {
            FailConfig(fieldName, "exceeds signed 64-bit nanosecond range");
        }
        digits.append(static_cast<std::size_t>(nanosecondPower), '0');
    }
    else if (digits != "0" && nanosecondPower < 0)
    {
        const int64_t divisorDigits = -nanosecondPower;
        if (divisorDigits > static_cast<int64_t>(digits.size()))
        {
            FailConfig(fieldName, "has precision finer than one nanosecond");
        }
        const std::size_t keep = digits.size() - static_cast<std::size_t>(divisorDigits);
        for (std::size_t index = keep; index < digits.size(); ++index)
        {
            if (digits[index] != '0')
            {
                FailConfig(fieldName, "has precision finer than one nanosecond");
            }
        }
        digits.resize(keep);
    }

    uint64_t nanoseconds = 0;
    const uint64_t maximum = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    for (const char character : digits)
    {
        const uint64_t digit = static_cast<uint64_t>(character - '0');
        if (nanoseconds > (maximum - digit) / 10)
        {
            FailConfig(fieldName, "exceeds signed 64-bit nanosecond range");
        }
        nanoseconds = nanoseconds * 10 + digit;
    }
    const int64_t parsed = static_cast<int64_t>(nanoseconds);
    if (positive && parsed == 0)
    {
        FailConfig(fieldName, "must be positive");
    }
    return parsed;
}

} // namespace ns3
