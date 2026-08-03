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

#include "metrics/core/flow-metrics.h"
#include "metrics/diagnostics/failure-diagnostics.h"
#include "metrics/metrics.h"
#include "metrics/routing/ecmp-route-recorder.h"
#include "para.h"
#include "routing/common/routing-mode.h"
#include "task/compute-profile.h"
#include "task/task-coordinator.h"
#include "task/task-trace.h"
#include "topology/satellite-topology.h"
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
  commandLine.AddValue("receiverRcvBufBytes",
                       "Receive-buffer bytes for each NetworkTransfer UDP socket",
                       config.receiverRcvBufBytes);
  commandLine.AddValue("transferLogMode",
                       "NetworkTransfer logging: summary, verbose, or silent",
                       config.transferLogMode);
  commandLine.AddValue("taskLogMode",
                       "Task input logging: summary, verbose, or silent",
                       config.taskLogMode);
  commandLine.AddValue("taskCompletionPolicy",
                       "Incomplete task handling: strict or report",
                       config.taskCompletionPolicy);
  commandLine.AddValue("diagnosticMode",
                       "Failure diagnostics: off or failure",
                       config.diagnosticMode);
  commandLine.AddValue("routingMode",
                       "Routing mode: global-first, global-hash-per-flow, "
                       "global-hrw-per-flow, global-size-aware-hrw, or "
                       "global-capacity-aware-hrw",
                       config.routingMode);
  commandLine.AddValue("ecmpHashSeed",
                       "FNV-1a-64 seed prefix for per-flow ECMP",
                       config.ecmpHashSeed);
  commandLine.AddValue("outputDir",
                       "Metrics output directory",
                       config.outputDirectory);
  commandLine.Parse(argc, argv);

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
  std::transform(config.taskCompletionPolicy.begin(),
                 config.taskCompletionPolicy.end(),
                 config.taskCompletionPolicy.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  std::transform(config.diagnosticMode.begin(),
                 config.diagnosticMode.end(),
                 config.diagnosticMode.begin(),
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
  if (config.receiverRcvBufBytes == 0)
    {
      std::cerr << "[RUN:Error] receiverRcvBufBytes must be positive"
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
  RoutingMode routingMode = RoutingMode::GLOBAL_FIRST;
  if (!TryParseRoutingMode(config.routingMode, routingMode))
    {
      std::cerr << "[RUN:Error] routingMode must be global-first, "
                   "global-hash-per-flow, global-hrw-per-flow, "
                   "global-size-aware-hrw, or global-capacity-aware-hrw"
                << std::endl;
      return EXIT_FAILURE;
    }
  std::string pacingMode =
    routingMode == RoutingMode::CAPACITY_AWARE_HRW
      ? "path-bottleneck-serialization"
      : "first-hop-serialization";
  if (taskMode && transferMode)
    {
      std::cerr << "[RUN:Error] task mode cannot be combined with transferTrace"
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
  if (config.taskCompletionPolicy != "strict"
      && config.taskCompletionPolicy != "report")
    {
      std::cerr << "[RUN:Error] taskCompletionPolicy must be strict or report"
                << std::endl;
      return EXIT_FAILURE;
    }
  if (config.diagnosticMode != "off"
      && config.diagnosticMode != "failure")
    {
      std::cerr << "[RUN:Error] diagnosticMode must be off or failure"
                << std::endl;
      return EXIT_FAILURE;
    }

  std::string runMode =
    taskMode
      ? "task"
      : (transferMode ? "network-transfer" : "topology-only");
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
                    << "  completionPolicy   : "
                    << config.taskCompletionPolicy << std::endl
                    << "  diagnosticMode     : " << config.diagnosticMode
                    << std::endl
                    << "  transferChunkMode  : "
                    << config.transferChunkMode << std::endl;
          if (config.transferChunkMode == "fixed")
            {
              std::cout << "  transferPayload    : "
                        << config.transferPayloadBytes << " bytes" << std::endl;
            }
          std::cout << "  pacingMode         : " << pacingMode
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
                    << "  pacingMode         : " << pacingMode
                    << std::endl
                    << "  transferLogMode    : "
                    << config.transferLogMode << std::endl;
        }
      else
        {
          std::cout << "  workload           : none" << std::endl;
        }
      std::cout << "  islMtu             : " << config.islMtuBytes
                << " bytes" << std::endl
                << "  islQueue           : " << config.islQueueBytes
                << " bytes" << std::endl
                << "  receiverRcvBuf     : "
                << config.receiverRcvBufBytes << " bytes" << std::endl
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
    taskMode && config.diagnosticMode == "failure",
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
                                  config.receiverRcvBufBytes,
                                  config.diagnosticMode == "failure",
                                  config.simulationDurationSeconds,
                                  config.taskLogMode);
    }
  NetworkTransferState networkTransfers;
  if (transferMode)
    {
      networkTransfers =
        InstallNetworkTransfers(config.transferTrace,
                                config.transferChunkMode,
                                config.transferPayloadBytes,
                                config.islMtuBytes,
                                config.receiverRcvBufBytes,
                                config.diagnosticMode == "failure",
                                config.transferLogMode,
                                config.simulationDurationSeconds,
                                topology);
    }
  Ptr<FlowMonitor> flowMonitor = InstallSimulationFlowMonitor();

  Simulator::Stop(Seconds(config.simulationDurationSeconds));
  Simulator::Run();
  bool taskRunComplete = !taskMode || taskCoordinator->IsComplete();

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
  std::vector<TransferFlowMetadata> transferFlowMetadata;
  std::vector<TransferSummaryRecord> transferSummaries;
  std::vector<UdpSocketDropEvent> udpSocketDropEvents;
  if (transferMode)
    {
      transferFlowMetadata =
        CollectNetworkTransferFlowMetadata(networkTransfers);
      transferSummaries =
        CollectNetworkTransferSummaries(networkTransfers);
      udpSocketDropEvents =
        networkTransfers.engine->CollectUdpSocketDropEvents();
    }
  else if (taskMode)
    {
      transferFlowMetadata =
        taskCoordinator->GetTransferEngine()->CollectFlowMetadata();
      transferSummaries =
        taskCoordinator->GetTransferEngine()->CollectSummaries();
      udpSocketDropEvents =
        taskCoordinator->GetTransferEngine()->CollectUdpSocketDropEvents();
    }
  CapacityAwareRuntimeSummary capacityAwareSummary;
  if (routingMode == RoutingMode::CAPACITY_AWARE_HRW)
    {
      if (transferMode)
        {
          capacityAwareSummary =
            networkTransfers.engine->CollectCapacityAwareSummary();
        }
      else if (taskMode)
        {
          capacityAwareSummary =
            taskCoordinator->GetTransferEngine()
              ->CollectCapacityAwareSummary();
        }
    }
  RunMetadata runMetadata = {
    runMode,
    config.routingMode,
    config.ecmpHashSeed,
    config.islMtuBytes,
    config.islQueueBytes,
    config.receiverRcvBufBytes,
    (taskMode || transferMode) && config.diagnosticMode == "failure",
    config.diagnosticMode,
    config.taskCompletionPolicy,
    transferMode || taskMode ? pacingMode : "none",
    transferMode || taskMode ? config.transferChunkMode : "none",
    (transferMode || taskMode) && config.transferChunkMode == "fixed"
      ? config.transferPayloadBytes
      : 0,
    taskMode ? config.computeProfile : "",
    taskMode ? config.taskTrace : ""
  };
  MetricsRecorder metrics(flowMonitor,
                          config.simulationDurationSeconds,
                          wallClockSeconds,
                          runMetadata,
                          applicationMetrics,
                          transferFlowMetadata,
                          transferSummaries,
                          routeRecorder.GetEvents(),
                          topology.GetFlowRouteRegistry(),
                          capacityAwareSummary,
                          topology.GetIslDirectedLinks(),
                          topology.GetIslQueueDropEvents(),
                          udpSocketDropEvents,
                          taskMode ? PeekPointer(taskCoordinator) : nullptr,
                          config.outputDirectory);
  metrics.Record();
  int exitCode = EXIT_SUCCESS;
  if (taskMode)
    {
      if (taskRunComplete)
        {
          taskCoordinator->ValidateCompleted();
        }
      else
        {
          std::ostream& message =
            config.taskCompletionPolicy == "report" ? std::cout : std::cerr;
          message << (config.taskCompletionPolicy == "report"
                        ? "[RUN:Report]"
                        : "[RUN:Error]")
                  << " task run incomplete; "
                  << (config.diagnosticMode == "failure"
                        ? "diagnostics"
                        : "base metrics")
                  << " were written to "
                  << (config.diagnosticMode == "failure"
                        ? GetFailureDiagnosticDirectory(
                            config.outputDirectory)
                        : config.outputDirectory)
                  << std::endl;
          if (config.taskCompletionPolicy == "strict")
            {
              exitCode = EXIT_FAILURE;
            }
        }
    }
  Simulator::Destroy();
  return exitCode;
}
