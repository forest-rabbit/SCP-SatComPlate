/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Write auditable per-flow ECMP/HRW route choices.

#include "ecmp-metrics.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace ns3
{

namespace
{

std::filesystem::path
OutputPath(const std::string& directory, const std::string& filename)
{
    const std::filesystem::path root = directory.empty() ? "." : directory;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        throw std::runtime_error("cannot create metrics directory " + root.string() + ": " +
                                 error.message());
    }
    return root / filename;
}

} // namespace

void
WriteEcmpRouteEvents(const std::vector<EcmpRouteDecisionEvent>& routeEvents,
                     const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "ecmp-route-events.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write ecmp-route-events.csv");
    }
    output << "simulation_time_ns,route_epoch,node_id,source_address,destination_address,"
              "protocol,source_port,destination_port,candidate_count_before_dedup,"
              "candidate_count_after_dedup,selected_index,selected_gateway,"
              "selected_output_interface,hash_value,selection_reason\n";
    for (const EcmpRouteDecisionEvent& event : routeEvents)
    {
        output << event.simulationTimeNs << ',' << event.routeEpoch << ',' << event.nodeId << ','
               << event.flowKey.sourceAddress << ',' << event.flowKey.destinationAddress << ','
               << static_cast<uint32_t>(event.flowKey.protocol) << ',' << event.flowKey.sourcePort
               << ',' << event.flowKey.destinationPort << ','
               << event.candidateCountBeforeDedup << ',' << event.candidateCountAfterDedup << ','
               << event.selectedIndex << ',' << event.selectedGateway << ','
               << event.selectedOutputInterface << ',' << event.hashValue << ','
               << event.selectionReason << '\n';
    }
}

} // namespace ns3
