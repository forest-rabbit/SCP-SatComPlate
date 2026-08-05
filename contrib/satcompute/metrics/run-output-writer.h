/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_RUN_OUTPUT_WRITER_H
#define SATCOMPUTE_RUN_OUTPUT_WRITER_H

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

class RunOutputError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct RunOutputContext
{
    std::filesystem::path effectiveConfigPath;
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

struct RunOutputResult
{
    bool complete{};
    bool diagnosticsGenerated{};
    std::filesystem::path runSummaryPath;
    std::vector<std::filesystem::path> files;
};

/** Write deterministic CSV/JSON results for one completed or partial run. */
RunOutputResult WriteRunOutputs(const ResolvedSatComputeConfig& config,
                                const RunOutputContext& context,
                                Ptr<NetworkTransferEngine> transferEngine,
                                Ptr<TaskCoordinator> taskCoordinator);

} // namespace ns3

#endif
