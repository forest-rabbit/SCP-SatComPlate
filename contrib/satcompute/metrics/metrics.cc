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

#include "failure-diagnostics.h"
#include "flow-metrics.h"
#include "task-metrics.h"
#include "transfer-metrics.h"
#include "../task/task-coordinator.h"

#include "ns3/abort.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sys/stat.h>

namespace ns3 {

namespace {

double
SafeDivide(double numerator, double denominator)
{
  return denominator > 0.0 ? numerator / denominator : 0.0;
}

uint64_t
CheckedAdd(uint64_t left, uint64_t right, const std::string& field)
{
  NS_ABORT_MSG_IF(left > std::numeric_limits<uint64_t>::max() - right,
                  "run summary " << field << " 溢出");
  return left + right;
}

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

void
WriteRunSummary(
  const FlowAggregate& aggregate,
  double simulationDurationSeconds,
  double wallClockSeconds,
  const RunMetadata& runMetadata,
  const ApplicationMetrics& applicationMetrics,
  const std::vector<TransferSummaryRecord>& transferSummaries,
  const TaskCoordinator* taskCoordinator,
  const std::string& outputDirectory)
{
  uint64_t declaredBytes = 0;
  uint64_t sentBytes = 0;
  uint64_t receivedBytes = 0;
  uint64_t derivedPackets = 0;
  for (const auto& summary : transferSummaries)
    {
      declaredBytes =
        CheckedAdd(declaredBytes, summary.declaredSizeBytes, "declared bytes");
      sentBytes =
        CheckedAdd(sentBytes, summary.sentApplicationBytes, "sent bytes");
      receivedBytes =
        CheckedAdd(receivedBytes,
                   summary.receivedApplicationBytes,
                   "received bytes");
      derivedPackets =
        CheckedAdd(derivedPackets,
                   summary.derivedPacketCount,
                   "derived packet count");
    }

  TaskAggregate taskAggregate = CollectTaskAggregate(taskCoordinator);
  NS_ABORT_MSG_IF(
    taskCoordinator != nullptr
      && (runMetadata.computeProfilePath.empty()
            || runMetadata.taskTracePath.empty()),
    "task mode run summary 缺少输入路径");

  if (transferSummaries.empty())
    {
      sentBytes = applicationMetrics.sentBytes;
      receivedBytes = applicationMetrics.receivedBytes;
    }
  else
    {
      NS_ABORT_MSG_IF(sentBytes != applicationMetrics.sentBytes
                        || receivedBytes != applicationMetrics.receivedBytes,
                      "NetworkTransfer summary 与应用聚合指标不一致");
    }

  std::ofstream output(OutputPath(outputDirectory, "run-summary.json"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 run summary JSON");
  output << std::setprecision(15)
         << "{\n"
         << "  \"simulation_duration_s\": "
         << simulationDurationSeconds << ",\n"
         << "  \"wall_clock_s\": " << wallClockSeconds << ",\n"
         << "  \"mode\": \"" << runMetadata.mode << "\",\n"
         << "  \"routing_mode\": \"" << runMetadata.routingMode << "\",\n"
         << "  \"ecmp_hash_seed\": " << runMetadata.ecmpHashSeed << ",\n"
         << "  \"isl_mtu_bytes\": " << runMetadata.islMtuBytes << ",\n"
         << "  \"isl_queue_bytes\": " << runMetadata.islQueueBytes << ",\n"
         << "  \"diagnostic_mode\": \"" << runMetadata.diagnosticMode
         << "\",\n"
         << "  \"pacing_mode\": \"" << runMetadata.pacingMode << "\",\n"
         << "  \"transfer_chunk_mode\": \""
         << runMetadata.transferChunkMode << "\",\n"
         << "  \"fixed_payload_bytes\": ";
  if (runMetadata.transferChunkMode == "fixed")
    {
      output << runMetadata.fixedPayloadBytes;
    }
  else
    {
      output << "null";
    }
  output << ",\n"
         << "  \"transfer_count\": " << transferSummaries.size() << ",\n"
         << "  \"declared_application_bytes\": " << declaredBytes << ",\n"
         << "  \"sent_application_bytes\": " << sentBytes << ",\n"
         << "  \"received_application_bytes\": " << receivedBytes << ",\n"
         << "  \"derived_udp_packets\": " << derivedPackets << ",\n"
         << "  \"flow_monitor_tx_packets\": " << aggregate.txPackets << ",\n"
         << "  \"flow_monitor_rx_packets\": " << aggregate.rxPackets << ",\n"
         << "  \"flow_monitor_lost_packets\": "
         << aggregate.lostPackets << ",\n"
         << "  \"compute_profile_path\": ";
  if (runMetadata.computeProfilePath.empty())
    {
      output << "null";
    }
  else
    {
      output << "\"" << runMetadata.computeProfilePath << "\"";
    }
  output << ",\n"
         << "  \"task_trace_path\": ";
  if (runMetadata.taskTracePath.empty())
    {
      output << "null";
    }
  else
    {
      output << "\"" << runMetadata.taskTracePath << "\"";
    }
  output << ",\n"
         << "  \"compute_node_count\": "
         << taskAggregate.computeNodeCount << ",\n"
         << "  \"task_count\": " << taskAggregate.taskCount << ",\n"
         << "  \"completed_task_count\": "
         << taskAggregate.completedTaskCount << ",\n"
         << "  \"task_completion_rate_percent\": "
         << SafeDivide(taskAggregate.completedTaskCount * 100.0,
                       taskAggregate.taskCount)
         << ",\n"
         << "  \"total_input_bytes\": "
         << taskAggregate.totalInputBytes << ",\n"
         << "  \"total_output_bytes\": "
         << taskAggregate.totalOutputBytes << ",\n"
         << "  \"total_compute_work_units\": "
         << taskAggregate.totalComputeWorkUnits << ",\n"
         << "  \"mean_task_completion_delay_ns\": "
         << taskAggregate.meanCompletionDelayNs << ",\n"
         << "  \"max_task_completion_delay_ns\": "
         << taskAggregate.maxCompletionDelayNs << "\n"
         << "}\n";
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
                                 const std::vector<IslDirectedLink>& directedLinks,
                                 const std::vector<IslQueueDropEvent>& queueDropEvents,
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
    m_directedLinks(directedLinks),
    m_queueDropEvents(queueDropEvents),
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
  if (!writeDiagnostics)
    {
      RemoveFailureDiagnosticOutputs(m_outputDirectory);
    }
  PrintNetworkMetrics(aggregate);
  WriteNetworkMetrics(aggregate, m_outputDirectory);
  WriteNetworkFlowDetails(m_monitor,
                          m_transferFlows,
                          m_outputDirectory,
                          taskRunComplete);
  WriteEcmpRouteEvents(m_routeEvents, m_outputDirectory);
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
  if (m_taskCoordinator != nullptr)
    {
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
            << OutputPath(m_outputDirectory, "incomplete-tasks.csv")
            << std::endl
            << "  incomplete transfers : "
            << OutputPath(m_outputDirectory, "incomplete-transfers.csv")
            << std::endl
            << "  ISL queue drops      : "
            << OutputPath(m_outputDirectory, "isl-queue-drops.csv")
            << std::endl
            << "  ISL drop summary     : "
            << OutputPath(m_outputDirectory, "isl-queue-drop-summary.csv")
            << std::endl
            << "  flow/link load       : "
            << OutputPath(m_outputDirectory, "flow-link-concentration.csv")
            << std::endl
            << "  diagnostics          : "
            << OutputPath(m_outputDirectory, "diagnostic-summary.json")
            << std::endl;
        }
    }
}

} // namespace ns3
