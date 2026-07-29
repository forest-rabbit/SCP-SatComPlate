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

// 只读导出 ns-3 全局路由的 ECMP 物理下一跳，用于 Python 一致性门禁。

#include "route-audit-common.h"

#include "../../../topology/satellite-topology.h"

#include "ns3/abort.h"
#include "ns3/core-module.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace ns3;

namespace {

void
WriteAuditSnapshot(SatelliteTopology* topology,
                   std::ostream* output,
                   uint32_t timeSeconds)
{
  NS_ABORT_MSG_IF(topology == nullptr || output == nullptr,
                  "route candidate audit callback 参数为空");
  for (uint32_t sourceIndex = 0;
       sourceIndex < topology->GetNodeCount();
       ++sourceIndex)
    {
      uint32_t sourceId =
        topology->GetSatelliteIdByNodeIndex(sourceIndex);
      for (uint32_t destinationIndex = 0;
           destinationIndex < topology->GetNodeCount();
           ++destinationIndex)
        {
          uint32_t destinationId =
            topology->GetSatelliteIdByNodeIndex(destinationIndex);
          if (sourceId == destinationId)
            {
              continue;
            }
          std::vector<uint32_t> candidates =
            topology->GetEcmpCandidateSatelliteIds(sourceId,
                                                   destinationId);
          *output << "{\"time_s\":" << timeSeconds
                  << ",\"source_id\":" << sourceId
                  << ",\"destination_id\":" << destinationId
                  << ",\"reachable\":"
                  << (candidates.empty() ? "false" : "true")
                  << ",\"candidate_next_hop_ids\":[";
          for (std::size_t index = 0; index < candidates.size(); ++index)
            {
              if (index != 0)
                {
                  *output << ",";
                }
              *output << candidates[index];
            }
          *output << "]}\n";
        }
    }
  output->flush();
  NS_ABORT_MSG_IF(!*output, "无法写入 route candidate audit");
}

} // namespace

int
main(int argc, char* argv[])
{
  std::string topologyDirectory;
  std::string outputFile;
  std::string auditTimes = "0";
  double simulationDurationSeconds = 1.0;

  CommandLine commandLine;
  commandLine.AddValue("topologyDir",
                       "Canonical snapshot directory",
                       topologyDirectory);
  commandLine.AddValue("simulationDuration",
                       "Simulation duration in seconds",
                       simulationDurationSeconds);
  commandLine.AddValue("auditTimes",
                       "Comma-separated integer simulation seconds",
                       auditTimes);
  commandLine.AddValue("outputFile",
                       "Destination JSONL file",
                       outputFile);
  commandLine.Parse(argc, argv);

  if (topologyDirectory.empty() || outputFile.empty())
    {
      std::cerr << "topologyDir and outputFile are required" << std::endl;
      return EXIT_FAILURE;
    }
  if (!std::isfinite(simulationDurationSeconds)
      || simulationDurationSeconds <= 0.0)
    {
      std::cerr << "simulationDuration must be finite and positive"
                << std::endl;
      return EXIT_FAILURE;
    }
  std::vector<uint32_t> times =
    ParseRouteAuditTimes(auditTimes, simulationDurationSeconds);
  std::ofstream output(outputFile.c_str(),
                       std::ios::out | std::ios::trunc);
  if (!output)
    {
      std::cerr << "cannot open outputFile: " << outputFile << std::endl;
      return EXIT_FAILURE;
    }

  TopologyConfig config = {
    topologyDirectory,
    simulationDurationSeconds,
    "global-hash-per-flow",
    1,
    1500,
    1500000,
    false,
    false
  };
  SatelliteTopology topology(config);
  topology.Initialize();
  for (uint32_t timeSeconds : times)
    {
      Simulator::Schedule(Seconds(timeSeconds) + NanoSeconds(1),
                          &WriteAuditSnapshot,
                          &topology,
                          &output,
                          timeSeconds);
    }
  Simulator::Stop(Seconds(simulationDurationSeconds));
  Simulator::Run();
  Simulator::Destroy();
  return EXIT_SUCCESS;
}
