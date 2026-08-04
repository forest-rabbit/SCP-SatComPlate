/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_SCENARIO_CONFIG_H
#define SATCOMPUTE_SCENARIO_CONFIG_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ns3
{

class ScenarioConfigError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct SimulationConfig
{
    int64_t startTimeNs;
    int64_t durationNs;
};

struct ConstellationConfig
{
    std::string orbitProvider;
    std::string constellationName;
    std::string constellationPattern;
    uint32_t numOrbits;
    uint32_t satellitesPerOrbit;
    long double altitudeM;
    long double inclinationDeg;
    bool phaseDiff;
    int64_t orbitEpochOffsetNs;

    uint32_t GetSatelliteCount() const;
};

struct NetworkConfig
{
    std::string topologySource;
    std::optional<std::filesystem::path> replayDirectory;
    std::string islCandidateStrategy;
    bool seamEnabled;
    long double maxIslDistanceM;
    std::string delayMode;
    std::optional<int64_t> fixedDelayNs;
    int64_t networkUpdateIntervalNs;
    uint64_t linkBandwidthBps;
    uint16_t islMtuBytes;
    uint32_t islQueueBytes;
    uint32_t receiverRcvBufBytes;
};

struct RoutingConfig
{
    std::string mode;
    uint64_t hashSeed;
    std::string recomputePolicy;
};

struct WorkloadConfig
{
    std::optional<std::filesystem::path> transferTrace;
    std::optional<std::filesystem::path> computeProfile;
    std::optional<std::filesystem::path> taskTrace;
    std::string transferChunkMode;
    uint32_t transferPayloadBytes;
    std::string taskCompletionPolicy;
};

struct TraceExportConfig
{
    bool enabled;
    int64_t intervalNs;
    bool includeFinalState;
    std::string format;
};

struct RandomnessConfig
{
    uint32_t seed;
    uint64_t run;
    int64_t streamStart;
};

struct ScenarioConfig
{
    std::string schemaVersion;
    std::string scenarioName;
    std::filesystem::path sourcePath;
    SimulationConfig simulation;
    ConstellationConfig constellation;
    NetworkConfig network;
    RoutingConfig routing;
    WorkloadConfig workloads;
    TraceExportConfig traceExport;
    RandomnessConfig randomness;
};

/**
 * Convert one decimal JSON seconds token to exact signed 64-bit nanoseconds.
 *
 * @param token Decimal or exponent-form JSON number token.
 * @param field Field name used in diagnostics.
 * @param positive Whether zero is forbidden.
 * @return Exact integer nanoseconds.
 * @throws ScenarioConfigError for invalid, sub-nanosecond, or overflowing values.
 */
int64_t ParseSecondsToNanoseconds(std::string_view token,
                                  std::string_view field,
                                  bool positive = false);

/**
 * Load and strictly validate a scenario 0.2 JSON file.
 *
 * @param path Scenario JSON path.
 * @return Fully resolved semantic configuration.
 * @throws ScenarioConfigError on I/O, JSON, type, field, or cross-field errors.
 */
ScenarioConfig LoadScenarioConfig(const std::filesystem::path& path);

/**
 * Write a deterministic resolved configuration and SHA-256 input manifest.
 *
 * @param config Validated scenario.
 * @param outputDirectory Operational output directory.
 * @param validateOnly Operational no-simulation mode selected by the CLI.
 * @return Path to effective-config.json.
 */
std::filesystem::path WriteEffectiveConfig(const ScenarioConfig& config,
                                           const std::filesystem::path& outputDirectory,
                                           bool validateOnly = false);

} // namespace ns3

#endif // SATCOMPUTE_SCENARIO_CONFIG_H
