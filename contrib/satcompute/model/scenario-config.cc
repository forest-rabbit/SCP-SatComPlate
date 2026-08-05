/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "scenario-config.h"

#include "../para.h"
#include "sha256.h"

#include "../third-party/nlohmann/json.hpp"

#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

constexpr uint64_t UINT32_MAX_VALUE = std::numeric_limits<uint32_t>::max();
constexpr uint64_t UINT16_MAX_VALUE = std::numeric_limits<uint16_t>::max();
constexpr uint64_t MAX_TIME_MICROSECONDS =
    static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / 1000;
constexpr uint32_t MAX_SATELLITES = 99999;

[[noreturn]] void
Fail(std::string_view field, std::string_view message)
{
    throw ScenarioConfigError(std::string(field) + " " + std::string(message));
}

void
RequireObjectFields(const Json& value,
                    std::string_view name,
                    std::initializer_list<std::string_view> fields)
{
    if (!value.is_object())
    {
        Fail(name, "must be an object");
    }

    std::set<std::string> expected;
    for (const auto field : fields)
    {
        expected.emplace(field);
    }
    std::set<std::string> actual;
    for (const auto& item : value.items())
    {
        actual.insert(item.key());
    }
    if (expected == actual)
    {
        return;
    }

    std::ostringstream message;
    message << "fields differ: missing=[";
    bool first = true;
    for (const auto& field : expected)
    {
        if (!actual.contains(field))
        {
            message << (first ? "" : ",") << field;
            first = false;
        }
    }
    message << "], unknown=[";
    first = true;
    for (const auto& field : actual)
    {
        if (!expected.contains(field))
        {
            message << (first ? "" : ",") << field;
            first = false;
        }
    }
    message << "]";
    Fail(name, message.str());
}

const Json&
GetField(const Json& object, std::string_view field)
{
    const auto iterator = object.find(std::string(field));
    if (iterator == object.end())
    {
        Fail(field, "is missing");
    }
    return *iterator;
}

std::string
RequireString(const Json& value, std::string_view field)
{
    if (!value.is_string())
    {
        Fail(field, "must be a string");
    }
    const std::string parsed = value.get<std::string>();
    if (parsed.empty())
    {
        Fail(field, "must not be empty");
    }
    return parsed;
}

std::string
RequireToken(const Json& value, std::string_view field)
{
    const std::string parsed = RequireString(value, field);
    static const std::regex pattern("^[A-Za-z0-9][A-Za-z0-9._-]*$");
    if (!std::regex_match(parsed, pattern))
    {
        Fail(field, "must be a filesystem-safe token");
    }
    return parsed;
}

std::string
RequireEnum(const Json& value,
            std::string_view field,
            std::initializer_list<std::string_view> allowed)
{
    const std::string parsed = RequireString(value, field);
    for (const auto candidate : allowed)
    {
        if (parsed == candidate)
        {
            return parsed;
        }
    }
    Fail(field, "has an unsupported value: " + parsed);
}

bool
RequireBoolean(const Json& value, std::string_view field)
{
    if (!value.is_boolean())
    {
        Fail(field, "must be a boolean");
    }
    return value.get<bool>();
}

uint64_t
RequireUint64(const Json& value, std::string_view field, uint64_t minimum, uint64_t maximum)
{
    uint64_t parsed = 0;
    if (value.is_number_unsigned())
    {
        parsed = value.get<uint64_t>();
    }
    else if (value.is_number_integer())
    {
        const int64_t signedValue = value.get<int64_t>();
        if (signedValue < 0)
        {
            Fail(field, "must be non-negative");
        }
        parsed = static_cast<uint64_t>(signedValue);
    }
    else
    {
        Fail(field, "must be an integer");
    }
    if (parsed < minimum || parsed > maximum)
    {
        Fail(field, "is outside its supported integer range");
    }
    return parsed;
}

