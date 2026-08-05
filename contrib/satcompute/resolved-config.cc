/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "resolved-config.h"

#include "para.h"
#include "model/scenario-config.h"

#include <limits>
#include <string_view>

namespace ns3
{

namespace
{

[[noreturn]] void
Fail(std::string_view field, std::string_view message)
{
    throw ResolvedSatComputeConfigError(std::string(field) + " " + std::string(message));
}

std::filesystem::path
ResolveRegularFile(const std::string& value, std::string_view field)
{
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(value, error);
    if (error || !std::filesystem::is_regular_file(resolved))
    {
        Fail(field, "must reference an existing regular file: " + value);
    }
    return resolved;
}

std::filesystem::path
ResolveDirectory(const std::string& value, std::string_view field)
{
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(value, error);
    if (error || !std::filesystem::is_directory(resolved))
    {
        Fail(field, "must reference an existing directory: " + value);
    }
    return resolved;
}

std::optional<std::filesystem::path>
ResolveOptionalFile(const std::string& value, std::string_view field)
{
    if (value.empty())
    {
        return std::nullopt;
    }
    return ResolveRegularFile(value, field);
}

std::filesystem::path
ResolveOutputDirectory(const std::string& value)
{
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::absolute(value, error);
    if (error)
    {
        Fail("outputDir", "cannot be converted to an absolute path: " + value);
    }
    return resolved.lexically_normal();
}

} // namespace

ResolvedSatComputeConfig
ResolveSatComputeConfig(const SatComputeConfig& config)
{
    try
    {
        ValidateSatComputeConfig(config);
    }
    catch (const SatComputeConfigError& error)
    {
        throw ResolvedSatComputeConfigError(error.what());
    }

    ResolvedSatComputeConfig resolved{};
    resolved.schemaVersion = "0.3";
    resolved.runName = config.runName;
    resolved.simulation.startTimeNs =
        SatComputeSecondsToNanoseconds(config.simulationStartSeconds, "simulationStart");
    resolved.simulation.durationNs =
        SatComputeSecondsToNanoseconds(config.simulationDurationSeconds, "simulationDuration");

    try
    {
        resolved.constellation = LoadConstellationDefinition(config.constellationConfig);
    }
    catch (const ConstellationDefinitionError& error)
    {
        throw ResolvedSatComputeConfigError(error.what());
    }
    const int64_t maximumTime = std::numeric_limits<int64_t>::max();
    if (resolved.constellation.orbitEpochOffsetNs >
        maximumTime - resolved.simulation.startTimeNs - resolved.simulation.durationNs)
    {
        Fail("orbit_epoch_offset_s", "plus simulation window exceeds the int64 ns range");
    }

    resolved.network.topologySource = config.topologySource;
    if (config.topologySource == "replay")
    {
        resolved.network.replayDirectory = ResolveDirectory(config.topologyDirectory,
                                                            "topologyDir");
    }
    resolved.network.islCandidateStrategy = config.islCandidateStrategy;
    resolved.network.seamEnabled = config.seamEnabled;
    resolved.network.maxIslDistanceM = config.maxIslDistanceMeters;
    resolved.network.delayMode = config.delayMode;
    if (config.delayMode == "fixed")
    {
        resolved.network.fixedDelayNs =
            SatComputeSecondsToNanoseconds(config.fixedDelaySeconds, "fixedDelay");
    }
    resolved.network.networkUpdateIntervalNs = SatComputeSecondsToNanoseconds(
        config.networkUpdateIntervalSeconds,
        "networkUpdateInterval");
    resolved.network.linkBandwidthBps = config.islBandwidthBps;
    resolved.network.islMtuBytes = config.islMtuBytes;
    resolved.network.islQueueBytes = config.islQueueBytes;
    resolved.network.receiverRcvBufBytes = config.receiverRcvBufBytes;

    resolved.routing.mode = config.routingMode;
    resolved.routing.hashSeed = config.ecmpHashSeed;
    resolved.routing.recomputePolicy = config.routingRecomputePolicy;

    resolved.workloads.transferTrace =
        ResolveOptionalFile(config.transferTrace, "transferTrace");
    resolved.workloads.computeProfile =
        ResolveOptionalFile(config.computeProfile, "computeProfile");
    resolved.workloads.taskTrace = ResolveOptionalFile(config.taskTrace, "taskTrace");
    resolved.workloads.transferChunkMode = config.transferChunkMode;
    resolved.workloads.transferPayloadBytes = config.transferPayloadBytes;
    resolved.workloads.taskCompletionPolicy = config.taskCompletionPolicy;

    resolved.traceExport.enabled = config.topologyExportEnabled;
    resolved.traceExport.intervalNs = SatComputeSecondsToNanoseconds(
        config.topologyExportIntervalSeconds,
        "topologyExportInterval");
    resolved.traceExport.includeFinalState = config.includeFinalTopologyState;
    resolved.traceExport.format = "json-slices";

    resolved.logging.transferLogMode = config.transferLogMode;
    resolved.logging.taskLogMode = config.taskLogMode;
    resolved.logging.diagnosticMode = config.diagnosticMode;
    resolved.randomness.seed = config.randomSeed;
    resolved.randomness.run = config.randomRun;
    resolved.randomness.streamStart = config.randomStreamStart;
    resolved.outputDirectory = ResolveOutputDirectory(config.outputDirectory);
    return resolved;
}

ResolvedSatComputeConfig
ResolveLegacyScenarioConfig(const ScenarioConfig& scenario,
                            const std::filesystem::path& outputDirectory)
{
    ResolvedSatComputeConfig resolved{};
    resolved.schemaVersion = "0.3";
    resolved.runName = scenario.scenarioName;
    resolved.simulation = {scenario.simulation.startTimeNs, scenario.simulation.durationNs};
    resolved.constellation = {"legacy-inline",
                              scenario.sourcePath,
                              scenario.constellation.constellationName,
                              scenario.constellation.constellationPattern,
                              scenario.constellation.numOrbits,
                              scenario.constellation.satellitesPerOrbit,
                              scenario.constellation.altitudeM,
                              scenario.constellation.inclinationDeg,
                              scenario.constellation.phaseDiff,
                              scenario.constellation.orbitEpochOffsetNs};
    resolved.network.topologySource =
        scenario.network.topologySource == "json-replay" ? "replay" : "online";
    resolved.network.replayDirectory = scenario.network.replayDirectory;
    resolved.network.islCandidateStrategy = scenario.network.islCandidateStrategy;
    resolved.network.seamEnabled = scenario.network.seamEnabled;
    resolved.network.maxIslDistanceM = scenario.network.maxIslDistanceM;
    resolved.network.delayMode = scenario.network.delayMode;
    resolved.network.fixedDelayNs = scenario.network.fixedDelayNs;
    resolved.network.networkUpdateIntervalNs = scenario.network.networkUpdateIntervalNs;
    resolved.network.linkBandwidthBps = scenario.network.linkBandwidthBps;
    resolved.network.islMtuBytes = scenario.network.islMtuBytes;
    resolved.network.islQueueBytes = scenario.network.islQueueBytes;
    resolved.network.receiverRcvBufBytes = scenario.network.receiverRcvBufBytes;
    resolved.routing = {scenario.routing.mode,
                        scenario.routing.hashSeed,
                        scenario.routing.recomputePolicy};
    resolved.workloads = {scenario.workloads.transferTrace,
                          scenario.workloads.computeProfile,
                          scenario.workloads.taskTrace,
                          scenario.workloads.transferChunkMode,
                          scenario.workloads.transferPayloadBytes,
                          scenario.workloads.taskCompletionPolicy};
    resolved.traceExport = {scenario.traceExport.enabled,
                            scenario.traceExport.intervalNs,
                            scenario.traceExport.includeFinalState,
                            scenario.traceExport.format};
    const SatComputeConfig defaults = GetDefaultSatComputeConfig();
    resolved.logging = {defaults.transferLogMode,
                        defaults.taskLogMode,
                        defaults.diagnosticMode};
    resolved.randomness = {scenario.randomness.seed,
                           scenario.randomness.run,
                           scenario.randomness.streamStart};
    resolved.outputDirectory = std::filesystem::absolute(outputDirectory).lexically_normal();
    return resolved;
}

} // namespace ns3
