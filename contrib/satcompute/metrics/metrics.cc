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

#include "ns3/ipv4-flow-probe.h"

#include <algorithm>
#include <iostream>

namespace ns3 {

namespace {

uint64_t
GetDropPacketCount(const FlowAggregate& aggregate, uint32_t reasonCode)
{
  std::size_t index = static_cast<std::size_t>(reasonCode);
  return index < aggregate.droppedPacketsByReason.size()
           ? aggregate.droppedPacketsByReason[index]
           : 0;
}

uint64_t
GetCompletedTransferCount(
  const std::vector<TransferSummaryRecord>& transferSummaries)
{
  return static_cast<uint64_t>(
    std::count_if(
      transferSummaries.begin(),
      transferSummaries.end(),
      [](const TransferSummaryRecord& transfer)
      {
        return transfer.transferState == "COMPLETED";
      }));
}

void
PrintRuntimeSummary(
  const FlowAggregate& aggregate,
  const RunMetadata& runMetadata,
  const std::vector<TransferSummaryRecord>& transferSummaries,
  const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
  const TaskCoordinator* taskCoordinator,
  const std::string& outputDirectory,
  bool diagnosticsGenerated)
{
  TaskAggregate tasks = CollectTaskAggregate(taskCoordinator);
  uint64_t completedTransfers =
    GetCompletedTransferCount(transferSummaries);
  bool taskMode = taskCoordinator != nullptr;
  bool taskRunComplete =
    !taskMode || tasks.completedTaskCount == tasks.taskCount;

  std::cout << "[SUMMARY]" << std::endl
            << "  run status          : "
            << (taskRunComplete ? "COMPLETE" : "PARTIAL") << std::endl
            << "  tasks completed     : ";
  if (taskMode)
    {
      std::cout << tasks.completedTaskCount << "/" << tasks.taskCount;
    }
  else
    {
      std::cout << "n/a";
    }
  std::cout << std::endl
            << "  transfers completed : " << completedTransfers
            << "/" << transferSummaries.size() << std::endl
            << "  completion rate     : ";
  if (taskMode && tasks.taskCount > 0)
    {
      std::cout
        << tasks.completedTaskCount * 100.0 / tasks.taskCount << " %";
    }
  else if (!taskMode && !transferSummaries.empty())
    {
      std::cout
        << completedTransfers * 100.0 / transferSummaries.size() << " %";
    }
  else
    {
      std::cout << "n/a";
    }
  std::cout
    << std::endl
    << "  FlowMonitor         : tx=" << aggregate.txPackets
    << " rx=" << aggregate.rxPackets
    << " lost=" << aggregate.lostPackets << std::endl
    << "  QueueDisc drops     : "
    << GetDropPacketCount(aggregate, Ipv4FlowProbe::DROP_QUEUE_DISC)
    << std::endl
    << "  device queue drops  : "
    << GetDropPacketCount(aggregate, Ipv4FlowProbe::DROP_QUEUE)
    << std::endl
    << "  UDP socket drops    : ";
  if (runMetadata.udpSocketDropCollectionEnabled)
    {
      std::cout << udpSocketDropEvents.size();
    }
  else
    {
      std::cout << "n/a";
    }
  std::cout
    << std::endl
    << "  unattributed losses : " << aggregate.UnattributedLostPackets()
    << std::endl
    << "  output root         : " << outputDirectory << std::endl;
  if (diagnosticsGenerated)
    {
      std::cout
        << "  failure diagnostics : "
        << GetFailureDiagnosticDirectory(outputDirectory) << std::endl;
    }
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
  PrintRuntimeSummary(aggregate,
                      m_runMetadata,
                      m_transferSummaries,
                      m_udpSocketDropEvents,
                      m_taskCoordinator,
                      m_outputDirectory,
                      writeDiagnostics || writeFlowDropReasons);
}

} // namespace ns3
