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

// 将 FlowMonitor 的逐流丢包原因写为按需生成的失败诊断。

#include "flow-drop-reason-diagnostics.h"

#include "../core/flow-metrics.h"

#include "ns3/abort.h"
#include "ns3/ipv4-flow-classifier.h"

#include <algorithm>
#include <fstream>
#include <map>
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

void
WriteFlowDropReasonRow(
  std::ofstream& output,
  FlowId flowId,
  uint64_t transferId,
  const Ipv4FlowClassifier::FiveTuple& tuple,
  int32_t reasonCode,
  const std::string& reasonName,
  uint64_t droppedPackets,
  uint64_t droppedBytes,
  uint64_t lostPackets,
  uint64_t reportedDropPackets,
  uint64_t unattributedLostPackets)
{
  output << flowId << ","
         << transferId << ","
         << tuple.sourceAddress << ","
         << tuple.destinationAddress << ","
         << static_cast<uint32_t>(tuple.protocol) << ","
         << tuple.sourcePort << ","
         << tuple.destinationPort << ","
         << reasonCode << ","
         << reasonName << ","
         << droppedPackets << ","
         << droppedBytes << ","
         << lostPackets << ","
         << reportedDropPackets << ","
         << unattributedLostPackets
         << "\n";
}

} // namespace

void
WriteFlowDropReasons(
  Ptr<FlowMonitor> monitor,
  const std::vector<TransferFlowMetadata>& transferFlows,
  const std::string& outputDirectory)
{
  NS_ABORT_MSG_IF(monitor == nullptr, "FlowMonitor 不可为空");
  std::ofstream output(OutputPath(outputDirectory, "flow-drop-reasons.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 FlowMonitor DropReason CSV");
  output
    << "flow_monitor_id,transfer_id,source_address,destination_address,"
       "protocol,source_port,destination_port,reason_code,reason_name,"
       "dropped_packets,dropped_bytes,flow_lost_packets,"
       "flow_reported_drop_packets,flow_unattributed_lost_packets\n";

  std::map<Ipv4FlowClassifier::FiveTuple, TransferFlowMetadata> metadataByTuple =
    BuildTransferMetadataIndex(transferFlows);
  Ptr<Ipv4FlowClassifier> classifier =
    GetSimulationIpv4FlowClassifier();
  NS_ABORT_MSG_IF(classifier == nullptr, "FlowMonitor 缺少 IPv4 classifier");

  for (const auto& item : monitor->GetFlowStats())
    {
      FlowId flowId = item.first;
      const FlowMonitor::FlowStats& stats = item.second;
      Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
      auto metadata = metadataByTuple.find(tuple);
      uint64_t transferId =
        metadata == metadataByTuple.end() ? 0 : metadata->second.transferId;

      uint64_t reportedDropPackets = 0;
      for (uint32_t packets : stats.packetsDropped)
        {
          reportedDropPackets += packets;
        }
      uint64_t unattributedLostPackets =
        stats.lostPackets > reportedDropPackets
          ? stats.lostPackets - reportedDropPackets
          : 0;

      std::size_t reasonCount =
        std::max(stats.packetsDropped.size(), stats.bytesDropped.size());
      for (std::size_t reason = 0; reason < reasonCount; ++reason)
        {
          uint64_t droppedPackets =
            reason < stats.packetsDropped.size()
              ? stats.packetsDropped[reason]
              : 0;
          uint64_t droppedBytes =
            reason < stats.bytesDropped.size()
              ? stats.bytesDropped[reason]
              : 0;
          if (droppedPackets == 0 && droppedBytes == 0)
            {
              continue;
            }
          WriteFlowDropReasonRow(
            output,
            flowId,
            transferId,
            tuple,
            static_cast<int32_t>(reason),
            GetIpv4DropReasonName(static_cast<uint32_t>(reason)),
            droppedPackets,
            droppedBytes,
            stats.lostPackets,
            reportedDropPackets,
            unattributedLostPackets);
        }

      if (unattributedLostPackets > 0)
        {
          WriteFlowDropReasonRow(output,
                                 flowId,
                                 transferId,
                                 tuple,
                                 -1,
                                 "UNATTRIBUTED_TIMEOUT",
                                 unattributedLostPackets,
                                 0,
                                 stats.lostPackets,
                                 reportedDropPackets,
                                 unattributedLostPackets);
        }
    }
}

} // namespace ns3
