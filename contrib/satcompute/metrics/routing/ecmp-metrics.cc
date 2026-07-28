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

// 写出每条流的 ECMP 路由选择证据。

#include "ecmp-metrics.h"

#include "ns3/abort.h"

#include <fstream>
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
  return directory.back() == '/'
           ? directory + filename
           : directory + "/" + filename;
}

} // namespace

void
WriteEcmpRouteEvents(
  const std::vector<EcmpRouteDecisionEvent>& routeEvents,
  const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "ecmp-route-events.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 ECMP 路由证据 CSV");
  output
    << "simulation_time_ns,route_epoch,node_id,source_address,"
       "destination_address,protocol,source_port,destination_port,"
       "candidate_count_before_dedup,candidate_count_after_dedup,"
       "selected_index,selected_gateway,selected_output_interface,"
       "hash_value,selection_reason\n";
  for (const auto& event : routeEvents)
    {
      output << event.simulationTimeNs << ","
             << event.routeEpoch << ","
             << event.nodeId << ","
             << event.flowKey.sourceAddress << ","
             << event.flowKey.destinationAddress << ","
             << static_cast<uint32_t>(event.flowKey.protocol) << ","
             << event.flowKey.sourcePort << ","
             << event.flowKey.destinationPort << ","
             << event.candidateCountBeforeDedup << ","
             << event.candidateCountAfterDedup << ","
             << event.selectedIndex << ","
             << event.selectedGateway << ","
             << event.selectedOutputInterface << ","
             << event.hashValue << ","
             << event.selectionReason << "\n";
    }
}

} // namespace ns3
