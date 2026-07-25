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

#include "metrics/ecmp-route-recorder.h"
#include "metrics/metrics.h"
#include "para.h"
#include "topo.h"
#include "traffic/background-traffic.h"
#include "traffic/network-transfer.h"

#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SatCompute");

int
main(int argc, char* argv[])
{
  SatComputeConfig config = GetDefaultSatComputeConfig();
  CommandLine commandLine;
  commandLine.AddValue("topologyDir",
                       "Directory containing paired nodes_<time>s.json and "
                       "topology_<time>s.json snapshots",
                       config.topologyDirectory);
  commandLine.AddValue("simulationDuration",
                       "Simulation duration in seconds",
                       config.simulationDurationSeconds);
  commandLine.AddValue("offeredLoad",
                       "Multiplier applied to the traffic matrix",
                       config.offeredLoad);
  commandLine.AddValue("transport",
                       "Application transport: udp or tcp",
                       config.transport);
  commandLine.AddValue("trafficMatrix",
                       "100N-row by N-column traffic input in Gbps",
                       config.trafficMatrix);
  commandLine.AddValue("transferTrace",
                       "Optional NetworkTransfer JSON trace",
                       config.transferTrace);
  commandLine.AddValue("transferPacketIntervalNs",
                       "Positive global UDP packet interval for NetworkTransfer",
                       config.transferPacketIntervalNs);
  commandLine.AddValue("routingMode",
                       "Routing mode: global-first or global-hash-per-flow",
                       config.routingMode);
  commandLine.AddValue("ecmpHashSeed",
                       "FNV-1a-64 seed prefix for per-flow ECMP",
                       config.ecmpHashSeed);
  commandLine.AddValue("outputDir",
                       "Metrics output directory",
                       config.outputDirectory);
  commandLine.Parse(argc, argv);

  std::transform(config.transport.begin(),
                 config.transport.end(),
                 config.transport.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });

  if (!std::isfinite(config.simulationDurationSeconds)
      || config.simulationDurationSeconds <= 0.0)
    {
      std::cerr << "[RUN:Error] simulationDuration must be a finite positive number"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (!std::isfinite(config.offeredLoad) || config.offeredLoad < 0.0)
    {
      std::cerr << "[RUN:Error] offeredLoad must be a finite non-negative number"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (config.transport != "udp" && config.transport != "tcp")
    {
      std::cerr << "[RUN:Error] transport must be udp or tcp" << std::endl;
      return EXIT_FAILURE;
    }
  if (config.transferPacketIntervalNs == 0)
    {
      std::cerr << "[RUN:Error] transferPacketIntervalNs must be positive"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (!config.transferTrace.empty() && config.offeredLoad > 0.0)
    {
      std::cerr << "[RUN:Error] transferTrace requires offeredLoad=0"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (!config.transferTrace.empty() && config.transport != "udp")
    {
      std::cerr << "[RUN:Error] NetworkTransfer supports udp only"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (config.routingMode != "global-first"
      && config.routingMode != "global-hash-per-flow")
    {
      std::cerr << "[RUN:Error] routingMode must be global-first or "
                   "global-hash-per-flow"
                << std::endl;
      return EXIT_FAILURE;
    }

  std::cout << "[RUN]" << std::endl
            << "  topologyDir       : " << config.topologyDirectory << std::endl
            << "  simulationDuration: " << config.simulationDurationSeconds << " s"
            << std::endl
            << "  offeredLoad       : " << config.offeredLoad << std::endl
            << "  transport         : " << config.transport << std::endl
            << "  trafficMatrix     : " << config.trafficMatrix << std::endl
            << "  transferTrace     : "
            << (config.transferTrace.empty() ? "(none)" : config.transferTrace)
            << std::endl
            << "  transferInterval  : "
            << config.transferPacketIntervalNs << " ns" << std::endl
            << "  routingMode       : " << config.routingMode << std::endl
            << "  ecmpHashSeed      : " << config.ecmpHashSeed << std::endl
            << "  outputDir         : " << config.outputDirectory << std::endl
            << "  routing           : SatCompute Ipv4GlobalRouting"
            << std::endl
            << std::endl;

  std::chrono::steady_clock::time_point wallClockStart =
    std::chrono::steady_clock::now();

  TopologyConfig topologyConfig = {
    config.topologyDirectory,
    config.simulationDurationSeconds,
    config.routingMode,
    config.ecmpHashSeed
  };
  SatelliteTopology topology(topologyConfig);
  topology.Initialize();
  EcmpRouteRecorder routeRecorder(topology);
  ApplicationState backgroundApplications;
  NetworkTransferState networkTransfers;
  if (config.transferTrace.empty())
    {
      backgroundApplications = InstallApplications(config, topology);
    }
  else
    {
      networkTransfers =
        InstallNetworkTransfers(config.transferTrace,
                                config.transferPacketIntervalNs,
                                config.simulationDurationSeconds,
                                topology);
    }
  Ptr<FlowMonitor> flowMonitor = InstallSimulationFlowMonitor();

  Simulator::Stop(Seconds(config.simulationDurationSeconds));
  Simulator::Run();

  double wallClockSeconds =
    std::chrono::duration<double>(std::chrono::steady_clock::now() - wallClockStart)
      .count();
  std::cout << "[RUN] wall-clock: " << wallClockSeconds << " s"
            << std::endl << std::endl;

  TaskApplicationMetrics applicationMetrics =
    config.transferTrace.empty()
      ? CollectApplicationMetrics(backgroundApplications)
      : CollectNetworkTransferMetrics(networkTransfers);
  std::vector<TransferFlowMetadata> transferFlowMetadata =
    config.transferTrace.empty()
      ? std::vector<TransferFlowMetadata>()
      : CollectNetworkTransferFlowMetadata(networkTransfers);
  MetricsRecorder metrics(flowMonitor,
                          config.simulationDurationSeconds,
                          wallClockSeconds,
                          config.transport,
                          applicationMetrics,
                          transferFlowMetadata,
                          routeRecorder.GetEvents(),
                          config.outputDirectory);
  metrics.Record();
  Simulator::Destroy();
  return EXIT_SUCCESS;
}
