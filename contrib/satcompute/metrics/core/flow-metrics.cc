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

// 汇总 FlowMonitor 指标并写出网络流基础输出。

#include "flow-metrics.h"

#include "ns3/abort.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/ipv4-flow-probe.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sys/stat.h>

namespace ns3 {

namespace {

FlowMonitorHelper g_flowMonitorHelper;

double
SafeDivide(double numerator, double denominator)
{
  return denominator > 0.0 ? numerator / denominator : 0.0;
}

std::string
OutputPath(const std::string& directory, const std::string& filename)
{
  if (directory.empty() || directory == ".")
    {
      return filename;
    }
  mkdir(directory.c_str(), 0755);
  return directory.back() == '/'
           ? directory + filename
           : directory + "/" + filename;
}

std::map<Ipv4FlowClassifier::FiveTuple, TransferFlowMetadata>
BuildTransferMetadataIndex(
  const std::vector<TransferFlowMetadata>& transferFlows)
{
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
  return metadataByTuple;
}

} // namespace

uint32_t
GetIpv4DropReasonCount()
{
  return static_cast<uint32_t>(Ipv4FlowProbe::DROP_INVALID_REASON) + 1;
}

const char*
GetIpv4DropReasonName(uint32_t reasonCode)
{
  switch (reasonCode)
    {
    case Ipv4FlowProbe::DROP_NO_ROUTE:
      return "NO_ROUTE";
    case Ipv4FlowProbe::DROP_TTL_EXPIRE:
      return "TTL_EXPIRE";
    case Ipv4FlowProbe::DROP_BAD_CHECKSUM:
      return "BAD_CHECKSUM";
    case Ipv4FlowProbe::DROP_QUEUE:
      return "QUEUE";
    case Ipv4FlowProbe::DROP_QUEUE_DISC:
      return "QUEUE_DISC";
    case Ipv4FlowProbe::DROP_INTERFACE_DOWN:
      return "INTERFACE_DOWN";
    case Ipv4FlowProbe::DROP_ROUTE_ERROR:
      return "ROUTE_ERROR";
    case Ipv4FlowProbe::DROP_FRAGMENT_TIMEOUT:
      return "FRAGMENT_TIMEOUT";
    case Ipv4FlowProbe::DROP_INVALID_REASON:
      return "INVALID_REASON";
    default:
      return "UNKNOWN_REASON";
    }
}

void
FlowAggregate::Add(const FlowMonitor::FlowStats& flow)
{
  txPackets += flow.txPackets;
  rxPackets += flow.rxPackets;
  lostPackets += flow.lostPackets;
  txBytes += flow.txBytes;
  rxBytes += flow.rxBytes;
  delaySumSeconds += flow.delaySum.GetSeconds();
  jitterSumSeconds += flow.jitterSum.GetSeconds();
  jitterSamples += flow.rxPackets > 0 ? flow.rxPackets - 1 : 0;

  std::size_t reasonCount =
    std::max(flow.packetsDropped.size(), flow.bytesDropped.size());
  droppedPacketsByReason.resize(
    std::max(droppedPacketsByReason.size(), reasonCount),
    0);
  droppedBytesByReason.resize(
    std::max(droppedBytesByReason.size(), reasonCount),
    0);
  for (std::size_t reason = 0; reason < reasonCount; ++reason)
    {
      if (reason < flow.packetsDropped.size())
        {
          droppedPacketsByReason[reason] += flow.packetsDropped[reason];
        }
      if (reason < flow.bytesDropped.size())
        {
          droppedBytesByReason[reason] += flow.bytesDropped[reason];
        }
    }

  if (flow.txPackets == 0)
    {
      return;
    }

  double start = flow.timeFirstTxPacket.GetSeconds();
  double end =
    std::max(flow.timeLastTxPacket.GetSeconds(),
             flow.timeLastRxPacket.GetSeconds());
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

double
FlowAggregate::MeasurementDurationSeconds() const
{
  return hasMeasurement
           ? std::max(0.0, measurementEndSeconds - measurementStartSeconds)
           : 0.0;
}

uint64_t
FlowAggregate::ReportedDropPackets() const
{
  uint64_t packets = 0;
  for (uint64_t count : droppedPacketsByReason)
    {
      packets += count;
    }
  return packets;
}

uint64_t
FlowAggregate::UnattributedLostPackets() const
{
  uint64_t reported = ReportedDropPackets();
  return lostPackets > reported ? lostPackets - reported : 0;
}

Ptr<FlowMonitor>
InstallSimulationFlowMonitor()
{
  return g_flowMonitorHelper.InstallAll();
}

Ptr<Ipv4FlowClassifier>
GetSimulationIpv4FlowClassifier()
{
  return DynamicCast<Ipv4FlowClassifier>(
    g_flowMonitorHelper.GetClassifier());
}

FlowAggregate
CollectFlowAggregate(Ptr<FlowMonitor> monitor)
{
  NS_ABORT_MSG_IF(monitor == nullptr, "FlowMonitor 不可为空");
  monitor->CheckForLostPackets();
  FlowAggregate aggregate;
  for (const auto& flow : monitor->GetFlowStats())
    {
      aggregate.Add(flow.second);
    }
  return aggregate;
}

void
WriteNetworkMetrics(const FlowAggregate& metrics,
                    const std::string& outputDirectory)
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

  std::map<Ipv4FlowClassifier::FiveTuple, TransferFlowMetadata> metadataByTuple =
    BuildTransferMetadataIndex(transferFlows);

  Ptr<Ipv4FlowClassifier> classifier =
    GetSimulationIpv4FlowClassifier();
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

} // namespace ns3
