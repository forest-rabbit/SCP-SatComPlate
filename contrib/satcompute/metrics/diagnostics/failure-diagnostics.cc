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

// 仅在任务未完成且显式启用诊断时，生成失败定位所需的详细证据。

#include "failure-diagnostics.h"

#include "../../task/task-coordinator.h"

#include "ns3/abort.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>

namespace ns3 {

namespace {

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

std::string
JoinPath(const std::string& directory, const std::string& filename)
{
  if (directory.empty() || directory == ".")
    {
      return filename;
    }
  return directory.back() == '/'
           ? directory + filename
           : directory + "/" + filename;
}

void
EnsureDirectory(const std::string& directory)
{
  if (directory.empty() || directory == ".")
    {
      return;
    }
  errno = 0;
  if (mkdir(directory.c_str(), 0755) == 0)
    {
      return;
    }
  int error = errno;
  if (error == EEXIST)
    {
      struct stat status;
      NS_ABORT_MSG_IF(stat(directory.c_str(), &status) != 0
                        || !S_ISDIR(status.st_mode),
                      "诊断输出路径不是目录: " << directory);
      return;
    }
  NS_ABORT_MSG("无法创建诊断输出目录: " << directory
               << " errno=" << error);
}

void
RemoveKnownFile(const std::string& path)
{
  errno = 0;
  struct stat status;
  int result = lstat(path.c_str(), &status);
  NS_ABORT_MSG_IF(result != 0 && errno != ENOENT,
                  "无法检查旧诊断输出: " << path);
  if (result != 0 || !S_ISREG(status.st_mode))
    {
      return;
    }
  errno = 0;
  NS_ABORT_MSG_IF(unlink(path.c_str()) != 0,
                  "无法清理旧诊断输出: " << path);
}

void
RemoveEmptyDirectory(const std::string& path)
{
  errno = 0;
  int result = rmdir(path.c_str());
  NS_ABORT_MSG_IF(result != 0
                    && errno != ENOENT
                    && errno != ENOTEMPTY
                    && errno != EEXIST,
                  "无法清理空诊断目录: " << path);
}

typedef std::pair<uint32_t, uint32_t> OutputQueueKey;
typedef std::tuple<uint32_t, uint32_t, uint32_t> DirectedLinkKey;
typedef std::tuple<uint32_t, uint32_t, uint16_t, uint32_t>
  UdpReceiverKey;

struct QueueDropSummaryRecord
{
  uint32_t sourceNodeId;
  uint32_t destinationNodeId;
  uint32_t outputInterface;
  uint64_t dropPackets;
  uint64_t dropBytes;
  int64_t firstDropTimeNs;
  int64_t lastDropTimeNs;
};

struct UdpSocketDropSummaryRecord
{
  uint32_t destinationNodeId;
  Ipv4Address destinationAddress;
  uint16_t destinationPort;
  uint32_t receiverRcvBufBytes;
  uint64_t dropPackets;
  uint64_t dropBytes;
  int64_t firstDropTimeNs;
  int64_t lastDropTimeNs;
};

struct FlowLinkSummaryRecord
{
  uint32_t sourceNodeId;
  uint32_t destinationNodeId;
  uint32_t outputInterface;
  uint64_t uniqueTransferCount;
  uint64_t plannedApplicationBytes;
  uint64_t largeTransferCount;
  uint64_t inputTransferCount;
  uint64_t resultTransferCount;
  bool adjacentToComputeNode;
  uint64_t dropPackets;
  uint64_t dropBytes;
};

DirectedLinkKey
MakeDirectedLinkKey(uint32_t sourceNodeId,
                    uint32_t destinationNodeId,
                    uint32_t outputInterface)
{
  return std::make_tuple(sourceNodeId,
                         destinationNodeId,
                         outputInterface);
}

UdpReceiverKey
MakeUdpReceiverKey(const UdpSocketDropEvent& event)
{
  return std::make_tuple(event.destinationSatelliteId,
                         event.destinationAddress.Get(),
                         event.destinationPort,
                         event.receiverRcvBufBytes);
}

std::map<OutputQueueKey, IslDirectedLink>
IndexDirectedLinks(const std::vector<IslDirectedLink>& directedLinks)
{
  std::map<OutputQueueKey, IslDirectedLink> linksByOutputQueue;
  for (const auto& link : directedLinks)
    {
      OutputQueueKey key =
        std::make_pair(link.sourceNodeId, link.outputInterface);
      NS_ABORT_MSG_IF(
        !linksByOutputQueue.insert(std::make_pair(key, link)).second,
        "重复 ISL directed queue 映射: source="
          << link.sourceNodeId << " interface=" << link.outputInterface);
    }
  return linksByOutputQueue;
}

std::vector<QueueDropSummaryRecord>
CollectQueueDropSummaries(
  const std::vector<IslDirectedLink>& directedLinks,
  const std::vector<IslQueueDropEvent>& queueDropEvents)
{
  std::map<OutputQueueKey, IslDirectedLink> linksByOutputQueue =
    IndexDirectedLinks(directedLinks);
  std::map<DirectedLinkKey, QueueDropSummaryRecord> summariesByLink;

  for (const auto& event : queueDropEvents)
    {
      OutputQueueKey outputQueue =
        std::make_pair(event.sourceNodeId, event.outputInterface);
      auto mappedLink = linksByOutputQueue.find(outputQueue);
      NS_ABORT_MSG_IF(
        mappedLink == linksByOutputQueue.end()
          || mappedLink->second.destinationNodeId != event.destinationNodeId,
        "ISL queue drop 无法映射到有向链路: source="
          << event.sourceNodeId << " destination="
          << event.destinationNodeId << " interface="
          << event.outputInterface);
      NS_ABORT_MSG_IF(event.packetSizeBytes == 0,
                      "ISL queue drop packet size 不能为 0");

      DirectedLinkKey key =
        MakeDirectedLinkKey(event.sourceNodeId,
                            event.destinationNodeId,
                            event.outputInterface);
      auto insertion = summariesByLink.insert(
        std::make_pair(
          key,
          QueueDropSummaryRecord{
            event.sourceNodeId,
            event.destinationNodeId,
            event.outputInterface,
            0,
            0,
            event.simulationTimeNs,
            event.simulationTimeNs
          }));
      QueueDropSummaryRecord& summary = insertion.first->second;
      ++summary.dropPackets;
      summary.dropBytes =
        CheckedAdd(summary.dropBytes,
                   event.packetSizeBytes,
                   "ISL queue drop bytes");
      summary.lastDropTimeNs = event.simulationTimeNs;
      NS_ABORT_MSG_IF(
        event.cumulativeDropPackets != summary.dropPackets
          || event.cumulativeDropBytes != summary.dropBytes,
        "ISL queue drop cumulative totals 不一致: source="
          << event.sourceNodeId << " destination="
          << event.destinationNodeId << " interface="
          << event.outputInterface);
    }

  std::vector<QueueDropSummaryRecord> summaries;
  summaries.reserve(summariesByLink.size());
  for (const auto& item : summariesByLink)
    {
      summaries.push_back(item.second);
    }
  std::sort(
    summaries.begin(),
    summaries.end(),
    [](const QueueDropSummaryRecord& left,
       const QueueDropSummaryRecord& right) {
      if (left.dropBytes != right.dropBytes)
        {
          return left.dropBytes > right.dropBytes;
        }
      if (left.dropPackets != right.dropPackets)
        {
          return left.dropPackets > right.dropPackets;
        }
      return std::make_tuple(left.sourceNodeId,
                             left.destinationNodeId,
                             left.outputInterface)
             < std::make_tuple(right.sourceNodeId,
                               right.destinationNodeId,
                               right.outputInterface);
    });
  return summaries;
}

void
WriteIslQueueDrops(
  const std::vector<IslQueueDropEvent>& queueDropEvents,
  const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "isl-queue-drops.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 ISL queue drops CSV");
  output
    << "simulation_time_ns,source_node_id,destination_node_id,"
       "output_interface,packet_size_bytes,cumulative_drop_packets,"
       "cumulative_drop_bytes\n";
  for (const auto& event : queueDropEvents)
    {
      output << event.simulationTimeNs << ","
             << event.sourceNodeId << ","
             << event.destinationNodeId << ","
             << event.outputInterface << ","
             << event.packetSizeBytes << ","
             << event.cumulativeDropPackets << ","
             << event.cumulativeDropBytes << "\n";
    }
}

void
WriteIslQueueDropSummaries(
  const std::vector<QueueDropSummaryRecord>& summaries,
  const std::string& outputDirectory)
{
  std::ofstream output(
    OutputPath(outputDirectory, "isl-queue-drop-summary.csv"),
    std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(),
                  "无法写入 ISL queue drop summary CSV");
  output
    << "source_node_id,destination_node_id,output_interface,"
       "drop_packets,drop_bytes,first_drop_time_ns,last_drop_time_ns\n";
  for (const auto& summary : summaries)
    {
      output << summary.sourceNodeId << ","
             << summary.destinationNodeId << ","
             << summary.outputInterface << ","
             << summary.dropPackets << ","
             << summary.dropBytes << ","
             << summary.firstDropTimeNs << ","
             << summary.lastDropTimeNs << "\n";
    }
}

std::vector<UdpSocketDropSummaryRecord>
CollectUdpSocketDropSummaries(
  const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
  const RunMetadata& runMetadata)
{
  std::map<UdpReceiverKey, UdpSocketDropSummaryRecord> summariesByReceiver;
  int64_t previousTimeNs = -1;
  for (const auto& event : udpSocketDropEvents)
    {
      NS_ABORT_MSG_IF(event.simulationTimeNs < previousTimeNs,
                      "UDP socket Drop 事件未按仿真时间排序");
      NS_ABORT_MSG_IF(event.destinationAddress == Ipv4Address::GetAny()
                        || event.destinationPort == 0
                        || event.packetSizeBytes == 0,
                      "UDP socket Drop 事件包含无效接收端或 packet size");
      NS_ABORT_MSG_IF(
        event.receiverRcvBufBytes != runMetadata.receiverRcvBufBytes,
        "UDP socket Drop 事件的 RcvBufSize 与运行配置不一致");
      previousTimeNs = event.simulationTimeNs;

      UdpReceiverKey key = MakeUdpReceiverKey(event);
      auto insertion = summariesByReceiver.insert(
        std::make_pair(
          key,
          UdpSocketDropSummaryRecord{
            event.destinationSatelliteId,
            event.destinationAddress,
            event.destinationPort,
            event.receiverRcvBufBytes,
            0,
            0,
            event.simulationTimeNs,
            event.simulationTimeNs
          }));
      UdpSocketDropSummaryRecord& summary = insertion.first->second;
      ++summary.dropPackets;
      summary.dropBytes =
        CheckedAdd(summary.dropBytes,
                   event.packetSizeBytes,
                   "UDP socket drop bytes");
      summary.lastDropTimeNs = event.simulationTimeNs;
      NS_ABORT_MSG_IF(
        event.cumulativeDropPackets != summary.dropPackets
          || event.cumulativeDropBytes != summary.dropBytes,
        "UDP socket Drop cumulative totals 不一致: destination="
          << event.destinationSatelliteId << " address="
          << event.destinationAddress << " port="
          << event.destinationPort);
    }

  std::vector<UdpSocketDropSummaryRecord> summaries;
  summaries.reserve(summariesByReceiver.size());
  for (const auto& item : summariesByReceiver)
    {
      summaries.push_back(item.second);
    }
  std::sort(
    summaries.begin(),
    summaries.end(),
    [](const UdpSocketDropSummaryRecord& left,
       const UdpSocketDropSummaryRecord& right) {
      if (left.dropBytes != right.dropBytes)
        {
          return left.dropBytes > right.dropBytes;
        }
      if (left.dropPackets != right.dropPackets)
        {
          return left.dropPackets > right.dropPackets;
        }
      return std::make_tuple(left.destinationNodeId,
                             left.destinationAddress.Get(),
                             left.destinationPort,
                             left.receiverRcvBufBytes)
             < std::make_tuple(right.destinationNodeId,
                               right.destinationAddress.Get(),
                               right.destinationPort,
                               right.receiverRcvBufBytes);
    });
  return summaries;
}

void
WriteUdpSocketDrops(
  const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
  const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "udp-socket-drops.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 UDP socket drops CSV");
  output
    << "simulation_time_ns,destination_node_id,destination_address,"
       "destination_port,packet_size_bytes,cumulative_drop_packets,"
       "cumulative_drop_bytes,receiver_rcv_buf_bytes\n";
  for (const auto& event : udpSocketDropEvents)
    {
      output << event.simulationTimeNs << ","
             << event.destinationSatelliteId << ","
             << event.destinationAddress << ","
             << event.destinationPort << ","
             << event.packetSizeBytes << ","
             << event.cumulativeDropPackets << ","
             << event.cumulativeDropBytes << ","
             << event.receiverRcvBufBytes << "\n";
    }
}

