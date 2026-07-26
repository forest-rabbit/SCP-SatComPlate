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

// SatCompute 可执行程序入口：解析参数、运行仿真并写出网络指标。

#include "metrics/ecmp-route-recorder.h"
#include "metrics/metrics.h"
#include "para.h"
#include "task/compute-profile.h"
#include "task/task-coordinator.h"
#include "task/task-trace.h"
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
  commandLine.AddValue("computeProfile",
                       "Optional static compute-node profile JSON",
                       config.computeProfile);
  commandLine.AddValue("taskTrace",
                       "Optional compute-task trace JSON",
                       config.taskTrace);
  commandLine.AddValue("transferChunkMode",
                       "NetworkTransfer chunking: fixed or size-aware",
                       config.transferChunkMode);
  commandLine.AddValue("transferPayloadBytes",
                       "UDP application payload cap in fixed chunk mode",
                       config.transferPayloadBytes);
  commandLine.AddValue("islMtuBytes",
                       "MTU applied to every ISL PointToPointNetDevice",
                       config.islMtuBytes);
  commandLine.AddValue("islQueueBytes",
                       "Byte capacity of every ISL DropTail queue",
                       config.islQueueBytes);
  commandLine.AddValue("transferLogMode",
                       "NetworkTransfer logging: summary, verbose, or silent",
                       config.transferLogMode);
  commandLine.AddValue("taskLogMode",
                       "Task input logging: summary, verbose, or silent",
                       config.taskLogMode);
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
  std::transform(config.transferLogMode.begin(),
                 config.transferLogMode.end(),
                 config.transferLogMode.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  std::transform(config.transferChunkMode.begin(),
                 config.transferChunkMode.end(),
                 config.transferChunkMode.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  std::transform(config.taskLogMode.begin(),
                 config.taskLogMode.end(),
                 config.taskLogMode.begin(),
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
  if (config.transferPayloadBytes == 0
      || config.transferPayloadBytes > 65507)
    {
      std::cerr << "[RUN:Error] transferPayloadBytes must be in 1..65507"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (config.transferChunkMode != "fixed"
      && config.transferChunkMode != "size-aware")
    {
      std::cerr << "[RUN:Error] transferChunkMode must be fixed or size-aware"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (config.islMtuBytes < 68)
    {
      std::cerr << "[RUN:Error] islMtuBytes must be in 68..65535"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (config.islQueueBytes == 0)
    {
      std::cerr << "[RUN:Error] islQueueBytes must be positive"
                << std::endl;
      return EXIT_FAILURE;
    }
  uint32_t maximumTransferPayloadBytes =
    config.transferChunkMode == "size-aware"
      ? GetSizeAwareMaximumPayloadBytes()
      : config.transferPayloadBytes;
  if (maximumTransferPayloadBytes + 28u > config.islMtuBytes)
    {
      std::cerr << "[RUN:Error] maximum transfer payload plus UDP/IPv4 headers "
                   "must fit islMtuBytes"
                << std::endl;
      return EXIT_FAILURE;
    }
  bool hasComputeProfile = !config.computeProfile.empty();
  bool hasTaskTrace = !config.taskTrace.empty();
  bool transferMode = !config.transferTrace.empty();
  if (hasComputeProfile != hasTaskTrace)
    {
      std::cerr << "[RUN:Error] computeProfile and taskTrace must be provided "
                   "together"
                << std::endl;
      return EXIT_FAILURE;
    }
  bool taskMode = hasComputeProfile && hasTaskTrace;
  if (taskMode && transferMode)
    {
      std::cerr << "[RUN:Error] task mode cannot be combined with transferTrace"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (taskMode && config.offeredLoad > 0.0)
    {
      std::cerr << "[RUN:Error] task mode requires offeredLoad=0"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (transferMode && config.offeredLoad > 0.0)
    {
      std::cerr << "[RUN:Error] transferTrace requires offeredLoad=0"
                << std::endl;
      return EXIT_FAILURE;
    }
  if ((transferMode || taskMode) && config.transport != "udp")
    {
      std::cerr << "[RUN:Error] NetworkTransfer and task modes support udp only"
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
  if (config.transferLogMode != "summary"
      && config.transferLogMode != "verbose"
      && config.transferLogMode != "silent")
    {
      std::cerr << "[RUN:Error] transferLogMode must be summary, verbose, or silent"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (config.taskLogMode != "summary"
      && config.taskLogMode != "verbose"
      && config.taskLogMode != "silent")
    {
      std::cerr << "[RUN:Error] taskLogMode must be summary, verbose, or silent"
                << std::endl;
      return EXIT_FAILURE;
    }

  bool legacyMode = !taskMode && !transferMode && config.offeredLoad > 0.0;
  std::string runMode =
    taskMode
      ? "task"
      : (transferMode
           ? "network-transfer"
           : (legacyMode ? "legacy-traffic" : "no-workload"));
  bool silentRun =
    (transferMode && config.transferLogMode == "silent")
    || (taskMode && config.taskLogMode == "silent");

  if (!silentRun)
    {
      std::cout << "[RUN]" << std::endl
                << "  mode               : " << runMode
                << std::endl
                << "  topologyDir        : " << config.topologyDirectory << std::endl
                << "  simulationDuration : "
                << config.simulationDurationSeconds << " s" << std::endl;
      if (taskMode)
        {
          std::cout << "  computeProfile     : " << config.computeProfile
                    << std::endl
                    << "  taskTrace          : " << config.taskTrace
                    << std::endl
                    << "  taskLogMode        : " << config.taskLogMode
                    << std::endl
                    << "  transferChunkMode  : "
                    << config.transferChunkMode << std::endl;
          if (config.transferChunkMode == "fixed")
            {
              std::cout << "  transferPayload    : "
                        << config.transferPayloadBytes << " bytes" << std::endl;
            }
          std::cout << "  pacingMode         : first-hop-serialization"
                    << std::endl
                    << "  transferLogMode    : "
                    << config.transferLogMode << std::endl;
        }
      else if (transferMode)
        {
          std::cout << "  transferTrace      : " << config.transferTrace << std::endl
                    << "  transferChunkMode  : "
                    << config.transferChunkMode << std::endl;
          if (config.transferChunkMode == "fixed")
            {
              std::cout << "  transferPayload    : "
                        << config.transferPayloadBytes << " bytes" << std::endl;
            }
          std::cout
                    << "  pacingMode         : first-hop-serialization"
                    << std::endl
                    << "  transferLogMode    : "
                    << config.transferLogMode << std::endl;
        }
      else if (legacyMode)
        {
          std::cout << "  trafficMatrix      : " << config.trafficMatrix << std::endl
                    << "  offeredLoad        : " << config.offeredLoad << std::endl
                    << "  transport          : " << config.transport << std::endl;
        }
      else
        {
          std::cout << "  workload           : none" << std::endl;
        }
      std::cout << "  islMtu             : " << config.islMtuBytes
                << " bytes" << std::endl
                << "  islQueue           : " << config.islQueueBytes
                << " bytes" << std::endl
                << "  routingMode        : " << config.routingMode << std::endl
                << "  ecmpHashSeed       : " << config.ecmpHashSeed << std::endl
                << "  outputDir          : " << config.outputDirectory << std::endl
                << std::endl;
    }

  std::chrono::steady_clock::time_point wallClockStart =
    std::chrono::steady_clock::now();

  TopologyConfig topologyConfig = {
    config.topologyDirectory,
    config.simulationDurationSeconds,
    config.routingMode,
    config.ecmpHashSeed,
    config.islMtuBytes,
    config.islQueueBytes,
    !silentRun
  };
  SatelliteTopology topology(topologyConfig);
  topology.Initialize();
  ComputeProfile computeProfile;
  TaskTrace taskTrace;
  if (taskMode)
    {
      computeProfile =
        ReadComputeProfile(config.computeProfile, topology, config.taskLogMode);
      taskTrace =
        ReadTaskTrace(config.taskTrace,
                      config.simulationDurationSeconds,
                      topology,
                      computeProfile,
                      config.taskLogMode);
    }
  EcmpRouteRecorder routeRecorder(topology);
  Ptr<TaskCoordinator> taskCoordinator;
  if (taskMode)
    {
      taskCoordinator = CreateObject<TaskCoordinator>();
      taskCoordinator->Initialize(computeProfile,
                                  taskTrace,
                                  topology,
                                  config.transferChunkMode,
                                  config.transferPayloadBytes,
                                  config.islMtuBytes,
                                  config.simulationDurationSeconds,
                                  config.taskLogMode);
    }
  ApplicationState backgroundApplications;
  NetworkTransferState networkTransfers;
  if (!taskMode && !transferMode)
    {
      backgroundApplications = InstallApplications(config, topology);
    }
  else if (transferMode)
    {
      networkTransfers =
        InstallNetworkTransfers(config.transferTrace,
                                config.transferChunkMode,
                                config.transferPayloadBytes,
                                config.islMtuBytes,
                                config.transferLogMode,
                                config.simulationDurationSeconds,
                                topology);
    }
  Ptr<FlowMonitor> flowMonitor = InstallSimulationFlowMonitor();

  Simulator::Stop(Seconds(config.simulationDurationSeconds));
  Simulator::Run();
  if (taskMode)
    {
      taskCoordinator->ValidateCompleted();
    }

  double wallClockSeconds =
    std::chrono::duration<double>(std::chrono::steady_clock::now() - wallClockStart)
      .count();
  if (!silentRun)
    {
      std::cout << "[RUN] wall-clock: " << wallClockSeconds << " s"
                << std::endl << std::endl;
    }

  ApplicationMetrics applicationMetrics = {};
  if (transferMode)
    {
      applicationMetrics = CollectNetworkTransferMetrics(networkTransfers);
    }
  else if (taskMode)
    {
      applicationMetrics =
        taskCoordinator->GetTransferEngine()->CollectApplicationMetrics();
    }
  else if (!taskMode)
    {
      applicationMetrics = CollectApplicationMetrics(backgroundApplications);
    }
  std::vector<TransferFlowMetadata> transferFlowMetadata;
  std::vector<TransferSummaryRecord> transferSummaries;
  if (transferMode)
    {
      transferFlowMetadata =
        CollectNetworkTransferFlowMetadata(networkTransfers);
      transferSummaries =
        CollectNetworkTransferSummaries(networkTransfers);
    }
  else if (taskMode)
    {
      transferFlowMetadata =
        taskCoordinator->GetTransferEngine()->CollectFlowMetadata();
      transferSummaries =
        taskCoordinator->GetTransferEngine()->CollectSummaries();
    }
  RunMetadata runMetadata = {
    runMode,
    config.routingMode,
    config.ecmpHashSeed,
    config.islMtuBytes,
    config.islQueueBytes,
    transferMode || taskMode ? "first-hop-serialization" : "none",
    transferMode || taskMode ? config.transferChunkMode : "none",
    (transferMode || taskMode) && config.transferChunkMode == "fixed"
      ? config.transferPayloadBytes
      : 0
  };
  MetricsRecorder metrics(flowMonitor,
                          config.simulationDurationSeconds,
                          wallClockSeconds,
                          runMetadata,
                          applicationMetrics,
                          transferFlowMetadata,
                          transferSummaries,
                          routeRecorder.GetEvents(),
                          config.outputDirectory);
  metrics.Record();
  Simulator::Destroy();
  return EXIT_SUCCESS;
}
