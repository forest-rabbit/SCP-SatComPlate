// 汇总 FlowMonitor、NetworkTransfer 与 ECMP 数据并写出结构化指标。

#include "metrics.h"

#include "../task/task-coordinator.h"

#include "ns3/abort.h"
#include "ns3/ipv4-flow-classifier.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sys/stat.h>
#include <tuple>

namespace ns3 {

namespace {

FlowMonitorHelper g_flowMonitorHelper;

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

struct FlowAggregate
{
  uint64_t txPackets = 0;
  uint64_t rxPackets = 0;
  uint64_t lostPackets = 0;
  uint64_t txBytes = 0;
  uint64_t rxBytes = 0;
  uint64_t jitterSamples = 0;
  double delaySumSeconds = 0.0;
  double jitterSumSeconds = 0.0;
  double measurementStartSeconds = 0.0;
  double measurementEndSeconds = 0.0;
  bool hasMeasurement = false;

  void Add(const FlowMonitor::FlowStats& flow)
  {
    txPackets += flow.txPackets;
    rxPackets += flow.rxPackets;
    lostPackets += flow.lostPackets;
    txBytes += flow.txBytes;
    rxBytes += flow.rxBytes;
    delaySumSeconds += flow.delaySum.GetSeconds();
    jitterSumSeconds += flow.jitterSum.GetSeconds();
    jitterSamples += flow.rxPackets > 0 ? flow.rxPackets - 1 : 0;

    if (flow.txPackets == 0)
      {
        return;
      }

    double start = flow.timeFirstTxPacket.GetSeconds();
    double end =
      std::max(flow.timeLastTxPacket.GetSeconds(), flow.timeLastRxPacket.GetSeconds());
    if (!hasMeasurement)
      {
        measurementStartSeconds = start;
        measurementEndSeconds = end;
        hasMeasurement = true;
        return;
      }
    measurementStartSeconds = std::min(measurementStartSeconds, start);
    measurementEndSeconds = std::max(measurementEndSeconds, end);
  }

  double MeasurementDurationSeconds() const
  {
    return hasMeasurement
             ? std::max(0.0, measurementEndSeconds - measurementStartSeconds)
             : 0.0;
  }
};

typedef std::pair<uint32_t, uint32_t> OutputQueueKey;
typedef std::tuple<uint32_t, uint32_t, uint32_t> DirectedLinkKey;

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
PrintNetworkMetrics(const FlowAggregate& metrics)
{
  double duration = metrics.MeasurementDurationSeconds();
  std::cout << "[METRICS] IPv4 network flows" << std::endl
            << "  Tx Packets          : " << metrics.txPackets << std::endl
            << "  Rx Packets          : " << metrics.rxPackets << std::endl
            << "  Lost Packets        : " << metrics.lostPackets << std::endl
            << "  Tx Bytes            : " << metrics.txBytes << std::endl
            << "  Rx Bytes            : " << metrics.rxBytes << std::endl
            << "  Measurement Duration: " << duration << " s" << std::endl
            << "  Mean Delay          : "
            << SafeDivide(metrics.delaySumSeconds, metrics.rxPackets) * 1000.0
            << " ms" << std::endl
            << "  Mean Jitter         : "
            << SafeDivide(metrics.jitterSumSeconds, metrics.jitterSamples) * 1000.0
            << " ms" << std::endl
            << "  Throughput          : "
            << SafeDivide(metrics.rxBytes * 8.0, duration * 1000000.0)
            << " Mbps" << std::endl
            << "  Loss Ratio          : "
            << SafeDivide(metrics.lostPackets * 100.0, metrics.txPackets)
            << " %" << std::endl
            << std::endl;
}

void
WriteNetworkMetrics(const FlowAggregate& metrics, const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "network-flow-metrics.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入网络流指标 CSV");