void
WriteUdpSocketDropSummaries(
  const std::vector<UdpSocketDropSummaryRecord>& summaries,
  const std::string& outputDirectory)
{
  std::ofstream output(
    OutputPath(outputDirectory, "udp-socket-drop-summary.csv"),
    std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(),
                  "无法写入 UDP socket drop summary CSV");
  output
    << "destination_node_id,destination_address,destination_port,"
       "receiver_rcv_buf_bytes,drop_packets,drop_bytes,"
       "first_drop_time_ns,last_drop_time_ns\n";
  for (const auto& summary : summaries)
    {
      output << summary.destinationNodeId << ","
             << summary.destinationAddress << ","
             << summary.destinationPort << ","
             << summary.receiverRcvBufBytes << ","
             << summary.dropPackets << ","
             << summary.dropBytes << ","
             << summary.firstDropTimeNs << ","
             << summary.lastDropTimeNs << "\n";
    }
}

std::vector<FlowLinkSummaryRecord>
CollectFlowLinkSummaries(
  const std::vector<TransferFlowMetadata>& transferFlows,
  const std::vector<EcmpRouteDecisionEvent>& routeEvents,
  const std::vector<IslDirectedLink>& directedLinks,
  const std::vector<QueueDropSummaryRecord>& queueDropSummaries,
  const TaskCoordinator* coordinator)
{
  struct FlowLinkAccumulator
  {
    explicit FlowLinkAccumulator(const IslDirectedLink& directedLink)
      : link(directedLink)
    {
    }

    IslDirectedLink link;
    std::set<uint64_t> transferIds;
    uint64_t plannedApplicationBytes = 0;
    uint64_t largeTransferCount = 0;
    uint64_t inputTransferCount = 0;
    uint64_t resultTransferCount = 0;
    uint64_t dropPackets = 0;
    uint64_t dropBytes = 0;
  };

  std::map<OutputQueueKey, IslDirectedLink> linksByOutputQueue =
    IndexDirectedLinks(directedLinks);
  std::map<EcmpFlowKey, TransferFlowMetadata> metadataByFlow;
  for (const auto& metadata : transferFlows)
    {
      EcmpFlowKey key;
      key.sourceAddress = metadata.sourceAddress;
      key.destinationAddress = metadata.destinationAddress;
      key.protocol = metadata.protocol;
      key.sourcePort = metadata.sourcePort;
      key.destinationPort = metadata.destinationPort;
      NS_ABORT_MSG_IF(
        !metadataByFlow.insert(std::make_pair(key, metadata)).second,
        "NetworkTransfer metadata 包含重复 ECMP flow key，transfer_id="
          << metadata.transferId);
    }

  std::set<uint64_t> inputTransferIds;
  std::set<uint64_t> resultTransferIds;
  std::set<uint32_t> computeNodeIds;
  if (coordinator != nullptr)
    {
      for (const auto& task : coordinator->GetTaskRuntimes())
        {
          inputTransferIds.insert(task.definition.inputTransferId);
          resultTransferIds.insert(task.definition.resultTransferId);
        }
      for (const auto& service : coordinator->GetComputeServices())
        {
          computeNodeIds.insert(service->GetNodeId());
        }
    }

  std::map<DirectedLinkKey, FlowLinkAccumulator> accumulators;
  for (const auto& event : routeEvents)
    {
      if (!event.hasFiveTuple || event.selectedOutputInterface < 0)
        {
          continue;
        }
      auto metadata = metadataByFlow.find(event.flowKey);
      if (metadata == metadataByFlow.end())
        {
          continue;
        }

      uint32_t outputInterface =
        static_cast<uint32_t>(event.selectedOutputInterface);
      OutputQueueKey outputQueue =
        std::make_pair(event.nodeId, outputInterface);
      auto mappedLink = linksByOutputQueue.find(outputQueue);
      NS_ABORT_MSG_IF(
        mappedLink == linksByOutputQueue.end(),
        "ECMP route event 无法映射到有向 ISL: node="
          << event.nodeId << " interface=" << outputInterface
          << " transfer_id=" << metadata->second.transferId);
      const IslDirectedLink& link = mappedLink->second;
      DirectedLinkKey linkKey =
        MakeDirectedLinkKey(link.sourceNodeId,
                            link.destinationNodeId,
                            link.outputInterface);
      auto insertion = accumulators.insert(
        std::make_pair(linkKey, FlowLinkAccumulator{link}));
      FlowLinkAccumulator& accumulator = insertion.first->second;
      if (!accumulator.transferIds.insert(metadata->second.transferId).second)
        {
          continue;
        }

      accumulator.plannedApplicationBytes =
        CheckedAdd(accumulator.plannedApplicationBytes,
                   metadata->second.plannedApplicationPayloadBytes,
                   "flow-link planned application bytes");
      if (metadata->second.plannedApplicationPayloadBytes > 64ull * 1024 * 1024)
        {
          ++accumulator.largeTransferCount;
        }
      if (inputTransferIds.find(metadata->second.transferId)
          != inputTransferIds.end())
        {
          ++accumulator.inputTransferCount;
        }
      else if (resultTransferIds.find(metadata->second.transferId)
               != resultTransferIds.end())
        {
          ++accumulator.resultTransferCount;
        }
      else
        {
          NS_ABORT_MSG_IF(
            coordinator != nullptr,
            "任务 transfer 无法区分 INPUT/RESULT，transfer_id="
              << metadata->second.transferId);
        }
    }

  for (const auto& drop : queueDropSummaries)
    {
      DirectedLinkKey linkKey =
        MakeDirectedLinkKey(drop.sourceNodeId,
                            drop.destinationNodeId,
                            drop.outputInterface);
      auto insertion = accumulators.insert(
        std::make_pair(
          linkKey,
          FlowLinkAccumulator{
            {drop.sourceNodeId,
             drop.destinationNodeId,
             drop.outputInterface}
          }));
      insertion.first->second.dropPackets = drop.dropPackets;
      insertion.first->second.dropBytes = drop.dropBytes;
    }

  std::vector<FlowLinkSummaryRecord> summaries;
  summaries.reserve(accumulators.size());
  for (const auto& item : accumulators)
    {
      const FlowLinkAccumulator& accumulator = item.second;
      summaries.push_back(
        {accumulator.link.sourceNodeId,
         accumulator.link.destinationNodeId,
         accumulator.link.outputInterface,
         accumulator.transferIds.size(),
         accumulator.plannedApplicationBytes,
         accumulator.largeTransferCount,
         accumulator.inputTransferCount,
         accumulator.resultTransferCount,
         computeNodeIds.find(accumulator.link.sourceNodeId)
               != computeNodeIds.end()
           || computeNodeIds.find(accumulator.link.destinationNodeId)
                != computeNodeIds.end(),
         accumulator.dropPackets,
         accumulator.dropBytes});
    }
  std::sort(
    summaries.begin(),
    summaries.end(),
    [](const FlowLinkSummaryRecord& left,
       const FlowLinkSummaryRecord& right) {
      if (left.plannedApplicationBytes != right.plannedApplicationBytes)
        {
          return left.plannedApplicationBytes
                 > right.plannedApplicationBytes;
        }
      if (left.uniqueTransferCount != right.uniqueTransferCount)
        {
          return left.uniqueTransferCount > right.uniqueTransferCount;
        }
      return std::make_tuple(left.sourceNodeId,
                             left.destinationNodeId,
                             left.outputInterface)
             < std::make_tuple(right.sourceNodeId,
                               right.destinationNodeId,
                               right.outputInterface);
    });
  return summaries;
}

