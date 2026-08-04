/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_CIRCULAR_ORBIT_TRACE_EXPORTER_H
#define SATCOMPUTE_CIRCULAR_ORBIT_TRACE_EXPORTER_H

#include "../../model/scenario-config.h"
#include "../online/circular-orbit-topology-policy.h"
#include "../orbit/online-orbit-constellation.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace ns3
{

class CircularOrbitTraceExporterError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct TopologyTraceSliceRecord
{
    int64_t simulationTimeNs{};
    std::filesystem::path nodesPath;
    std::filesystem::path topologyPath;
    std::string nodesSha256;
    std::string topologySha256;
    uint32_t activeLinkCount{};
};

struct TopologyTraceExportResult
{
    std::filesystem::path outputDirectory;
    std::filesystem::path manifestPath;
    std::vector<TopologyTraceSliceRecord> slices;
};

/** Build canonical trace times: zero, interior interval points, optional end. */
std::vector<int64_t> BuildTopologyTraceTimes(int64_t durationNs,
                                             int64_t intervalNs,
                                             bool includeFinalState);

/** Format exact integer nanoseconds as a canonical decimal-seconds token. */
std::string FormatTopologyTraceTimeToken(int64_t simulationTimeNs);

/**
 * Read-only JSON-slice exporter shared by online simulation and export-only runs.
 *
 * The exporter owns no mobility or network state and therefore cannot perturb
 * orbit evolution, candidate evaluation, or routing event order.
 */
class CircularOrbitTraceExporter
{
  public:
    CircularOrbitTraceExporter(const ScenarioConfig& config,
                               const std::filesystem::path& outputDirectory,
                               const OnlineOrbitConstellation& constellation);

    void Initialize();
    const TopologyTraceExportResult& Finalize();
    const std::vector<int64_t>& GetScheduledTimesNs() const;
    const TopologyTraceExportResult& GetResult() const;

  private:
    void WriteScheduledSlice(int64_t expectedTimeNs);

    ScenarioConfig m_config;
    std::filesystem::path m_outputDirectory;
    const OnlineOrbitConstellation* m_constellation;
    CircularOrbitTopologyPolicy m_policy;
    std::vector<int64_t> m_scheduledTimesNs;
    TopologyTraceExportResult m_result;
    bool m_initialized{};
    bool m_finalized{};
};

} // namespace ns3

#endif // SATCOMPUTE_CIRCULAR_ORBIT_TRACE_EXPORTER_H
