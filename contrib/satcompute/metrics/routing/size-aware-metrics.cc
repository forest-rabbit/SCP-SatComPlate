/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Write logical byte-reservation events and final size-aware state.

#include "size-aware-metrics.h"

#include "../../third-party/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

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

void
RemoveFile(const std::string& directory, const std::string& filename)
{
    const std::filesystem::path root = directory.empty() ? "." : directory;
    std::error_code error;
    std::filesystem::remove(root / filename, error);
    if (error)
    {
        throw std::runtime_error("cannot remove stale metric " + (root / filename).string() +
                                 ": " + error.message());
    }
}

} // namespace

void
WriteSizeAwareMetrics(Ptr<FlowRouteRegistry> registry, const std::string& outputDirectory)
{
    if (registry == nullptr)
    {
        throw std::runtime_error("size-aware metrics require a flow registry");
    }
    const std::vector<FlowRouteReservationEvent>& events = registry->GetEvents();
    std::ofstream eventOutput(OutputPath(outputDirectory, "size-aware-reservation-events.csv"),
                              std::ios::out | std::ios::trunc);
    if (!eventOutput.is_open())
    {
        throw std::runtime_error("cannot write size-aware-reservation-events.csv");
    }
    eventOutput
        << "simulation_time_ns,action,selection_reason,route_epoch,node_id,transfer_id,"
           "source_address,destination_address,protocol,source_port,destination_port,"
           "declared_bytes,candidate_gateway,candidate_output_interface,"
           "candidate_destination,candidate_destination_mask,candidate_reserved_before,"
           "candidate_reserved_after,total_reserved_before,total_reserved_after\n";
    std::map<std::string, uint64_t> actionCounts;
    for (const FlowRouteReservationEvent& event : events)
    {
        ++actionCounts[event.action];
        eventOutput << event.simulationTimeNs << ',' << event.action << ','
                    << event.selectionReason << ',' << event.routeEpoch << ',' << event.nodeId
                    << ',' << event.transferId << ',' << event.flowKey.sourceAddress << ','
                    << event.flowKey.destinationAddress << ','
                    << static_cast<uint32_t>(event.flowKey.protocol) << ','
                    << event.flowKey.sourcePort << ',' << event.flowKey.destinationPort << ','
                    << event.declaredBytes << ',' << event.candidate.gateway << ','
                    << event.candidate.outputInterface << ',' << event.candidate.destination << ','
                    << event.candidate.destinationMask << ','
                    << event.candidateReservedBefore << ',' << event.candidateReservedAfter << ','
                    << event.totalReservedBefore << ',' << event.totalReservedAfter << '\n';
    }

    const Json summary = {
        {"registered_flow_count", registry->GetRegisteredFlowCount()},
        {"active_flow_count_at_end", registry->GetActiveFlowCount()},
        {"assignment_count_at_end", registry->GetAssignmentCount()},
        {"final_total_reserved_bytes", registry->GetTotalReservedBytes()},
        {"peak_total_reserved_bytes", registry->GetPeakReservedBytes()},
        {"peak_candidate_reserved_bytes", registry->GetPeakCandidateReservedBytes()},
        {"reservation_event_count", events.size()},
        {"assign_event_count", actionCounts["ASSIGN"]},
        {"sticky_reuse_event_count", actionCounts["STICKY_REUSE"]},
        {"candidate_invalid_release_event_count",
         actionCounts["RELEASE_CANDIDATE_INVALID"]},
        {"route_invalidated_release_event_count",
         actionCounts["RELEASE_ROUTE_INVALIDATED"]},
        {"sender_finished_release_event_count", actionCounts["RELEASE_SENDER_FINISHED"]},
        {"transfer_completed_release_event_count",
         actionCounts["RELEASE_TRANSFER_COMPLETED"]}};
    std::ofstream summaryOutput(OutputPath(outputDirectory, "size-aware-summary.json"),
                                std::ios::out | std::ios::trunc);
    if (!summaryOutput.is_open())
    {
        throw std::runtime_error("cannot write size-aware-summary.json");
    }
    summaryOutput << summary.dump(2) << '\n';
}

void
RemoveSizeAwareMetrics(const std::string& outputDirectory)
{
    RemoveFile(outputDirectory, "size-aware-reservation-events.csv");
    RemoveFile(outputDirectory, "size-aware-summary.json");
}

} // namespace ns3
