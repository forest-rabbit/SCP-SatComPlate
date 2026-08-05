/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_METRICS_H
#define SATCOMPUTE_METRICS_H

#include "../resolved-config.h"
#include "../routing/ns3/satcompute-ipv4-global-routing.h"
#include "../routing/state/capacity-reservation-state.h"
#include "../routing/state/flow-route-registry.h"
#include "../task/task-coordinator.h"
#include "../topology/link/satellite-link-state.h"
#include "../traffic/network-transfer-engine.h"

#include "ns3/ptr.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

namespace ns3
{

class FlowMonitor;

class MetricsError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/** ns-3.48 runtime and provenance sources consumed by the legacy metrics layers. */
struct MetricsRuntimeContext
{
    std::filesystem::path outputDirectory;
    int64_t wallClockNs{};
    uint32_t appliedTopologySliceCount{};
    uint32_t routeComputationCount{};
    Ptr<FlowRouteRegistry> flowRouteRegistry;
    std::optional<CapacityAwareRuntimeSummary> capacityAwareSummary;
    Ptr<FlowMonitor> flowMonitor;
    std::vector<EcmpRouteDecisionEvent> routeEvents;
    std::vector<IslDirectedLink> directedLinks;
    std::vector<IslQueueDropEvent> queueDropEvents;
};

struct MetricsRecordResult
{
    bool complete{};
    bool diagnosticsGenerated{};
    std::filesystem::path runSummaryPath;
    std::vector<std::filesystem::path> files;
};

/** Orchestrate core, routing, and failure metrics for one simulation run. */
class MetricsRecorder
{
  public:
    MetricsRecorder(const ResolvedSatComputeConfig& config,
                    MetricsRuntimeContext context,
                    Ptr<NetworkTransferEngine> transferEngine,
                    Ptr<TaskCoordinator> taskCoordinator);

    MetricsRecordResult Record();

  private:
    ResolvedSatComputeConfig m_config;
    MetricsRuntimeContext m_context;
    Ptr<NetworkTransferEngine> m_transferEngine;
    Ptr<TaskCoordinator> m_taskCoordinator;
};

} // namespace ns3

#endif // SATCOMPUTE_METRICS_H