long double
RequireNumber(const Json& value,
              std::string_view field,
              long double minimum,
              std::optional<long double> maximum,
              bool minimumExclusive,
              bool maximumExclusive = false)
{
    if (!value.is_number())
    {
        Fail(field, "must be a number");
    }
    const long double parsed = value.get<long double>();
    if (!std::isfinite(parsed))
    {
        Fail(field, "must be finite");
    }
    if ((minimumExclusive && parsed <= minimum) || (!minimumExclusive && parsed < minimum))
    {
        Fail(field, "is below its supported range");
    }
    if (maximum &&
        ((maximumExclusive && parsed >= *maximum) || (!maximumExclusive && parsed > *maximum)))
    {
        Fail(field, "is above its supported range");
    }
    return parsed;
}

std::filesystem::path
ResolvePath(const std::filesystem::path& baseDirectory, const std::string& value)
{
    std::filesystem::path path(value);
    if (!path.is_absolute())
    {
        path = baseDirectory / path;
    }
    return std::filesystem::absolute(path).lexically_normal();
}

std::optional<std::filesystem::path>
RequireOptionalPath(const Json& value,
                    std::string_view field,
                    const std::filesystem::path& baseDirectory)
{
    if (value.is_null())
    {
        return std::nullopt;
    }
    return ResolvePath(baseDirectory, RequireString(value, field));
}

void
RequireInputFile(const std::optional<std::filesystem::path>& path, std::string_view field)
{
    if (!path)
    {
        return;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(*path, error) || error)
    {
        Fail(field, "must reference an existing regular file: " + path->string());
    }
}

Json
ReadJson(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        throw ScenarioConfigError("cannot open scenario: " + path.string());
    }
    try
    {
        return Json::parse(input, nullptr, true, false);
    }
    catch (const std::exception& error)
    {
        throw ScenarioConfigError("cannot parse scenario " + path.string() + ": " + error.what());
    }
}

Json
OptionalPathJson(const std::optional<std::filesystem::path>& path)
{
    return path ? Json(path->string()) : Json(nullptr);
}

Json
InputHash(const std::filesystem::path& path)
{
    return Json{{"path", path.string()}, {"sha256", Sha256File(path)}};
}

} // namespace

uint32_t
ConstellationConfig::GetSatelliteCount() const
{
    return numOrbits * satellitesPerOrbit;
}

int64_t
ParseSecondsToNanoseconds(std::string_view token, std::string_view field, bool positive)
{
    try
    {
        return SatComputeDecimalSecondsToNanoseconds(token, field, positive);
    }
    catch (const SatComputeConfigError& error)
    {
        throw ScenarioConfigError(error.what());
    }
}