void
WriteFlowLinkSummaries(
  const std::vector<FlowLinkSummaryRecord>& summaries,
  const std::string& outputDirectory)
{
  std::ofstream output(
    OutputPath(outputDirectory, "flow-link-concentration.csv"),
    std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(),
                  "无法写入 flow-link concentration CSV");
  output
    << "source_node_id,destination_node_id,output_interface,"
       "unique_transfer_count,planned_application_bytes,"
       "large_transfer_count,input_transfer_count,result_transfer_count,"
       "adjacent_to_compute_node,drop_packets,drop_bytes\n";
  for (const auto& summary : summaries)
    {
      output << summary.sourceNodeId << ","
             << summary.destinationNodeId << ","
             << summary.outputInterface << ","
             << summary.uniqueTransferCount << ","
             << summary.plannedApplicationBytes << ","
             << summary.largeTransferCount << ","
             << summary.inputTransferCount << ","
             << summary.resultTransferCount << ","
             << (summary.adjacentToComputeNode ? 1 : 0) << ","
             << summary.dropPackets << ","
             << summary.dropBytes << "\n";
    }
}

void
WriteIncompleteTasks(const TaskCoordinator& coordinator,
                     const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "incomplete-tasks.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 incomplete tasks CSV");
  output
    << "task_id,state,source_node_id,compute_node_id,result_node_id,"
       "input_transfer_id,result_transfer_id,arrival_time_ns,"
       "last_transition_time_ns,input_transfer_complete_time_ns,"
       "queue_enter_time_ns,compute_start_time_ns,compute_complete_time_ns,"
       "result_transfer_complete_time_ns\n";
  for (const auto& task : coordinator.GetTaskRuntimes())
    {
      if (task.state == TASK_COMPLETED)
        {
          continue;
        }
      output << task.definition.taskId << ","
             << TaskStateToString(task.state) << ","
             << task.definition.sourceNodeId << ","
             << task.definition.computeNodeId << ","
             << task.definition.resultNodeId << ","
             << task.definition.inputTransferId << ","
             << task.definition.resultTransferId << ","
             << task.definition.arrivalTimeNs << ","
             << task.lastTransitionTimeNs << ","
             << task.inputTransferCompleteTimeNs << ","
             << task.queueEnterTimeNs << ","
             << task.computeStartTimeNs << ","
             << task.computeCompleteTimeNs << ","
             << task.resultTransferCompleteTimeNs << "\n";
    }
}

