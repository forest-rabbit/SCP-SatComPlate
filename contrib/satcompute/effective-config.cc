/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "effective-config.h"

#include "resolved-config.h"
#include "satcompute-version.h"
#include "sha256.h"
#include "topology/snapshot/snapshot-schedule.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>
#include <string>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

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

Json
SnapshotPairHash(int64_t timeNs,
                 const std::filesystem::path& nodesPath,
                 const std::filesystem::path& topologyPath)
{
    return Json{{"time_ns", timeNs},
                {"nodes", InputHash(nodesPath)},
                {"topology", InputHash(topologyPath)}};
}

Json
BuildReplayInputManifest(const ResolvedSatComputeConfig& config)
{
    if (!config.network.replayDirectory)
    {
        return nullptr;
    }
    const SnapshotSchedule schedule = ScanSatelliteSnapshots(*config.network.replayDirectory,
                                                             config.simulation.durationNs,
                                                             config.network.networkUpdateIntervalNs);
    Json snapshots = Json::array();
    snapshots.push_back(SnapshotPairHash(0,
                                         schedule.initialNodesFilename,
                                         schedule.initialLinksFilename));
    for (const SnapshotUpdate& update : schedule.updates)
    {
        snapshots.push_back(
            SnapshotPairHash(update.timeNs, update.nodesFilename, update.linksFilename));
    }

    Json result = {{"directory", config.network.replayDirectory->string()},
                   {"selected_snapshots", std::move(snapshots)}};
    const std::filesystem::path manifestPath = *config.network.replayDirectory / "manifest.json";
    result["manifest"] = std::filesystem::is_regular_file(manifestPath)
                             ? InputHash(manifestPath)
                             : Json(nullptr);
    return result;
}

void
WriteAtomically(const std::filesystem::path& outputPath, const Json& payload)
{
    const std::filesystem::path temporaryPath = outputPath.string() + ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw EffectiveConfigError("cannot write effective config: " +
                                       temporaryPath.string());
        }
        output << payload.dump(2) << '\n';
        output.close();
        if (!output)
        {
            throw EffectiveConfigError("cannot finish effective config: " +
                                       temporaryPath.string());
        }
    }

    std::error_code error;
    std::filesystem::remove(outputPath, error);
    error.clear();
    std::filesystem::rename(temporaryPath, outputPath, error);
    if (error)
    {
        throw EffectiveConfigError("cannot publish effective config " + outputPath.string() +
                                   ": " + error.message());
    }
}

} // namespace

std::filesystem::path
WriteEffectiveConfig(const ResolvedSatComputeConfig& config, bool validateOnly, bool exportOnly)
{
    std::error_code error;
    std::filesystem::create_directories(config.outputDirectory, error);
    if (error)
    {
        throw EffectiveConfigError("cannot create output directory " +
                                   config.outputDirectory.string() + ": " + error.message());
    }

    Json root = {
        {"schema_version", config.schemaVersion},
        {"run_name", config.runName},
        {"software",
         {{"application", "satcompute"}, {"ns3_version", GetSatComputeNs3Version()}}},
        {"simulation",
         {{"start_time_ns", config.simulation.startTimeNs},
          {"duration_ns", config.simulation.durationNs}}},
        {"constellation",
         {{"schema_version", config.constellation.schemaVersion},
          {"source_path", config.constellation.sourcePath.string()},
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
        {"logging",
         {{"transfer_log_mode", config.logging.transferLogMode},
          {"task_log_mode", config.logging.taskLogMode},
          {"diagnostic_mode", config.logging.diagnosticMode}}},
        {"randomness",
         {{"seed", config.randomness.seed},
          {"run", config.randomness.run},
          {"stream_start", config.randomness.streamStart}}},
        {"operational",
         {{"output_directory", config.outputDirectory.string()},
          {"validate_only", validateOnly},
          {"export_only", exportOnly}}}};

    try
    {
        Json inputManifest = {
            {"constellation_config", InputHash(config.constellation.sourcePath)},
            {"replay_topology", BuildReplayInputManifest(config)}};
        if (config.workloads.transferTrace)
        {
            inputManifest["transfer_trace"] = InputHash(*config.workloads.transferTrace);
        }
        if (config.workloads.computeProfile)
        {
            inputManifest["compute_profile"] = InputHash(*config.workloads.computeProfile);
        }
        if (config.workloads.taskTrace)
        {
            inputManifest["task_trace"] = InputHash(*config.workloads.taskTrace);
        }
        root["input_manifest"] = std::move(inputManifest);
    }
    catch (const std::exception& error)
    {
        throw EffectiveConfigError("cannot build effective input manifest: " +
                                   std::string(error.what()));
    }

    const std::filesystem::path outputPath = config.outputDirectory / "effective-config.json";
    WriteAtomically(outputPath, root);
    return outputPath;
}

} // namespace ns3