ScenarioConfig
LoadScenarioConfig(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path sourcePath = std::filesystem::weakly_canonical(path, error);
    if (error || !std::filesystem::is_regular_file(sourcePath))
    {
        throw ScenarioConfigError("scenario must be an existing regular file: " + path.string());
    }
    const std::filesystem::path baseDirectory = sourcePath.parent_path();
    const Json root = ReadJson(sourcePath);
    RequireObjectFields(root,
                        "scenario root",
                        {"schema_version",
                         "scenario_name",
                         "simulation",
                         "constellation",
                         "network",
                         "routing",
                         "workloads",
                         "trace_export",
                         "randomness"});

    ScenarioConfig config{};
    config.schemaVersion = RequireString(GetField(root, "schema_version"), "schema_version");
    if (config.schemaVersion != "0.2")
    {
        Fail("schema_version", "must be 0.2");
    }
    config.scenarioName = RequireToken(GetField(root, "scenario_name"), "scenario_name");
    config.sourcePath = sourcePath;

    const Json& simulation = GetField(root, "simulation");
    RequireObjectFields(simulation, "simulation", {"start_time_s", "duration_s"});
    config.simulation.startTimeNs = ParseSecondsToNanoseconds(
        GetField(simulation, "start_time_s").dump(),
        "simulation.start_time_s");
    if (config.simulation.startTimeNs != 0)
    {
        Fail("simulation.start_time_s", "must be 0");
    }
    config.simulation.durationNs = ParseSecondsToNanoseconds(
        GetField(simulation, "duration_s").dump(),
        "simulation.duration_s",
        true);

    const Json& constellation = GetField(root, "constellation");
    RequireObjectFields(constellation,
                        "constellation",
                        {"orbit_provider",
                         "constellation_name",
                         "constellation_pattern",
                         "num_orbits",
                         "satellites_per_orbit",
                         "altitude_m",
                         "inclination_deg",
                         "phase_diff",
                         "orbit_epoch_offset_s"});
    config.constellation.orbitProvider = RequireEnum(GetField(constellation, "orbit_provider"),
                                                     "constellation.orbit_provider",
                                                     {"ns3-circular", "json-replay"});
    config.constellation.constellationName =
        RequireToken(GetField(constellation, "constellation_name"),
                     "constellation.constellation_name");
    config.constellation.constellationPattern =
        RequireEnum(GetField(constellation, "constellation_pattern"),
                    "constellation.constellation_pattern",
                    {"walker-star", "walker-delta"});
    config.constellation.numOrbits = static_cast<uint32_t>(
        RequireUint64(GetField(constellation, "num_orbits"),
                      "constellation.num_orbits",
                      1,
                      MAX_SATELLITES));
    config.constellation.satellitesPerOrbit = static_cast<uint32_t>(
        RequireUint64(GetField(constellation, "satellites_per_orbit"),
                      "constellation.satellites_per_orbit",
                      1,
                      MAX_SATELLITES));
    if (static_cast<uint64_t>(config.constellation.numOrbits) *
            config.constellation.satellitesPerOrbit >
        MAX_SATELLITES)
    {
        Fail("constellation", "total satellite count must not exceed 99999");
    }
    config.constellation.altitudeM = RequireNumber(GetField(constellation, "altitude_m"),
                                                   "constellation.altitude_m",
                                                   0,
                                                   std::nullopt,
                                                   true);
    config.constellation.inclinationDeg =
        RequireNumber(GetField(constellation, "inclination_deg"),
                      "constellation.inclination_deg",
                      0,
                      180,
                      false,
                      true);
    config.constellation.phaseDiff =
        RequireBoolean(GetField(constellation, "phase_diff"), "constellation.phase_diff");
    config.constellation.orbitEpochOffsetNs = ParseSecondsToNanoseconds(
        GetField(constellation, "orbit_epoch_offset_s").dump(),
        "constellation.orbit_epoch_offset_s");
    if (config.constellation.orbitEpochOffsetNs >
        std::numeric_limits<int64_t>::max() - config.simulation.durationNs)
    {
        Fail("constellation.orbit_epoch_offset_s", "plus duration exceeds int64 ns range");
    }

    const Json& network = GetField(root, "network");
    RequireObjectFields(network,
                        "network",
                        {"topology_source",
                         "replay_directory",
                         "isl_candidate_strategy",
                         "seam_enabled",
                         "max_isl_distance_m",
                         "delay_mode",
                         "fixed_delay_us",
                         "network_update_interval_s",
                         "link_bandwidth_bps",
                         "isl_mtu_bytes",
                         "isl_queue_bytes",
                         "receiver_rcv_buf_bytes"});
    config.network.topologySource = RequireEnum(GetField(network, "topology_source"),
                                                "network.topology_source",
                                                {"online", "json-replay"});
    config.network.replayDirectory = RequireOptionalPath(GetField(network, "replay_directory"),
                                                         "network.replay_directory",
                                                         baseDirectory);
    if (config.network.topologySource == "online" && config.network.replayDirectory)
    {
        Fail("network.replay_directory", "must be null for online topology");
    }
    if (config.network.topologySource == "json-replay" && !config.network.replayDirectory)
    {
        Fail("network.replay_directory", "is required for json-replay topology");
    }
    if (config.network.replayDirectory &&
        !std::filesystem::is_directory(*config.network.replayDirectory))
    {
        Fail("network.replay_directory", "must reference an existing directory");
    }
    config.network.islCandidateStrategy =
        RequireEnum(GetField(network, "isl_candidate_strategy"),
                    "network.isl_candidate_strategy",
                    {"plus-grid"});
    config.network.seamEnabled =
        RequireBoolean(GetField(network, "seam_enabled"), "network.seam_enabled");
    config.network.maxIslDistanceM = RequireNumber(GetField(network, "max_isl_distance_m"),
                                                   "network.max_isl_distance_m",
                                                   0,
                                                   std::nullopt,
                                                   true);
    config.network.delayMode = RequireEnum(GetField(network, "delay_mode"),
                                           "network.delay_mode",
                                           {"fixed", "distance"});
    const Json& fixedDelay = GetField(network, "fixed_delay_us");
    if (config.network.delayMode == "fixed")
    {
        const uint64_t fixedDelayUs = RequireUint64(fixedDelay,
                                                    "network.fixed_delay_us",
                                                    1,
                                                    MAX_TIME_MICROSECONDS);
        config.network.fixedDelayNs = static_cast<int64_t>(fixedDelayUs * 1000);
    }
    else
    {
        if (!fixedDelay.is_null())
        {
            Fail("network.fixed_delay_us", "must be null for distance mode");
        }
        config.network.fixedDelayNs = std::nullopt;
    }
    config.network.networkUpdateIntervalNs = ParseSecondsToNanoseconds(
        GetField(network, "network_update_interval_s").dump(),
        "network.network_update_interval_s",
        true);
    config.network.linkBandwidthBps = RequireUint64(GetField(network, "link_bandwidth_bps"),
                                                    "network.link_bandwidth_bps",
                                                    1,
                                                    UINT64_MAX);
    config.network.islMtuBytes = static_cast<uint16_t>(
        RequireUint64(GetField(network, "isl_mtu_bytes"),
                      "network.isl_mtu_bytes",
                      68,
                      UINT16_MAX_VALUE));
    config.network.islQueueBytes = static_cast<uint32_t>(
        RequireUint64(GetField(network, "isl_queue_bytes"),
                      "network.isl_queue_bytes",
                      1,
                      UINT32_MAX_VALUE));
    config.network.receiverRcvBufBytes = static_cast<uint32_t>(
        RequireUint64(GetField(network, "receiver_rcv_buf_bytes"),
                      "network.receiver_rcv_buf_bytes",
                      1,
                      UINT32_MAX_VALUE));

    const Json& routing = GetField(root, "routing");
    RequireObjectFields(routing, "routing", {"mode", "hash_seed", "recompute_policy"});
    config.routing.mode = RequireEnum(GetField(routing, "mode"),
                                      "routing.mode",
                                      {"global-first",
                                       "global-hash-per-flow",
                                       "global-hrw-per-flow",
                                       "global-size-aware-hrw",
                                       "global-capacity-aware-hrw"});
    config.routing.hashSeed =
        RequireUint64(GetField(routing, "hash_seed"), "routing.hash_seed", 0, UINT64_MAX);
    config.routing.recomputePolicy = RequireEnum(GetField(routing, "recompute_policy"),
                                                 "routing.recompute_policy",
                                                 {"on-topology-change"});

    const Json& workloads = GetField(root, "workloads");
    RequireObjectFields(workloads,
                        "workloads",
                        {"transfer_trace",
                         "compute_profile",
                         "task_trace",
                         "transfer_chunk_mode",
                         "transfer_payload_bytes",
                         "task_completion_policy"});
    config.workloads.transferTrace = RequireOptionalPath(GetField(workloads, "transfer_trace"),
                                                         "workloads.transfer_trace",
                                                         baseDirectory);
    config.workloads.computeProfile = RequireOptionalPath(GetField(workloads, "compute_profile"),
                                                          "workloads.compute_profile",
                                                          baseDirectory);
    config.workloads.taskTrace = RequireOptionalPath(GetField(workloads, "task_trace"),
                                                     "workloads.task_trace",
                                                     baseDirectory);
    if (config.workloads.computeProfile.has_value() != config.workloads.taskTrace.has_value())
    {
        Fail("workloads", "compute_profile and task_trace must be provided together");
    }
    if (config.workloads.transferTrace && config.workloads.computeProfile)
    {
        Fail("workloads", "transfer_trace cannot be mixed with task inputs");
    }
    RequireInputFile(config.workloads.transferTrace, "workloads.transfer_trace");
    RequireInputFile(config.workloads.computeProfile, "workloads.compute_profile");
    RequireInputFile(config.workloads.taskTrace, "workloads.task_trace");
    config.workloads.transferChunkMode =
        RequireEnum(GetField(workloads, "transfer_chunk_mode"),
                    "workloads.transfer_chunk_mode",
                    {"fixed", "size-aware"});
    config.workloads.transferPayloadBytes = static_cast<uint32_t>(
        RequireUint64(GetField(workloads, "transfer_payload_bytes"),
                      "workloads.transfer_payload_bytes",
                      1,
                      65507));
    if (config.workloads.transferChunkMode == "fixed" &&
        config.workloads.transferPayloadBytes + 28 > config.network.islMtuBytes)
    {
        Fail("workloads.transfer_payload_bytes", "plus headers exceeds the ISL MTU");
    }
    if (config.workloads.transferChunkMode == "size-aware" && config.network.islMtuBytes < 64028)
    {
        Fail("network.isl_mtu_bytes", "must be at least 64028 for size-aware chunking");
    }
    config.workloads.taskCompletionPolicy =
        RequireEnum(GetField(workloads, "task_completion_policy"),
                    "workloads.task_completion_policy",
                    {"strict", "report"});

    const Json& traceExport = GetField(root, "trace_export");
    RequireObjectFields(traceExport,
                        "trace_export",
                        {"enabled", "interval_s", "include_final_state", "format"});
    config.traceExport.enabled =
        RequireBoolean(GetField(traceExport, "enabled"), "trace_export.enabled");
    config.traceExport.intervalNs = ParseSecondsToNanoseconds(
        GetField(traceExport, "interval_s").dump(),
        "trace_export.interval_s",
        true);
    config.traceExport.includeFinalState =
        RequireBoolean(GetField(traceExport, "include_final_state"),
                       "trace_export.include_final_state");
    config.traceExport.format =
        RequireEnum(GetField(traceExport, "format"), "trace_export.format", {"json-slices"});

    const Json& randomness = GetField(root, "randomness");
    RequireObjectFields(randomness, "randomness", {"seed", "run", "stream_start"});
    config.randomness.seed = static_cast<uint32_t>(
        RequireUint64(GetField(randomness, "seed"), "randomness.seed", 1, UINT32_MAX_VALUE));
    config.randomness.run =
        RequireUint64(GetField(randomness, "run"), "randomness.run", 0, UINT64_MAX);
    config.randomness.streamStart = static_cast<int64_t>(RequireUint64(
        GetField(randomness, "stream_start"),
        "randomness.stream_start",
        0,
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));

    if ((config.constellation.orbitProvider == "ns3-circular" &&
         config.network.topologySource != "online") ||
        (config.constellation.orbitProvider == "json-replay" &&
         config.network.topologySource != "json-replay"))
    {
        Fail("constellation.orbit_provider", "and network.topology_source disagree");
    }
    return config;
}