void
WriteIncompleteTransfers(
  const std::vector<TransferSummaryRecord>& summaries,
  const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "incomplete-transfers.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 incomplete transfers CSV");
  output
    << "transfer_id,transfer_state,source_node_id,destination_node_id,"
       "source_address,destination_address,source_port,destination_port,"
       "declared_size_bytes,payload_bytes_per_packet,derived_packet_count,"
       "sent_application_bytes,sent_packet_count,"
       "received_application_bytes,received_packet_count,"
       "missing_application_bytes,missing_packet_count_lower_bound,"
       "arrival_time_ns,last_send_time_ns,completion_time_ns\n";
  for (const auto& summary : summaries)
    {
      if (summary.transferState == "COMPLETED")
        {
          continue;
        }
      NS_ABORT_MSG_IF(
        summary.receivedApplicationBytes > summary.declaredSizeBytes
          || summary.receivedPacketCount > summary.derivedPacketCount,
        "incomplete transfer 接收量超过声明值，transfer_id="
          << summary.transferId);
      output << summary.transferId << ","
             << summary.transferState << ","
             << summary.sourceSatelliteId << ","
             << summary.destinationSatelliteId << ","
             << summary.sourceAddress << ","
             << summary.destinationAddress << ","
             << summary.sourcePort << ","
             << summary.destinationPort << ","
             << summary.declaredSizeBytes << ","
             << summary.payloadBytesPerPacket << ","
             << summary.derivedPacketCount << ","
             << summary.sentApplicationBytes << ","
             << summary.sentPacketCount << ","
             << summary.receivedApplicationBytes << ","
             << summary.receivedPacketCount << ","
             << summary.declaredSizeBytes
                  - summary.receivedApplicationBytes << ","
             << summary.derivedPacketCount
                  - summary.receivedPacketCount << ","
             << summary.arrivalTimeNs << ","
             << summary.lastSendTimeNs << ","
             << summary.completionTimeNs << "\n";
    }
}

