/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SATCOMPUTE_FAILURE_DIAGNOSTICS_H
#define SATCOMPUTE_FAILURE_DIAGNOSTICS_H

#include "../core/flow-metrics.h"
#include "../core/run-summary.h"
#include "../../routing/ns3/satcompute-ipv4-global-routing.h"
#include "../../topology/link/satellite-link-state.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3
{

class TaskCoordinator;

std::string GetFailureDiagnosticDirectory(const std::string& outputDirectory);
void PrepareFailureDiagnosticDirectory(const std::string& outputDirectory);
void RemoveFailureDiagnosticOutputs(const std::string& outputDirectory);

/** Legacy seconds-based failure diagnostic entry point. */
void WriteFailureDiagnostics(
    const FlowAggregate& aggregate,
    double simulationDurationSeconds,
    const RunMetadata& runMetadata,
    const std::vector<TransferFlowMetadata>& transferFlows,
    const std::vector<TransferSummaryRecord>& transferSummaries,
    const std::vector<EcmpRouteDecisionEvent>& routeEvents,
    const std::vector<IslDirectedLink>& directedLinks,
    const std::vector<IslQueueDropEvent>& queueDropEvents,
    const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
    const TaskCoordinator& coordinator,
    const std::string& outputDirectory);

/** Precise internal entry point using the resolved integer-nanosecond duration. */
void WriteFailureDiagnosticsNs(
    const FlowAggregate& aggregate,
    int64_t simulationDurationNs,
    const RunMetadata& runMetadata,
    const std::vector<TransferFlowMetadata>& transferFlows,
    const std::vector<TransferSummaryRecord>& transferSummaries,
    const std::vector<EcmpRouteDecisionEvent>& routeEvents,
    const std::vector<IslDirectedLink>& directedLinks,
    const std::vector<IslQueueDropEvent>& queueDropEvents,
    const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
    const TaskCoordinator& coordinator,
    const std::string& outputDirectory);

} // namespace ns3

#endif // SATCOMPUTE_FAILURE_DIAGNOSTICS_H
