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
#include "../task/task-coordinator.h"

#include "ns3/abort.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
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

void
WriteTransferSummaries(
  const std::vector<TransferSummaryRecord>& summaries,
  const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "transfer-summary.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 transfer summary CSV");
  output
    << "transfer_id,source_node_id,destination_node_id,source_address,"
       "destination_address,source_port,destination_port,declared_size_bytes,"
       "effective_payload_bytes,pacing_mode,"
       "derived_packet_count,final_packet_payload_bytes,arrival_time_ns,"
       "last_send_time_ns,sent_application_bytes,"
       "received_application_bytes,received_packet_count,completion_time_ns,"
       "completion_delay_ns\n";
  for (const auto& summary : summaries)
    {
      output << summary.transferId << ","
             << summary.sourceSatelliteId << ","
             << summary.destinationSatelliteId << ","
             << summary.sourceAddress << ","
             << summary.destinationAddress << ","
             << summary.sourcePort << ","
             << summary.destinationPort << ","
             << summary.declaredSizeBytes << ","
             << summary.payloadBytesPerPacket << ","
             << summary.pacingMode << ","
             << summary.derivedPacketCount << ","
             << summary.finalPacketPayloadBytes << ","
             << summary.arrivalTimeNs << ","
             << summary.lastSendTimeNs << ","
             << summary.sentApplicationBytes << ","
             << summary.receivedApplicationBytes << ","
             << summary.receivedPacketCount << ","
             << summary.completionTimeNs << ","
             << summary.completionDelayNs << "\n";
    }
}

uint64_t
NonNegativeDifference(int64_t endTimeNs,
                      int64_t startTimeNs,
                      const std::string& field,
                      uint64_t taskId)
{
  NS_ABORT_MSG_IF(startTimeNs < 0 || endTimeNs < startTimeNs,
                  "任务时间戳无效: task_id=" << taskId
                    << " field=" << field
                    << " start=" << startTimeNs
                    << " end=" << endTimeNs);
  return static_cast<uint64_t>(endTimeNs - startTimeNs);
}

int64_t
OptionalDifference(int64_t endTimeNs,
                   int64_t startTimeNs,
                   const std::string& field,
                   uint64_t taskId)
{
  if (startTimeNs < 0 || endTimeNs < 0)
    {
      return -1;
    }
  NS_ABORT_MSG_IF(endTimeNs < startTimeNs,
                  "任务时间戳无效: task_id=" << taskId
                    << " field=" << field
                    << " start=" << startTimeNs
                    << " end=" << endTimeNs);
  return endTimeNs - startTimeNs;
}

struct TaskAggregate
{
  uint64_t computeNodeCount = 0;
  uint64_t taskCount = 0;
  uint64_t completedTaskCount = 0;
  uint64_t totalInputBytes = 0;
  uint64_t totalOutputBytes = 0;
  uint64_t totalComputeWorkUnits = 0;
  uint64_t totalCompletionDelayNs = 0;
  uint64_t meanCompletionDelayNs = 0;
  uint64_t maxCompletionDelayNs = 0;
};

TaskAggregate
CollectTaskAggregate(const TaskCoordinator* coordinator)
{
  TaskAggregate aggregate;
  if (coordinator == nullptr)
    {
      return aggregate;
    }

  aggregate.computeNodeCount = coordinator->GetComputeServices().size();
  aggregate.taskCount = coordinator->GetTaskRuntimes().size();
  for (const auto& task : coordinator->GetTaskRuntimes())
    {
      aggregate.totalInputBytes =
        CheckedAdd(aggregate.totalInputBytes,
                   task.definition.inputBytes,
                   "task input bytes");
      aggregate.totalOutputBytes =
        CheckedAdd(aggregate.totalOutputBytes,
                   task.definition.outputBytes,
                   "task output bytes");
      aggregate.totalComputeWorkUnits =
        CheckedAdd(aggregate.totalComputeWorkUnits,
                   task.definition.computeWorkUnits,
                   "task compute work units");
      if (task.state != TASK_COMPLETED)
        {
          continue;
        }
      ++aggregate.completedTaskCount;
      uint64_t completionDelayNs =
        NonNegativeDifference(task.resultTransferCompleteTimeNs,
                              task.definition.arrivalTimeNs,
                              "end_to_end_completion_delay_ns",
                              task.definition.taskId);
      aggregate.totalCompletionDelayNs =
        CheckedAdd(aggregate.totalCompletionDelayNs,
                   completionDelayNs,
                   "task completion delay");
      aggregate.maxCompletionDelayNs =
        std::max(aggregate.maxCompletionDelayNs, completionDelayNs);
    }
  if (aggregate.completedTaskCount > 0)
    {
      aggregate.meanCompletionDelayNs =
        aggregate.totalCompletionDelayNs / aggregate.completedTaskCount;
    }
  return aggregate;
}

void
WriteTaskEvents(const TaskCoordinator& coordinator,
                const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "task-events.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 task events CSV");
  output << "simulation_time_ns,task_id,from_state,to_state,node_id,cause\n";
  for (const auto& event : coordinator.GetTaskEvents())
    {
      output << event.simulationTimeNs << ","
             << event.taskId << ","
             << TaskStateToString(event.fromState) << ","
             << TaskStateToString(event.toState) << ","
             << event.nodeId << ","
             << event.cause << "\n";
    }
}

