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

// 编排基础网络、传输、任务指标，并按需调用独立的失败诊断模块。

#include "metrics.h"

#include "core/flow-metrics.h"
#include "core/run-summary.h"
#include "core/task-metrics.h"
#include "core/transfer-metrics.h"
#include "diagnostics/failure-diagnostics.h"
#include "diagnostics/flow-drop-reason-diagnostics.h"
#include "routing/ecmp-metrics.h"
#include "routing/size-aware-metrics.h"
#include "../task/task-coordinator.h"

#include <iostream>
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
  return directory.back() == '/' ? directory + filename : directory + "/" + filename;
}

} // namespace

MetricsRecorder::MetricsRecorder(Ptr<FlowMonitor> monitor,
                                 double simulationDurationSeconds,
                                 double wallClockSeconds,
                                 const RunMetadata& runMetadata,
                                 const ApplicationMetrics& applicationMetrics,
                                 const std::vector<TransferFlowMetadata>& transferFlows,
                                 const std::vector<TransferSummaryRecord>& transferSummaries,
                                 const std::vector<EcmpRouteDecisionEvent>& routeEvents,
                                 Ptr<SizeAwareFlowRegistry> sizeAwareRegistry,
                                 const std::vector<IslDirectedLink>& directedLinks,
                                 const std::vector<IslQueueDropEvent>& queueDropEvents,
                                 const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
                                 const TaskCoordinator* taskCoordinator,
                                 const std::string& outputDirectory)
  : m_monitor(monitor),
    m_simulationDurationSeconds(simulationDurationSeconds),
    m_wallClockSeconds(wallClockSeconds),
    m_runMetadata(runMetadata),
    m_applicationMetrics(applicationMetrics),
    m_transferFlows(transferFlows),
    m_transferSummaries(transferSummaries),
    m_routeEvents(routeEvents),
    m_sizeAwareRegistry(sizeAwareRegistry),
    m_directedLinks(directedLinks),
    m_queueDropEvents(queueDropEvents),
    m_udpSocketDropEvents(udpSocketDropEvents),
    m_taskCoordinator(taskCoordinator),
    m_outputDirectory(outputDirectory)
{
}

