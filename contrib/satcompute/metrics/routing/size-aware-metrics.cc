/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// 写出大小感知 HRW 的逻辑预留事件和运行结束状态。

#include "size-aware-metrics.h"

#include "ns3/abort.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sys/stat.h>

namespace ns3 {

namespace {

std::string
OutputPath(const std::string& directory, const std::string& filename)
{
  if (directory.empty() || directory == ".")
    {
      return filename;
    }
  mkdir(directory.c_str(), 0755);
  return directory.back() == '/' ? directory + filename
                                 : directory + "/" + filename;
}

} // namespace

void
WriteSizeAwareMetrics(Ptr<FlowRouteRegistry> registry,
                      const std::string& outputDirectory)
{
  NS_ABORT_MSG_IF(registry == nullptr,
                  "size-aware metrics 缺少 flow registry");
  const std::vector<FlowRouteReservationEvent>& events =
    registry->GetEvents();
  std::ofstream eventOutput(
    OutputPath(outputDirectory, "size-aware-reservation-events.csv"),
    std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!eventOutput.is_open(),
                  "无法写入 size-aware reservation events CSV");
  eventOutput
    << "simulation_time_ns,action,selection_reason,route_epoch,node_id,"
       "transfer_id,source_address,destination_address,protocol,source_port,"
       "destination_port,declared_bytes,candidate_gateway,"
       "candidate_output_interface,candidate_destination,"
       "candidate_destination_mask,candidate_reserved_before,"
       "candidate_reserved_after,total_reserved_before,total_reserved_after\n";
  std::map<std::string, uint64_t> actionCounts;
  for (const auto& event : events)
    {
      ++actionCounts[event.action];
      eventOutput << event.simulationTimeNs << ","
                  << event.action << ","
                  << event.selectionReason << ","
                  << event.routeEpoch << ","
                  << event.nodeId << ","
                  << event.transferId << ","
                  << event.flowKey.sourceAddress << ","
                  << event.flowKey.destinationAddress << ","
                  << static_cast<uint32_t>(event.flowKey.protocol) << ","
                  << event.flowKey.sourcePort << ","
                  << event.flowKey.destinationPort << ","
                  << event.declaredBytes << ","
                  << event.candidate.gateway << ","
                  << event.candidate.outputInterface << ","
                  << event.candidate.destination << ","
                  << event.candidate.destinationMask << ","
                  << event.candidateReservedBefore << ","
                  << event.candidateReservedAfter << ","
                  << event.totalReservedBefore << ","
                  << event.totalReservedAfter << "\n";
    }

  std::ofstream summaryOutput(
    OutputPath(outputDirectory, "size-aware-summary.json"),
    std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!summaryOutput.is_open(),
                  "无法写入 size-aware summary JSON");
  summaryOutput
    << "{\n"
    << "  \"registered_flow_count\": "
    << registry->GetRegisteredFlowCount() << ",\n"
    << "  \"active_flow_count_at_end\": "
    << registry->GetActiveFlowCount() << ",\n"
    << "  \"assignment_count_at_end\": "
    << registry->GetAssignmentCount() << ",\n"
    << "  \"final_total_reserved_bytes\": "
    << registry->GetTotalReservedBytes() << ",\n"
    << "  \"peak_total_reserved_bytes\": "
    << registry->GetPeakReservedBytes() << ",\n"
    << "  \"peak_candidate_reserved_bytes\": "
    << registry->GetPeakCandidateReservedBytes() << ",\n"
    << "  \"reservation_event_count\": " << events.size() << ",\n"
    << "  \"assign_event_count\": " << actionCounts["ASSIGN"] << ",\n"
    << "  \"sticky_reuse_event_count\": "
    << actionCounts["STICKY_REUSE"] << ",\n"
    << "  \"candidate_invalid_release_event_count\": "
    << actionCounts["RELEASE_CANDIDATE_INVALID"] << ",\n"
    << "  \"route_invalidated_release_event_count\": "
    << actionCounts["RELEASE_ROUTE_INVALIDATED"] << ",\n"
    << "  \"sender_finished_release_event_count\": "
    << actionCounts["RELEASE_SENDER_FINISHED"] << ",\n"
    << "  \"transfer_completed_release_event_count\": "
    << actionCounts["RELEASE_TRANSFER_COMPLETED"] << "\n"
    << "}\n";
}

void
RemoveSizeAwareMetrics(const std::string& outputDirectory)
{
  std::remove(
    OutputPath(outputDirectory, "size-aware-reservation-events.csv").c_str());
  std::remove(OutputPath(outputDirectory, "size-aware-summary.json").c_str());
}

} // namespace ns3