  double duration = metrics.MeasurementDurationSeconds();
  output << std::setprecision(15)
         << "tx_packets,rx_packets,lost_packets,tx_bytes,rx_bytes,"
            "measurement_start_s,measurement_end_s,measurement_duration_s,"
            "mean_delay_ms,mean_jitter_ms,throughput_mbps,loss_ratio_percent\n"
         << metrics.txPackets << ","
         << metrics.rxPackets << ","
         << metrics.lostPackets << ","
         << metrics.txBytes << ","
         << metrics.rxBytes << ","
         << metrics.measurementStartSeconds << ","
         << metrics.measurementEndSeconds << ","
         << duration << ","
         << SafeDivide(metrics.delaySumSeconds, metrics.rxPackets) * 1000.0 << ","
         << SafeDivide(metrics.jitterSumSeconds, metrics.jitterSamples) * 1000.0 << ","
         << SafeDivide(metrics.rxBytes * 8.0, duration * 1000000.0) << ","
         << SafeDivide(metrics.lostPackets * 100.0, metrics.txPackets)
         << "\n";
}

void
WriteNetworkFlowDetails(
  Ptr<FlowMonitor> monitor,
  const std::vector<TransferFlowMetadata>& transferFlows,
  const std::string& outputDirectory,
  bool requireCompleteCoverage)
{
  std::ofstream output(OutputPath(outputDirectory, "network-flow-details.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入逐流网络指标 CSV");
  output
    << "flow_monitor_id,transfer_id,source_address,destination_address,"
       "protocol,source_port,destination_port,"
       "planned_application_payload_bytes,received_application_payload_bytes,"
       "tx_packets,rx_packets,lost_packets,tx_bytes,rx_bytes,"
       "time_first_tx_ns,time_last_rx_ns,mean_delay_ns,mean_jitter_ns,"
       "throughput_bps\n";

  std::map<Ipv4FlowClassifier::FiveTuple, TransferFlowMetadata> metadataByTuple;
  for (const auto& metadata : transferFlows)
    {
      Ipv4FlowClassifier::FiveTuple tuple = {
        metadata.sourceAddress,
        metadata.destinationAddress,
        metadata.protocol,
        metadata.sourcePort,
        metadata.destinationPort
      };
      NS_ABORT_MSG_IF(
        !metadataByTuple.insert(std::make_pair(tuple, metadata)).second,
        "NetworkTransfer metadata 包含重复 five-tuple，transfer_id="
          << metadata.transferId);
    }

  Ptr<Ipv4FlowClassifier> classifier =
    DynamicCast<Ipv4FlowClassifier>(g_flowMonitorHelper.GetClassifier());
  NS_ABORT_MSG_IF(classifier == nullptr, "FlowMonitor 缺少 IPv4 classifier");
  std::set<uint64_t> matchedTransferIds;
  for (const auto& item : monitor->GetFlowStats())
    {
      FlowId flowId = item.first;
      const FlowMonitor::FlowStats& stats = item.second;
      Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
      auto metadata = metadataByTuple.find(tuple);

      uint64_t transferId = 0;
      uint64_t plannedPayloadBytes = 0;
      uint64_t receivedPayloadBytes = 0;
      if (metadata != metadataByTuple.end())
        {
          transferId = metadata->second.transferId;
          plannedPayloadBytes =
            metadata->second.plannedApplicationPayloadBytes;
          receivedPayloadBytes =
            metadata->second.receivedApplicationPayloadBytes;
          matchedTransferIds.insert(transferId);
        }

      int64_t firstTxNs =
        stats.txPackets > 0 ? stats.timeFirstTxPacket.GetNanoSeconds() : 0;
      int64_t lastRxNs =
        stats.rxPackets > 0 ? stats.timeLastRxPacket.GetNanoSeconds() : 0;
      uint64_t meanDelayNs =
        stats.rxPackets > 0
          ? static_cast<uint64_t>(stats.delaySum.GetNanoSeconds())
              / stats.rxPackets
          : 0;
      uint64_t jitterSamples =
        stats.rxPackets > 0 ? stats.rxPackets - 1 : 0;
      uint64_t meanJitterNs =
        jitterSamples > 0
          ? static_cast<uint64_t>(stats.jitterSum.GetNanoSeconds())
              / jitterSamples
          : 0;
      int64_t durationNs =
        stats.rxPackets > 0 ? std::max<int64_t>(0, lastRxNs - firstTxNs) : 0;
      long double throughputBps =
        durationNs > 0
          ? static_cast<long double>(stats.rxBytes) * 8.0L * 1000000000.0L
              / static_cast<long double>(durationNs)
          : 0.0L;

      output << std::setprecision(15)
             << flowId << ","
             << transferId << ","
             << tuple.sourceAddress << ","
             << tuple.destinationAddress << ","
             << static_cast<uint32_t>(tuple.protocol) << ","
             << tuple.sourcePort << ","
             << tuple.destinationPort << ","
             << plannedPayloadBytes << ","
             << receivedPayloadBytes << ","
             << stats.txPackets << ","
             << stats.rxPackets << ","
             << stats.lostPackets << ","
             << stats.txBytes << ","
             << stats.rxBytes << ","
             << firstTxNs << ","
             << lastRxNs << ","
             << meanDelayNs << ","
             << meanJitterNs << ","
             << static_cast<double>(throughputBps)
             << "\n";
    }

  for (const auto& metadata : transferFlows)
    {
      if (matchedTransferIds.find(metadata.transferId)
          != matchedTransferIds.end())
        {
          continue;
        }
      NS_ABORT_MSG_IF(
        requireCompleteCoverage,
        "FlowMonitor 缺少 NetworkTransfer five-tuple，transfer_id="
          << metadata.transferId);
      output << "0,"
             << metadata.transferId << ","
             << metadata.sourceAddress << ","
             << metadata.destinationAddress << ","
             << static_cast<uint32_t>(metadata.protocol) << ","
             << metadata.sourcePort << ","
             << metadata.destinationPort << ","
             << metadata.plannedApplicationPayloadBytes << ","
             << metadata.receivedApplicationPayloadBytes << ","
             << "0,0,0,0,0,0,0,0,0,0\n";
    }
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

void
WriteDiagnosticSummary(
  const FlowAggregate& aggregate,
  double simulationDurationSeconds,
  const RunMetadata& runMetadata,
  const std::vector<TransferSummaryRecord>& transferSummaries,
  const std::vector<QueueDropSummaryRecord>& queueDropSummaries,
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

Ptr<FlowMonitor>
InstallSimulationFlowMonitor()
{
  return g_flowMonitorHelper.InstallAll();
}

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
  m_monitor->CheckForLostPackets();
  FlowAggregate aggregate;
  for (const auto& flow : m_monitor->GetFlowStats())
    {
      aggregate.Add(flow.second);
    }

  bool taskRunComplete =
    m_taskCoordinator == nullptr || m_taskCoordinator->IsComplete();
  std::vector<QueueDropSummaryRecord> queueDropSummaries;
  std::vector<FlowLinkSummaryRecord> flowLinkSummaries;
  if (!taskRunComplete)
    {
      queueDropSummaries =
        CollectQueueDropSummaries(m_directedLinks, m_queueDropEvents);
      flowLinkSummaries =
        CollectFlowLinkSummaries(m_transferFlows,
                                 m_routeEvents,
                                 m_directedLinks,
                                 queueDropSummaries,
                                 m_taskCoordinator);
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
      if (!taskRunComplete)
        {
          WriteIncompleteTasks(*m_taskCoordinator, m_outputDirectory);
          WriteIncompleteTransfers(m_transferSummaries, m_outputDirectory);
          WriteIslQueueDrops(m_queueDropEvents, m_outputDirectory);
          WriteIslQueueDropSummaries(queueDropSummaries, m_outputDirectory);
          WriteFlowLinkSummaries(flowLinkSummaries, m_outputDirectory);
          WriteDiagnosticSummary(aggregate,
                                 m_simulationDurationSeconds,
                                 m_runMetadata,
                                 m_transferSummaries,
                                 queueDropSummaries,
                                 flowLinkSummaries,
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
      if (!taskRunComplete)
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
