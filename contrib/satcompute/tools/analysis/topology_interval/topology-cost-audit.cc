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

// 测量纯拓扑快照加载与全局路由重算成本，不安装业务、probe 或 FlowMonitor。

#include "../../../routing/satcompute-ipv4-global-routing-helper.h"
#include "../../../third-party/nlohmann/json.hpp"
#include "../../../topology/satellite-topology.h"

#include "ns3/abort.h"
#include "ns3/core-module.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

using json = nlohmann::json;
using namespace ns3;

int
main(int argc, char* argv[])
{
  std::string topologyDirectory;
  std::string outputFile;
  double simulationDurationSeconds = 1.0;

  CommandLine commandLine;
  commandLine.AddValue("topologyDir",
                       "Canonical snapshot directory",
                       topologyDirectory);
  commandLine.AddValue("simulationDuration",
                       "Simulation duration in seconds",
                       simulationDurationSeconds);
  commandLine.AddValue("outputFile",
                       "Destination JSON file",
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
  std::chrono::steady_clock::time_point start =
    std::chrono::steady_clock::now();
  SatelliteTopology topology(config);
  topology.Initialize();
  Simulator::Stop(Seconds(simulationDurationSeconds));
  Simulator::Run();
  double wallClockSeconds =
    std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  uint64_t routeEpoch =
    SatComputeIpv4GlobalRoutingHelper::GetRouting(topology.GetNode(0))
      ->GetRouteEpoch();

  std::ofstream output(outputFile.c_str(),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output, "无法写入 topology cost audit");
  json result = {
    {"schema_version", "0.1"},
    {"node_count", topology.GetNodeCount()},
    {"simulation_duration_s", simulationDurationSeconds},
    {"wall_clock_s", wallClockSeconds},
    {"final_route_epoch", routeEpoch}
  };
  output << result.dump() << "\n";
  output.flush();
  NS_ABORT_MSG_IF(!output, "无法写入 topology cost audit");

  Simulator::Destroy();
  return EXIT_SUCCESS;
}