std::filesystem::path
WriteEffectiveConfig(const ScenarioConfig& config,
                     const std::filesystem::path& outputDirectory,
                     bool validateOnly,
                     bool exportOnly)
{
    const std::filesystem::path resolvedOutput =
        std::filesystem::absolute(outputDirectory).lexically_normal();
    std::error_code error;
    std::filesystem::create_directories(resolvedOutput, error);
    if (error)
    {
        throw ScenarioConfigError("cannot create output directory " + resolvedOutput.string() +
                                  ": " + error.message());
    }

    Json root = {
        {"schema_version", config.schemaVersion},
        {"scenario_name", config.scenarioName},
        {"source_path", config.sourcePath.string()},
        {"simulation",
         {{"start_time_ns", config.simulation.startTimeNs},
          {"duration_ns", config.simulation.durationNs}}},
        {"constellation",
         {{"orbit_provider", config.constellation.orbitProvider},
          {"constellation_name", config.constellation.constellationName},
          {"constellation_pattern", config.constellation.constellationPattern},
          {"num_orbits", config.constellation.numOrbits},
          {"satellites_per_orbit", config.constellation.satellitesPerOrbit},
          {"satellite_count", config.constellation.GetSatelliteCount()},
          {"altitude_m", static_cast<double>(config.constellation.altitudeM)},
          {"inclination_deg", static_cast<double>(config.constellation.inclinationDeg)},
          {"phase_diff", config.constellation.phaseDiff},
          {"orbit_epoch_offset_ns", config.constellation.orbitEpochOffsetNs}}},
        {"network",
         {{"topology_source", config.network.topologySource},
          {"replay_directory", OptionalPathJson(config.network.replayDirectory)},
          {"isl_candidate_strategy", config.network.islCandidateStrategy},
          {"seam_enabled", config.network.seamEnabled},
          {"max_isl_distance_m", static_cast<double>(config.network.maxIslDistanceM)},
          {"delay_mode", config.network.delayMode},
          {"fixed_delay_ns",
           config.network.fixedDelayNs ? Json(*config.network.fixedDelayNs) : Json(nullptr)},
          {"network_update_interval_ns", config.network.networkUpdateIntervalNs},
          {"link_bandwidth_bps", config.network.linkBandwidthBps},
          {"isl_mtu_bytes", config.network.islMtuBytes},
          {"isl_queue_bytes", config.network.islQueueBytes},
          {"receiver_rcv_buf_bytes", config.network.receiverRcvBufBytes}}},
        {"routing",
         {{"mode", config.routing.mode},
          {"hash_seed", config.routing.hashSeed},
          {"recompute_policy", config.routing.recomputePolicy}}},
        {"workloads",
         {{"transfer_trace", OptionalPathJson(config.workloads.transferTrace)},
          {"compute_profile", OptionalPathJson(config.workloads.computeProfile)},
          {"task_trace", OptionalPathJson(config.workloads.taskTrace)},
          {"transfer_chunk_mode", config.workloads.transferChunkMode},
          {"transfer_payload_bytes", config.workloads.transferPayloadBytes},
          {"task_completion_policy", config.workloads.taskCompletionPolicy}}},
        {"trace_export",
         {{"enabled", config.traceExport.enabled},
          {"interval_ns", config.traceExport.intervalNs},
          {"include_final_state", config.traceExport.includeFinalState},
          {"format", config.traceExport.format}}},
        {"randomness",
         {{"seed", config.randomness.seed},
          {"run", config.randomness.run},
          {"stream_start", config.randomness.streamStart}}},
        {"operational",
         {{"output_directory", resolvedOutput.string()},
          {"validate_only", validateOnly},
          {"export_only", exportOnly}}}};

    Json inputHashes = {{"scenario_config", InputHash(config.sourcePath)}};
    if (config.workloads.transferTrace)
    {
        inputHashes["transfer_trace"] = InputHash(*config.workloads.transferTrace);
    }
    if (config.workloads.computeProfile)
    {
        inputHashes["compute_profile"] = InputHash(*config.workloads.computeProfile);
    }
    if (config.workloads.taskTrace)
    {
        inputHashes["task_trace"] = InputHash(*config.workloads.taskTrace);
    }
    root["input_hashes"] = std::move(inputHashes);

    const std::filesystem::path outputPath = resolvedOutput / "effective-config.json";
    const std::filesystem::path temporaryPath = resolvedOutput / "effective-config.json.tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw ScenarioConfigError("cannot write effective config: " + temporaryPath.string());
        }
        output << root.dump(2) << '\n';
        output.close();
        if (!output)
        {
            throw ScenarioConfigError("cannot finish effective config: " + temporaryPath.string());
        }
    }
    std::filesystem::remove(outputPath, error);
    error.clear();
    std::filesystem::rename(temporaryPath, outputPath, error);
    if (error)
    {
        throw ScenarioConfigError("cannot publish effective config " + outputPath.string() + ": " +
                                  error.message());
    }
    return outputPath;
}

} // namespace ns3