void
WriteTaskSummaries(const TaskCoordinator& coordinator,
                   const std::string& outputDirectory)
{
  std::map<uint32_t, uint64_t> ratesByNodeId;
  for (const auto& service : coordinator.GetComputeServices())
    {
      NS_ABORT_MSG_IF(
        !ratesByNodeId.insert(
          std::make_pair(
            service->GetNodeId(),
            service->GetComputeRateWorkUnitsPerSecond())).second,
        "重复 ComputeService node_id=" << service->GetNodeId());
    }

  std::ofstream output(OutputPath(outputDirectory, "task-summary.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 task summary CSV");
  output
    << "task_id,source_node_id,compute_node_id,result_node_id,input_bytes,"
       "output_bytes,compute_work_units,compute_rate_work_units_per_second,"
       "input_transfer_id,result_transfer_id,arrival_time_ns,"
       "input_transfer_complete_time_ns,queue_enter_time_ns,"
       "compute_start_time_ns,compute_complete_time_ns,"
       "result_transfer_start_time_ns,result_transfer_complete_time_ns,"
       "input_transfer_delay_ns,queue_delay_ns,compute_service_time_ns,"
       "result_transfer_delay_ns,end_to_end_completion_delay_ns,final_state\n";
  for (const auto& task : coordinator.GetTaskRuntimes())
    {
      auto rate = ratesByNodeId.find(task.definition.computeNodeId);
      NS_ABORT_MSG_IF(rate == ratesByNodeId.end(),
                      "task summary 缺少 ComputeService，task_id="
                        << task.definition.taskId);
      int64_t inputDelayNs =
        OptionalDifference(task.inputTransferCompleteTimeNs,
                           task.definition.arrivalTimeNs,
                           "input_transfer_delay_ns",
                           task.definition.taskId);
      int64_t queueDelayNs =
        OptionalDifference(task.computeStartTimeNs,
                           task.queueEnterTimeNs,
                           "queue_delay_ns",
                           task.definition.taskId);
      int64_t serviceTimeNs =
        OptionalDifference(task.computeCompleteTimeNs,
                           task.computeStartTimeNs,
                           "compute_service_time_ns",
                           task.definition.taskId);
      int64_t resultDelayNs =
        OptionalDifference(task.resultTransferCompleteTimeNs,
                           task.resultTransferStartTimeNs,
                           "result_transfer_delay_ns",
                           task.definition.taskId);
      int64_t completionDelayNs =
        OptionalDifference(task.resultTransferCompleteTimeNs,
                           task.definition.arrivalTimeNs,
                           "end_to_end_completion_delay_ns",
                           task.definition.taskId);

      output << task.definition.taskId << ","
             << task.definition.sourceNodeId << ","
             << task.definition.computeNodeId << ","
             << task.definition.resultNodeId << ","
             << task.definition.inputBytes << ","
             << task.definition.outputBytes << ","
             << task.definition.computeWorkUnits << ","
             << rate->second << ","
             << task.definition.inputTransferId << ","
             << task.definition.resultTransferId << ","
             << task.definition.arrivalTimeNs << ","
             << task.inputTransferCompleteTimeNs << ","
             << task.queueEnterTimeNs << ","
             << task.computeStartTimeNs << ","
             << task.computeCompleteTimeNs << ","
             << task.resultTransferStartTimeNs << ","
             << task.resultTransferCompleteTimeNs << ","
             << inputDelayNs << ","
             << queueDelayNs << ","
             << serviceTimeNs << ","
             << resultDelayNs << ","
             << completionDelayNs << ","
             << TaskStateToString(task.state) << "\n";
    }
}

void
WriteComputeNodeSummaries(const TaskCoordinator& coordinator,
                          double simulationDurationSeconds,
                          const std::string& outputDirectory)
{
  int64_t simulationDurationNs =
    Seconds(simulationDurationSeconds).GetNanoSeconds();
  NS_ABORT_MSG_IF(simulationDurationNs <= 0,
                  "simulationDuration 无法表示为正的纳秒时长");

  std::ofstream output(OutputPath(outputDirectory, "compute-node-summary.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 compute node summary CSV");
  output
    << "node_id,compute_rate_work_units_per_second,enqueued_tasks,"
       "completed_tasks,busy_time_ns,max_queue_length,utilization_percent\n";
  for (const auto& service : coordinator.GetComputeServices())
    {
      NS_ABORT_MSG_IF(
        service->GetBusyTimeNs()
          > static_cast<uint64_t>(simulationDurationNs),
        "ComputeService busy_time_ns 超过 simulationDuration，node_id="
          << service->GetNodeId());
      long double utilizationPercent =
        static_cast<long double>(service->GetBusyTimeNs()) * 100.0L
        / static_cast<long double>(simulationDurationNs);
      output << std::setprecision(15)
             << service->GetNodeId() << ","
             << service->GetComputeRateWorkUnitsPerSecond() << ","
             << service->GetEnqueuedTaskCount() << ","
             << service->GetCompletedTaskCount() << ","
             << service->GetBusyTimeNs() << ","
             << service->GetMaxQueueLength() << ","
             << static_cast<double>(utilizationPercent) << "\n";
    }
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
      WriteTaskEvents(*m_taskCoordinator, m_outputDirectory);
      WriteTaskSummaries(*m_taskCoordinator, m_outputDirectory);
      WriteComputeNodeSummaries(*m_taskCoordinator,
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