void
MetricsRecorder::Record()
{
  FlowAggregate aggregate = CollectFlowAggregate(m_monitor);

  bool taskRunComplete =
    m_taskCoordinator == nullptr || m_taskCoordinator->IsComplete();
  bool diagnosticsEnabled =
    m_taskCoordinator != nullptr
    && m_runMetadata.diagnosticMode == "failure";
  bool writeDiagnostics = !taskRunComplete && diagnosticsEnabled;
  bool transferOnlyRun =
    m_runMetadata.mode == "network-transfer";
  bool writeFlowDropReasons =
    m_runMetadata.diagnosticMode == "failure"
    && (transferOnlyRun
        || (m_taskCoordinator != nullptr && !taskRunComplete));
  RemoveFailureDiagnosticOutputs(m_outputDirectory);
  PrintNetworkMetrics(aggregate);
  WriteNetworkMetrics(aggregate, m_outputDirectory);
  WriteNetworkFlowDetails(m_monitor,
                          m_transferFlows,
                          m_outputDirectory,
                          taskRunComplete);
  if (writeFlowDropReasons)
    {
      WriteFlowDropReasons(m_monitor,
                           m_transferFlows,
                           m_outputDirectory);
    }
  WriteEcmpRouteEvents(m_routeEvents, m_outputDirectory);
  if (m_runMetadata.routingMode == "global-size-aware-hrw")
    {
      WriteSizeAwareMetrics(m_sizeAwareRegistry, m_outputDirectory);
    }
  else
    {
      NS_ABORT_MSG_IF(m_sizeAwareRegistry != nullptr,
                      "非 size-aware 运行不应持有 flow registry");
      RemoveSizeAwareMetrics(m_outputDirectory);
    }
  WriteTransferSummaries(m_transferSummaries, m_outputDirectory);
  if (m_taskCoordinator != nullptr)
    {
      WriteTaskMetrics(*m_taskCoordinator,
                       m_simulationDurationSeconds,
                       m_outputDirectory);
      if (writeDiagnostics)
        {
          WriteFailureDiagnostics(aggregate,
                                  m_simulationDurationSeconds,
                                  m_runMetadata,
                                  m_transferFlows,
                                  m_transferSummaries,
                                  m_routeEvents,
                                  m_directedLinks,
                                  m_queueDropEvents,
                                  m_udpSocketDropEvents,
                                  *m_taskCoordinator,
                                  m_outputDirectory);
        }
    }
  WriteRunSummary(aggregate,
                  m_simulationDurationSeconds,
                  m_wallClockSeconds,
                  m_runMetadata,
                  m_applicationMetrics,
                  m_transferSummaries,
                  m_udpSocketDropEvents,
                  m_taskCoordinator,
                  m_outputDirectory);

  std::cout << "[METRICS] Output" << std::endl
            << "  network : "
            << OutputPath(m_outputDirectory, "network-flow-metrics.csv") << std::endl
            << "  details : "
            << OutputPath(m_outputDirectory, "network-flow-details.csv") << std::endl
            << "  ECMP    : "
            << OutputPath(m_outputDirectory, "ecmp-route-events.csv") << std::endl
            << "  transfer: "
            << OutputPath(m_outputDirectory, "transfer-summary.csv") << std::endl
            << "  run     : "
            << OutputPath(m_outputDirectory, "run-summary.json") << std::endl;
  if (m_sizeAwareRegistry != nullptr)
    {
      std::cout
        << "  reserve : "
        << OutputPath(m_outputDirectory,
                      "size-aware-reservation-events.csv")
        << std::endl
        << "  size-aware summary: "
        << OutputPath(m_outputDirectory, "size-aware-summary.json")
        << std::endl;
    }
  if (m_taskCoordinator != nullptr)
    {
      std::string failureDirectory =
        GetFailureDiagnosticDirectory(m_outputDirectory);
      std::cout
        << "  task    : "
        << OutputPath(m_outputDirectory, "task-summary.csv") << std::endl
        << "  events  : "
        << OutputPath(m_outputDirectory, "task-events.csv") << std::endl
        << "  compute : "
        << OutputPath(m_outputDirectory, "compute-node-summary.csv")
        << std::endl;
      if (writeDiagnostics)
        {
          std::cout
            << "  incomplete tasks     : "
            << OutputPath(failureDirectory, "incomplete-tasks.csv")
            << std::endl
            << "  incomplete transfers : "
            << OutputPath(failureDirectory, "incomplete-transfers.csv")
            << std::endl
            << "  ISL queue drops      : "
            << OutputPath(failureDirectory, "isl-queue-drops.csv")
            << std::endl
            << "  ISL drop summary     : "
            << OutputPath(failureDirectory, "isl-queue-drop-summary.csv")
            << std::endl
            << "  UDP socket drops     : "
            << OutputPath(failureDirectory, "udp-socket-drops.csv")
            << std::endl
            << "  UDP drop summary     : "
            << OutputPath(failureDirectory, "udp-socket-drop-summary.csv")
            << std::endl
            << "  flow/link load       : "
            << OutputPath(failureDirectory, "flow-link-concentration.csv")
            << std::endl
            << "  diagnostics          : "
            << OutputPath(failureDirectory, "diagnostic-summary.json")
            << std::endl;
        }
    }
  if (writeFlowDropReasons)
    {
      std::cout
        << "  FlowMonitor drops    : "
        << OutputPath(GetFailureDiagnosticDirectory(m_outputDirectory),
                      "flow-drop-reasons.csv")
        << std::endl;
    }
}

} // namespace ns3
