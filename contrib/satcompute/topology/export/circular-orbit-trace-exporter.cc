/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "circular-orbit-trace-exporter.h"

#include "../../satcompute-version.h"
#include "../../sha256.h"
#include <nlohmann/json.hpp>

#include "ns3/simulator.h"

#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

std::string
CandidateKindToString(PlusGridCandidateKind kind)
{
    return kind == PlusGridCandidateKind::INTRA_PLANE ? "intra-plane" : "inter-plane";
}

void
WriteJsonFile(const std::filesystem::path& path, const Json& content)
{
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw CircularOrbitTraceExporterError("cannot write topology trace " +
                                                  temporary.string());
        }
        output << content.dump(2) << '\n';
        output.close();
        if (!output)
        {
            throw CircularOrbitTraceExporterError("cannot finish topology trace " +
                                                  temporary.string());
        }
    }

    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::filesystem::remove(temporary);
        throw CircularOrbitTraceExporterError("cannot publish topology trace " + path.string() +
                                              ": " + error.message());
    }
}

} // namespace

std::vector<int64_t>
BuildTopologyTraceTimes(int64_t durationNs, int64_t intervalNs, bool includeFinalState)
{
    if (durationNs <= 0 || intervalNs <= 0)
    {
        throw CircularOrbitTraceExporterError(
            "trace duration and interval must be positive integer nanoseconds");
    }
    const uint64_t interiorCount =
        static_cast<uint64_t>((durationNs - 1) / intervalNs);
    const uint64_t totalCount = 1 + interiorCount + (includeFinalState ? 1 : 0);
    if (totalCount > std::numeric_limits<uint32_t>::max())
    {
        throw CircularOrbitTraceExporterError("topology trace slice count exceeds uint32 range");
    }

    std::vector<int64_t> times;
    times.reserve(static_cast<std::size_t>(totalCount));
    times.push_back(0);
    for (int64_t timeNs = intervalNs; timeNs < durationNs;)
    {
        times.push_back(timeNs);
        if (durationNs - timeNs <= intervalNs)
        {
            break;
        }
        timeNs += intervalNs;
    }
    if (includeFinalState)
    {
        times.push_back(durationNs);
    }
    return times;
}

std::string
FormatTopologyTraceTimeToken(int64_t simulationTimeNs)
{
    if (simulationTimeNs < 0)
    {
        throw CircularOrbitTraceExporterError("trace filename time must be non-negative");
    }
    constexpr int64_t nanosecondsPerSecond = 1000000000;
    const int64_t wholeSeconds = simulationTimeNs / nanosecondsPerSecond;
    const int64_t fractionalNanoseconds = simulationTimeNs % nanosecondsPerSecond;
    std::ostringstream token;
    token << wholeSeconds;
    if (fractionalNanoseconds != 0)
    {
        std::ostringstream fraction;
        fraction << std::setfill('0') << std::setw(9) << fractionalNanoseconds;
        std::string digits = fraction.str();
        while (digits.back() == '0')
        {
            digits.pop_back();
        }
        token << '.' << digits;
    }
    return token.str();
}

CircularOrbitTraceExporter::CircularOrbitTraceExporter(
    const ResolvedSatComputeConfig& config,
    const std::filesystem::path& outputDirectory,
    const OnlineOrbitConstellation& constellation)
    : m_config(config),
      m_outputDirectory(std::filesystem::absolute(outputDirectory).lexically_normal()),
      m_constellation(&constellation),
      m_policy(config),
      m_scheduledTimesNs(BuildTopologyTraceTimes(config.simulation.durationNs,
                                                 config.traceExport.intervalNs,
                                                 config.traceExport.includeFinalState))
{
    if (!m_config.traceExport.enabled || m_config.traceExport.format != "json-slices")
    {
        throw CircularOrbitTraceExporterError(
            "circular-orbit trace exporter requires enabled json-slices output");
    }
    if (m_config.network.topologySource != "online")
    {
        throw CircularOrbitTraceExporterError(
            "circular-orbit trace exporter requires online orbit state");
    }
}

void
CircularOrbitTraceExporter::Initialize()
{
    if (m_initialized)
    {
        throw CircularOrbitTraceExporterError("topology trace exporter initialized twice");
    }
    if (!Simulator::Now().IsZero())
    {
        throw CircularOrbitTraceExporterError(
            "topology trace exporter must initialize at simulation time zero");
    }
    std::error_code error;
    std::filesystem::create_directories(m_outputDirectory, error);
    if (error)
    {
        throw CircularOrbitTraceExporterError("cannot create topology trace directory " +
                                              m_outputDirectory.string() + ": " +
                                              error.message());
    }
    m_result.outputDirectory = m_outputDirectory;
    WriteScheduledSlice(0);
    for (std::size_t index = 1; index < m_scheduledTimesNs.size(); ++index)
    {
        const int64_t timeNs = m_scheduledTimesNs[index];
        Simulator::Schedule(NanoSeconds(timeNs),
                            &CircularOrbitTraceExporter::WriteScheduledSlice,
                            this,
                            timeNs);
    }
    m_initialized = true;
}

