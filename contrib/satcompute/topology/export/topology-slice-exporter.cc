/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "topology-slice-exporter.h"

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

void
WriteJsonFile(const std::filesystem::path& path, const Json& content)
{
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw TopologySliceExporterError("cannot write topology slice " +
                                             temporary.string());
        }
        output << content.dump(2) << '\n';
        output.close();
        if (!output)
        {
            throw TopologySliceExporterError("cannot finish topology slice " +
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
        throw TopologySliceExporterError("cannot publish topology slice " + path.string() +
                                         ": " + error.message());
    }
}

} // namespace

std::vector<int64_t>
BuildTopologySliceTimes(int64_t durationNs, int64_t intervalNs, bool includeFinalState)
{
    if (durationNs <= 0 || intervalNs <= 0)
    {
        throw TopologySliceExporterError(
            "topology duration and slice interval must be positive integer nanoseconds");
    }
    const uint64_t interiorCount = static_cast<uint64_t>((durationNs - 1) / intervalNs);
    const uint64_t totalCount = 1 + interiorCount + (includeFinalState ? 1 : 0);
    if (totalCount > std::numeric_limits<uint32_t>::max())
    {
        throw TopologySliceExporterError("topology slice count exceeds uint32 range");
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
FormatTopologySliceTimeToken(int64_t simulationTimeNs)
{
    if (simulationTimeNs < 0)
    {
        throw TopologySliceExporterError("topology filename time must be non-negative");
    }
    constexpr int64_t NANOSECONDS_PER_SECOND = 1000000000;
    const int64_t wholeSeconds = simulationTimeNs / NANOSECONDS_PER_SECOND;
    const int64_t fractionalNanoseconds = simulationTimeNs % NANOSECONDS_PER_SECOND;
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

TopologySliceExporter::TopologySliceExporter(
    int64_t durationNs,
    int64_t intervalNs,
    bool includeFinalState,
    uint64_t linkBandwidthBps,
    const std::filesystem::path& outputDirectory,
    const OnlineOrbitConstellation& constellation,
    const CircularOrbitTopologyPolicy& policy)
    : m_linkBandwidthBps(linkBandwidthBps),
      m_outputDirectory(std::filesystem::absolute(outputDirectory).lexically_normal()),
      m_constellation(&constellation),
      m_policy(&policy),
      m_scheduledTimesNs(BuildTopologySliceTimes(durationNs,
                                                 intervalNs,
                                                 includeFinalState))
{
    if (m_linkBandwidthBps == 0)
    {
        throw TopologySliceExporterError("topology slice bandwidth must be positive");
    }
}

void
TopologySliceExporter::Initialize()
{
    if (m_initialized)
    {
        throw TopologySliceExporterError("topology slice exporter initialized twice");
    }
    if (!Simulator::Now().IsZero())
    {
        throw TopologySliceExporterError(
            "topology slice exporter must initialize at simulation time zero");
    }
    std::error_code error;
    std::filesystem::create_directories(m_outputDirectory, error);
    if (error)
    {
        throw TopologySliceExporterError("cannot create topology slice directory " +
                                         m_outputDirectory.string() + ": " + error.message());
    }
    m_result.outputDirectory = m_outputDirectory;
    WriteScheduledSlice(0);
    for (std::size_t index = 1; index < m_scheduledTimesNs.size(); ++index)
    {
        const int64_t timeNs = m_scheduledTimesNs[index];
        Simulator::Schedule(NanoSeconds(timeNs),
                            &TopologySliceExporter::WriteScheduledSlice,
                            this,
                            timeNs);
    }
    m_initialized = true;
}

void
TopologySliceExporter::WriteScheduledSlice(int64_t expectedTimeNs)
{
    const int64_t actualTimeNs = Simulator::Now().GetNanoSeconds();
    if (actualTimeNs != expectedTimeNs)
    {
        throw TopologySliceExporterError(
            "topology slice event ran at an unexpected simulation time");
    }
    const CircularOrbitTopologyState state = m_policy->EvaluateCurrent(*m_constellation);
    const std::string timeToken = FormatTopologySliceTimeToken(actualTimeNs);
    const std::filesystem::path nodesPath =
        m_outputDirectory / ("nodes_" + timeToken + "s.json");
    const std::filesystem::path linksPath =
        m_outputDirectory / ("links_" + timeToken + "s.json");

    Json nodes = {{"simulation_time_ns", actualTimeNs}, {"nodes", Json::array()}};
    for (const SatelliteEcefPosition& position : state.positions)
    {
        nodes["nodes"].push_back({{"node_id", position.satelliteId},
                                  {"node_type", "sat"},
                                  {"x", position.positionM.x},
                                  {"y", position.positionM.y},
                                  {"z", position.positionM.z}});
    }

    Json links = {{"simulation_time_ns", actualTimeNs}, {"links", Json::array()}};
    uint32_t activeLinkCount = 0;
    for (const EvaluatedSatelliteLink& link : state.evaluatedLinks)
    {
        links["links"].push_back({{"node1_id", link.sourceId},
                                  {"node2_id", link.destinationId},
                                  {"type", "sat"},
                                  {"active", link.active},
                                  {"distance_m", link.distanceM},
                                  {"delay_ns", link.delayNs},
                                  {"link_bandwidth_bps", m_linkBandwidthBps}});
        if (link.active)
        {
            ++activeLinkCount;
        }
    }

    WriteJsonFile(nodesPath, nodes);
    WriteJsonFile(linksPath, links);
    m_result.slices.push_back({actualTimeNs, nodesPath, linksPath, activeLinkCount});
}

const TopologySliceExportResult&
TopologySliceExporter::Finalize()
{
    if (!m_initialized)
    {
        throw TopologySliceExporterError("topology slice exporter is not initialized");
    }
    if (m_finalized)
    {
        return m_result;
    }
    if (m_result.slices.size() != m_scheduledTimesNs.size())
    {
        throw TopologySliceExporterError(
            "topology-only run ended before all scheduled slices were written");
    }
    m_finalized = true;
    return m_result;
}

const std::vector<int64_t>&
TopologySliceExporter::GetScheduledTimesNs() const
{
    return m_scheduledTimesNs;
}

const TopologySliceExportResult&
TopologySliceExporter::GetResult() const
{
    if (!m_initialized)
    {
        throw TopologySliceExporterError("topology slice exporter is not initialized");
    }
    return m_result;
}

} // namespace ns3
