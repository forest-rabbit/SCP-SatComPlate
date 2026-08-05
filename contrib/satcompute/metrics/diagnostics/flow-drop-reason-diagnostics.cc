/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Write per-flow FlowMonitor loss attribution only when diagnostics are enabled.

#include "flow-drop-reason-diagnostics.h"

#include "failure-diagnostics.h"

#include "ns3/ipv4-flow-classifier.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace ns3
{

namespace
{

std::map<Ipv4FlowClassifier::FiveTuple, TransferFlowMetadata>
BuildTransferMetadataIndex(const std::vector<TransferFlowMetadata>& transferFlows)
{
    std::map<Ipv4FlowClassifier::FiveTuple, TransferFlowMetadata> metadataByTuple;
    for (const TransferFlowMetadata& metadata : transferFlows)
    {
        const Ipv4FlowClassifier::FiveTuple tuple = {metadata.sourceAddress,
                                                     metadata.destinationAddress,
                                                     metadata.protocol,
                                                     metadata.sourcePort,
                                                     metadata.destinationPort};
        if (!metadataByTuple.emplace(tuple, metadata).second)
        {
            throw std::runtime_error("duplicate transfer five-tuple in drop diagnostics");
        }
    }
    return metadataByTuple;
}

void
WriteFlowDropReasonRow(std::ofstream& output,
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
    output << flowId << ',' << transferId << ',' << tuple.sourceAddress << ','
           << tuple.destinationAddress << ',' << static_cast<uint32_t>(tuple.protocol) << ','
           << tuple.sourcePort << ',' << tuple.destinationPort << ',' << reasonCode << ','
           << reasonName << ',' << droppedPackets << ',' << droppedBytes << ',' << lostPackets
           << ',' << reportedDropPackets << ',' << unattributedLostPackets << '\n';
}

} // namespace

void
WriteFlowDropReasons(Ptr<FlowMonitor> monitor,
                     const std::vector<TransferFlowMetadata>& transferFlows,
                     const std::string& outputDirectory)
{
    if (monitor == nullptr)
    {
        throw std::runtime_error("FlowMonitor must not be null");
    }
    PrepareFailureDiagnosticDirectory(outputDirectory);
    const std::filesystem::path path =
        std::filesystem::path(GetFailureDiagnosticDirectory(outputDirectory)) /
        "flow-drop-reasons.csv";
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write flow-drop-reasons.csv");
    }
    output << "flow_monitor_id,transfer_id,source_address,destination_address,protocol,"
              "source_port,destination_port,reason_code,reason_name,dropped_packets,"
              "dropped_bytes,flow_lost_packets,flow_reported_drop_packets,"
              "flow_unattributed_lost_packets\n";

    const auto metadataByTuple = BuildTransferMetadataIndex(transferFlows);
    const Ptr<Ipv4FlowClassifier> classifier = GetSimulationIpv4FlowClassifier();
    if (classifier == nullptr)
    {
        throw std::runtime_error("FlowMonitor has no IPv4 classifier");
    }
    for (const auto& item : monitor->GetFlowStats())
    {
        const FlowId flowId = item.first;
        const FlowMonitor::FlowStats& stats = item.second;
        const Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
        const auto metadata = metadataByTuple.find(tuple);
        const uint64_t transferId =
            metadata == metadataByTuple.end() ? 0 : metadata->second.transferId;

        uint64_t reportedDropPackets = 0;
        for (const uint32_t packets : stats.packetsDropped)
        {
            reportedDropPackets += packets;
        }
        const uint64_t unattributedLostPackets =
            stats.lostPackets > reportedDropPackets ? stats.lostPackets - reportedDropPackets : 0;
        const std::size_t reasonCount =
            std::max(stats.packetsDropped.size(), stats.bytesDropped.size());
        for (std::size_t reason = 0; reason < reasonCount; ++reason)
        {
            const uint64_t droppedPackets =
                reason < stats.packetsDropped.size() ? stats.packetsDropped[reason] : 0;
            const uint64_t droppedBytes =
                reason < stats.bytesDropped.size() ? stats.bytesDropped[reason] : 0;
            if (droppedPackets == 0 && droppedBytes == 0)
            {
                continue;
            }
            WriteFlowDropReasonRow(output,
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