void
WriteDiagnosticSummary(
  const FlowAggregate& aggregate,
  double simulationDurationSeconds,
  const RunMetadata& runMetadata,
  const std::vector<TransferSummaryRecord>& transferSummaries,
  const std::vector<QueueDropSummaryRecord>& queueDropSummaries,
  const std::vector<UdpSocketDropSummaryRecord>& udpSocketDropSummaries,
  const std::vector<FlowLinkSummaryRecord>& flowLinkSummaries,
  const TaskCoordinator& coordinator,
  const std::string& outputDirectory)
{
  std::map<std::string, uint64_t> tasksByState;
  uint64_t completedTasks = 0;
  for (const auto& task : coordinator.GetTaskRuntimes())
    {
      ++tasksByState[TaskStateToString(task.state)];
      if (task.state == TASK_COMPLETED)
        {
          ++completedTasks;
        }
    }

  std::map<std::string, uint64_t> transfersByState;
  uint64_t completedTransfers = 0;
  for (const auto& transfer : transferSummaries)
    {
      ++transfersByState[transfer.transferState];
      if (transfer.transferState == "COMPLETED")
        {
          ++completedTransfers;
        }
    }

  uint64_t queueDropPackets = 0;
  uint64_t queueDropBytes = 0;
  for (const auto& drop : queueDropSummaries)
    {
      queueDropPackets =
        CheckedAdd(queueDropPackets,
                   drop.dropPackets,
                   "queue drop packets");
      queueDropBytes =
        CheckedAdd(queueDropBytes,
                   drop.dropBytes,
                   "queue drop bytes");
    }

  uint64_t udpSocketDropPackets = 0;
  uint64_t udpSocketDropBytes = 0;
  for (const auto& drop : udpSocketDropSummaries)
    {
      udpSocketDropPackets =
        CheckedAdd(udpSocketDropPackets,
                   drop.dropPackets,
                   "UDP socket drop packets");
      udpSocketDropBytes =
        CheckedAdd(udpSocketDropBytes,
                   drop.dropBytes,
                   "UDP socket drop bytes");
    }

  std::ofstream output(OutputPath(outputDirectory, "diagnostic-summary.json"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 diagnostic summary JSON");
  output << std::setprecision(15)
         << "{\n"
         << "  \"run_status\": \"INCOMPLETE\",\n"
         << "  \"simulation_duration_s\": "
         << simulationDurationSeconds << ",\n"
         << "  \"task_count\": "
         << coordinator.GetTaskRuntimes().size() << ",\n"
         << "  \"completed_task_count\": " << completedTasks << ",\n"
         << "  \"incomplete_task_count\": "
         << coordinator.GetTaskRuntimes().size() - completedTasks << ",\n"
         << "  \"tasks_by_state\": {\n"
         << "    \"PENDING\": " << tasksByState["PENDING"] << ",\n"
         << "    \"INPUT_TRANSFERRING\": "
         << tasksByState["INPUT_TRANSFERRING"] << ",\n"
         << "    \"QUEUED\": " << tasksByState["QUEUED"] << ",\n"
         << "    \"RUNNING\": " << tasksByState["RUNNING"] << ",\n"
         << "    \"RESULT_TRANSFERRING\": "
         << tasksByState["RESULT_TRANSFERRING"] << ",\n"
         << "    \"COMPLETED\": " << tasksByState["COMPLETED"] << "\n"
         << "  },\n"
         << "  \"transfer_count\": " << transferSummaries.size() << ",\n"
         << "  \"registered_transfer_count\": "
         << transfersByState["REGISTERED"] << ",\n"
         << "  \"started_transfer_count\": "
         << transfersByState["STARTED"] << ",\n"
         << "  \"completed_transfer_count\": "
         << completedTransfers << ",\n"
         << "  \"incomplete_transfer_count\": "
         << transferSummaries.size() - completedTransfers << ",\n"
         << "  \"flowmonitor_tx_packets\": "
         << aggregate.txPackets << ",\n"
         << "  \"flowmonitor_rx_packets\": "
         << aggregate.rxPackets << ",\n"
         << "  \"flowmonitor_lost_packets\": "
         << aggregate.lostPackets << ",\n"
         << "  \"queue_drop_packets\": " << queueDropPackets << ",\n"
         << "  \"queue_drop_bytes\": " << queueDropBytes << ",\n"
         << "  \"dropped_directed_link_count\": "
         << queueDropSummaries.size() << ",\n"
         << "  \"receiver_rcv_buf_bytes\": "
         << runMetadata.receiverRcvBufBytes << ",\n"
         << "  \"udp_socket_drop_packets\": "
         << udpSocketDropPackets << ",\n"
         << "  \"udp_socket_drop_bytes\": "
         << udpSocketDropBytes << ",\n"
         << "  \"udp_socket_dropped_receiver_count\": "
         << udpSocketDropSummaries.size() << ",\n"
         << "  \"top_dropped_links\": [";
  uint32_t droppedLinkLimit =
    std::min<uint32_t>(10, queueDropSummaries.size());
  for (uint32_t index = 0; index < droppedLinkLimit; ++index)
    {
      const QueueDropSummaryRecord& drop = queueDropSummaries[index];
      output << (index == 0 ? "\n" : ",\n")
             << "    {\"source_node_id\": " << drop.sourceNodeId
             << ", \"destination_node_id\": " << drop.destinationNodeId
             << ", \"output_interface\": " << drop.outputInterface
             << ", \"drop_packets\": " << drop.dropPackets
             << ", \"drop_bytes\": " << drop.dropBytes
             << ", \"first_drop_time_ns\": " << drop.firstDropTimeNs
             << ", \"last_drop_time_ns\": " << drop.lastDropTimeNs
             << "}";
    }
  output << (droppedLinkLimit == 0 ? "" : "\n  ")
         << "],\n"
         << "  \"top_planned_load_links\": [";
  uint32_t plannedLinkCount = 0;
  for (const auto& link : flowLinkSummaries)
    {
      if (plannedLinkCount == 10 || link.plannedApplicationBytes == 0)
        {
          break;
        }
      output << (plannedLinkCount == 0 ? "\n" : ",\n")
             << "    {\"source_node_id\": " << link.sourceNodeId
             << ", \"destination_node_id\": " << link.destinationNodeId
             << ", \"output_interface\": " << link.outputInterface
             << ", \"unique_transfer_count\": "
             << link.uniqueTransferCount
             << ", \"planned_application_bytes\": "
             << link.plannedApplicationBytes
             << ", \"large_transfer_count\": "
             << link.largeTransferCount
             << ", \"adjacent_to_compute_node\": "
             << (link.adjacentToComputeNode ? "true" : "false")
             << ", \"drop_packets\": " << link.dropPackets
             << ", \"drop_bytes\": " << link.dropBytes
             << "}";
      ++plannedLinkCount;
    }
  output << (plannedLinkCount == 0 ? "" : "\n  ")
         << "],\n"
         << "  \"compute_node_count\": "
         << coordinator.GetComputeServices().size() << ",\n"
         << "  \"queue_bytes_per_device\": "
         << runMetadata.islQueueBytes << ",\n"
         << "  \"ecmp_hash_seed\": " << runMetadata.ecmpHashSeed << "\n"
         << "}\n";
}

} // namespace

std::string
GetFailureDiagnosticDirectory(const std::string& outputDirectory)
{
  return JoinPath(JoinPath(outputDirectory, "diagnostics"), "failure");
}

void
PrepareFailureDiagnosticDirectory(const std::string& outputDirectory)
{
  EnsureDirectory(outputDirectory);
  std::string diagnosticsDirectory =
    JoinPath(outputDirectory, "diagnostics");
  EnsureDirectory(diagnosticsDirectory);
  EnsureDirectory(GetFailureDiagnosticDirectory(outputDirectory));
}

void
RemoveFailureDiagnosticOutputs(const std::string& outputDirectory)
{
  const std::vector<std::string> filenames = {
    "incomplete-tasks.csv",
    "incomplete-transfers.csv",
    "isl-queue-drops.csv",
    "isl-queue-drop-summary.csv",
    "udp-socket-drops.csv",
    "udp-socket-drop-summary.csv",
    "flow-link-concentration.csv",
    "flow-drop-reasons.csv",
    "diagnostic-summary.json"
  };
  std::string failureDirectory =
    GetFailureDiagnosticDirectory(outputDirectory);
  for (const auto& filename : filenames)
    {
      RemoveKnownFile(JoinPath(outputDirectory, filename));
      RemoveKnownFile(JoinPath(failureDirectory, filename));
    }
  RemoveEmptyDirectory(failureDirectory);
  RemoveEmptyDirectory(JoinPath(outputDirectory, "diagnostics"));
}

void
WriteFailureDiagnostics(
  const FlowAggregate& aggregate,
  double simulationDurationSeconds,
  const RunMetadata& runMetadata,
  const std::vector<TransferFlowMetadata>& transferFlows,
  const std::vector<TransferSummaryRecord>& transferSummaries,
  const std::vector<EcmpRouteDecisionEvent>& routeEvents,
  const std::vector<IslDirectedLink>& directedLinks,
  const std::vector<IslQueueDropEvent>& queueDropEvents,
  const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
  const TaskCoordinator& coordinator,
  const std::string& outputDirectory)
{
  NS_ABORT_MSG_IF(!runMetadata.udpSocketDropCollectionEnabled,
                  "失败诊断要求启用 UDP socket Drop 采集");
  PrepareFailureDiagnosticDirectory(outputDirectory);
  std::string failureDirectory =
    GetFailureDiagnosticDirectory(outputDirectory);
  std::vector<QueueDropSummaryRecord> queueDropSummaries =
    CollectQueueDropSummaries(directedLinks, queueDropEvents);
  std::vector<UdpSocketDropSummaryRecord> udpSocketDropSummaries =
    CollectUdpSocketDropSummaries(udpSocketDropEvents, runMetadata);
  std::vector<FlowLinkSummaryRecord> flowLinkSummaries =
    CollectFlowLinkSummaries(transferFlows,
                             routeEvents,
                             directedLinks,
                             queueDropSummaries,
                             &coordinator);

  WriteIncompleteTasks(coordinator, failureDirectory);
  WriteIncompleteTransfers(transferSummaries, failureDirectory);
  WriteIslQueueDrops(queueDropEvents, failureDirectory);
  WriteIslQueueDropSummaries(queueDropSummaries, failureDirectory);
  WriteUdpSocketDrops(udpSocketDropEvents, failureDirectory);
  WriteUdpSocketDropSummaries(udpSocketDropSummaries, failureDirectory);
  WriteFlowLinkSummaries(flowLinkSummaries, failureDirectory);
  WriteDiagnosticSummary(aggregate,
                         simulationDurationSeconds,
                         runMetadata,
                         transferSummaries,
                         queueDropSummaries,
                         udpSocketDropSummaries,
                         flowLinkSummaries,
                         coordinator,
                         failureDirectory);
}

} // namespace ns3