void
CircularOrbitTraceExporter::WriteScheduledSlice(int64_t expectedTimeNs)
{
    const int64_t actualTimeNs = Simulator::Now().GetNanoSeconds();
    if (actualTimeNs != expectedTimeNs)
    {
        throw CircularOrbitTraceExporterError(
            "topology trace event ran at an unexpected simulation time");
    }
    const CircularOrbitTopologyState state = m_policy.EvaluateCurrent(*m_constellation);
    const std::string timeToken = FormatTopologyTraceTimeToken(actualTimeNs);
    const std::filesystem::path nodesPath =
        m_outputDirectory / ("nodes_" + timeToken + "s.json");
    const std::filesystem::path topologyPath =
        m_outputDirectory / ("topology_" + timeToken + "s.json");

    Json nodes = {{"schema_version", "0.2"},
                  {"simulation_time_ns", actualTimeNs},
                  {"state_semantics", "orbit-policy-evaluation"},
                  {"coordinate_frame", "ECEF"},
                  {"coordinate_units", "m"},
                  {"node_count", state.positions.size()},
                  {"nodes", Json::array()}};
    for (const SatelliteEcefPosition& position : state.positions)
    {
        nodes["nodes"].push_back({{"node_id", position.satelliteId},
                                  {"node_type", "sat"},
                                  {"x_m", position.positionM.x},
                                  {"y_m", position.positionM.y},
                                  {"z_m", position.positionM.z}});
    }

    Json topology = {{"schema_version", "0.2"},
                     {"simulation_time_ns", actualTimeNs},
                     {"state_semantics", "orbit-policy-evaluation"},
                     {"distance_units", "m"},
                     {"delay_units", "ns"},
                     {"bandwidth_units", "bps"},
                     {"candidate_link_count", state.evaluatedLinks.size()},
                     {"active_link_count", 0},
                     {"links", Json::array()}};
    uint32_t activeLinkCount = 0;
    for (const EvaluatedSatelliteLink& link : state.evaluatedLinks)
    {
        if (!link.active)
        {
            continue;
        }
        topology["links"].push_back(
            {{"node1_id", link.sourceId},
             {"node2_id", link.destinationId},
             {"type", "sat"},
             {"candidate_kind", CandidateKindToString(link.kind)},
             {"distance_m", link.distanceM},
             {"delay_ns", link.delayNs},
             {"link_bandwidth_bps", m_config.network.linkBandwidthBps}});
        ++activeLinkCount;
    }
    topology["active_link_count"] = activeLinkCount;

    WriteJsonFile(nodesPath, nodes);
    WriteJsonFile(topologyPath, topology);
    m_result.slices.push_back({actualTimeNs,
                               nodesPath,
                               topologyPath,
                               Sha256File(nodesPath),
                               Sha256File(topologyPath),
                               activeLinkCount});
}

const TopologyTraceExportResult&
CircularOrbitTraceExporter::Finalize()
{
    if (!m_initialized)
    {
        throw CircularOrbitTraceExporterError("topology trace exporter is not initialized");
    }
    if (m_finalized)
    {
        return m_result;
    }
    if (m_result.slices.size() != m_scheduledTimesNs.size())
    {
        throw CircularOrbitTraceExporterError(
            "topology trace ended before all scheduled slices were written");
    }

    Json slices = Json::array();
    for (const TopologyTraceSliceRecord& slice : m_result.slices)
    {
        slices.push_back(
            {{"simulation_time_ns", slice.simulationTimeNs},
             {"nodes_file", slice.nodesPath.filename().string()},
             {"nodes_sha256", slice.nodesSha256},
             {"topology_file", slice.topologyPath.filename().string()},
             {"topology_sha256", slice.topologySha256},
             {"active_link_count", slice.activeLinkCount}});
    }
    const Json manifest = {
        {"schema_version", "0.3"},
        {"run_name", m_config.runName},
        {"constellation_config_sha256", Sha256File(m_config.constellation.sourcePath)},
        {"ns3_version", GetSatComputeNs3Version()},
        {"state_semantics", "orbit-policy-evaluation"},
        {"coordinate_frame", "ECEF"},
        {"coordinate_units", "m"},
        {"speed_of_light_m_per_s", SATCOMPUTE_SPEED_OF_LIGHT_M_PER_S},
        {"simulation_duration_ns", m_config.simulation.durationNs},
        {"trace_interval_ns", m_config.traceExport.intervalNs},
        {"network_update_interval_ns", m_config.network.networkUpdateIntervalNs},
        {"include_final_state", m_config.traceExport.includeFinalState},
        {"constellation",
         {{"num_orbits", m_config.constellation.shell.planes},
          {"satellites_per_orbit", m_config.constellation.shell.sats},
          {"satellite_count", m_config.constellation.GetSatelliteCount()},
          {"altitude_km", m_config.constellation.shell.alt},
          {"inclination_deg", m_config.constellation.shell.inc},
          {"phasing_factor", m_config.constellation.shell.phasing},
          {"raan_span_deg", m_config.constellation.shell.raanSpanDeg}}},
        {"topology",
         {{"candidate_strategy", m_config.network.islCandidateStrategy},
          {"seam_enabled", m_config.network.seamEnabled},
          {"max_isl_distance_m", static_cast<double>(m_config.network.maxIslDistanceM)},
          {"delay_mode", m_config.network.delayMode},
          {"fixed_delay_ns",
           m_config.network.fixedDelayNs ? Json(*m_config.network.fixedDelayNs) : Json(nullptr)},
          {"link_bandwidth_bps", m_config.network.linkBandwidthBps}}},
        {"randomness",
         {{"seed", m_config.randomness.seed},
          {"run", m_config.randomness.run},
          {"stream_start", m_config.randomness.streamStart}}},
        {"slice_count", slices.size()},
        {"slices", std::move(slices)}};

    m_result.manifestPath = m_outputDirectory / "manifest.json";
    WriteJsonFile(m_result.manifestPath, manifest);
    m_finalized = true;
    return m_result;
}

const std::vector<int64_t>&
CircularOrbitTraceExporter::GetScheduledTimesNs() const
{
    return m_scheduledTimesNs;
}

const TopologyTraceExportResult&
CircularOrbitTraceExporter::GetResult() const
{
    if (!m_initialized)
    {
        throw CircularOrbitTraceExporterError("topology trace exporter is not initialized");
    }
    return m_result;
}

} // namespace ns3
