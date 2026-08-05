/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Aggregate FlowMonitor counters and write the legacy network metric files.

#include "flow-metrics.h"

#include "ns3/ipv4-flow-probe.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace ns3
{

namespace
{

std::unique_ptr<FlowMonitorHelper> g_flowMonitorHelper;

double
SafeDivide(double numerator, double denominator)
{
    return denominator > 0.0 ? numerator / denominator : 0.0;
}

std::filesystem::path
OutputPath(const std::string& directory, const std::string& filename)
{
    const std::filesystem::path root = directory.empty() ? "." : directory;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        throw std::runtime_error("cannot create metrics directory " + root.string() + ": " +
                                 error.message());
    }
    return root / filename;
}

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
            throw std::runtime_error("duplicate transfer five-tuple in flow metadata");
        }
    }
    return metadataByTuple;
}

} // namespace

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

    const std::size_t reasonCount =
        std::max(flow.packetsDropped.size(), flow.bytesDropped.size());
    droppedPacketsByReason.resize(std::max(droppedPacketsByReason.size(), reasonCount), 0);
    droppedBytesByReason.resize(std::max(droppedBytesByReason.size(), reasonCount), 0);
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
    const double start = flow.timeFirstTxPacket.GetSeconds();
    const double end =
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

double
FlowAggregate::MeasurementDurationSeconds() const
{
    return hasMeasurement ? std::max(0.0, measurementEndSeconds - measurementStartSeconds) : 0.0;
}

uint64_t
FlowAggregate::ReportedDropPackets() const
{
    uint64_t packets = 0;
    for (const uint64_t count : droppedPacketsByReason)
    {
        packets += count;
    }
    return packets;
}

uint64_t
FlowAggregate::UnattributedLostPackets() const
{
    const uint64_t reported = ReportedDropPackets();
    return lostPackets > reported ? lostPackets - reported : 0;
}

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

Ptr<FlowMonitor>
InstallSimulationFlowMonitor()
{
    if (g_flowMonitorHelper == nullptr)
    {
        g_flowMonitorHelper = std::make_unique<FlowMonitorHelper>();
    }
    return g_flowMonitorHelper->InstallAll();
}

void
ResetSimulationFlowMonitor()
{
    g_flowMonitorHelper.reset();
}

Ptr<Ipv4FlowClassifier>
GetSimulationIpv4FlowClassifier()
{
    return g_flowMonitorHelper == nullptr
               ? nullptr
               : DynamicCast<Ipv4FlowClassifier>(g_flowMonitorHelper->GetClassifier());
}

FlowAggregate
CollectFlowAggregate(Ptr<FlowMonitor> monitor)
{
    if (monitor == nullptr)
    {
        throw std::runtime_error("FlowMonitor must not be null");
    }
    monitor->CheckForLostPackets();
    FlowAggregate aggregate;
    for (const auto& flow : monitor->GetFlowStats())
    {
        aggregate.Add(flow.second);
    }
    return aggregate;
}

void
WriteNetworkMetrics(const FlowAggregate& metrics, const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "network-flow-metrics.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write network-flow-metrics.csv");
    }

    const double duration = metrics.MeasurementDurationSeconds();
    output << std::setprecision(15)
           << "tx_packets,rx_packets,lost_packets,tx_bytes,rx_bytes,"
              "measurement_start_s,measurement_end_s,measurement_duration_s,"
              "mean_delay_ms,mean_jitter_ms,throughput_mbps,loss_ratio_percent\n"
           << metrics.txPackets << ',' << metrics.rxPackets << ',' << metrics.lostPackets << ','
           << metrics.txBytes << ',' << metrics.rxBytes << ',' << metrics.measurementStartSeconds
           << ',' << metrics.measurementEndSeconds << ',' << duration << ','
           << SafeDivide(metrics.delaySumSeconds, metrics.rxPackets) * 1000.0 << ','
           << SafeDivide(metrics.jitterSumSeconds, metrics.jitterSamples) * 1000.0 << ','
           << SafeDivide(metrics.rxBytes * 8.0, duration * 1000000.0) << ','
           << SafeDivide(metrics.lostPackets * 100.0, metrics.txPackets) << '\n';
}

void
WriteNetworkFlowDetails(Ptr<FlowMonitor> monitor,
                        const std::vector<TransferFlowMetadata>& transferFlows,
                        const std::string& outputDirectory,
                        bool requireCompleteCoverage)
{
    if (monitor == nullptr)
    {
        throw std::runtime_error("FlowMonitor must not be null");
    }
    std::ofstream output(OutputPath(outputDirectory, "network-flow-details.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write network-flow-details.csv");
    }
    output << "flow_monitor_id,transfer_id,source_address,destination_address,protocol,"
              "source_port,destination_port,planned_application_payload_bytes,"
              "received_application_payload_bytes,tx_packets,rx_packets,lost_packets,"
              "tx_bytes,rx_bytes,time_first_tx_ns,time_last_rx_ns,mean_delay_ns,"
              "mean_jitter_ns,throughput_bps\n";

    const auto metadataByTuple = BuildTransferMetadataIndex(transferFlows);
    const Ptr<Ipv4FlowClassifier> classifier = GetSimulationIpv4FlowClassifier();
    if (classifier == nullptr)
    {
        throw std::runtime_error("FlowMonitor has no IPv4 classifier");
    }
    std::set<uint64_t> matchedTransferIds;
    for (const auto& item : monitor->GetFlowStats())
    {
        const FlowId flowId = item.first;
        const FlowMonitor::FlowStats& stats = item.second;
        const Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
        const auto metadata = metadataByTuple.find(tuple);

        uint64_t transferId = 0;
        uint64_t plannedPayloadBytes = 0;
        uint64_t receivedPayloadBytes = 0;
        if (metadata != metadataByTuple.end())
        {
            transferId = metadata->second.transferId;
            plannedPayloadBytes = metadata->second.plannedApplicationPayloadBytes;
            receivedPayloadBytes = metadata->second.receivedApplicationPayloadBytes;
            matchedTransferIds.insert(transferId);
        }

        const int64_t firstTxNs =
            stats.txPackets > 0 ? stats.timeFirstTxPacket.GetNanoSeconds() : 0;
        const int64_t lastRxNs =
            stats.rxPackets > 0 ? stats.timeLastRxPacket.GetNanoSeconds() : 0;
        const uint64_t meanDelayNs =
            stats.rxPackets > 0
                ? static_cast<uint64_t>(stats.delaySum.GetNanoSeconds()) / stats.rxPackets
                : 0;
        const uint64_t jitterSamples = stats.rxPackets > 0 ? stats.rxPackets - 1 : 0;
        const uint64_t meanJitterNs =
            jitterSamples > 0
                ? static_cast<uint64_t>(stats.jitterSum.GetNanoSeconds()) / jitterSamples
                : 0;
        const int64_t durationNs =
            stats.rxPackets > 0 ? std::max<int64_t>(0, lastRxNs - firstTxNs) : 0;
        const long double throughputBps =
            durationNs > 0
                ? static_cast<long double>(stats.rxBytes) * 8.0L * 1000000000.0L /
                      static_cast<long double>(durationNs)
                : 0.0L;

        output << std::setprecision(15) << flowId << ',' << transferId << ','
               << tuple.sourceAddress << ',' << tuple.destinationAddress << ','
               << static_cast<uint32_t>(tuple.protocol) << ',' << tuple.sourcePort << ','
               << tuple.destinationPort << ',' << plannedPayloadBytes << ','
               << receivedPayloadBytes << ',' << stats.txPackets << ',' << stats.rxPackets << ','
               << stats.lostPackets << ',' << stats.txBytes << ',' << stats.rxBytes << ','
               << firstTxNs << ',' << lastRxNs << ',' << meanDelayNs << ',' << meanJitterNs << ','
               << static_cast<double>(throughputBps) << '\n';
    }

    for (const TransferFlowMetadata& metadata : transferFlows)
    {
        if (matchedTransferIds.find(metadata.transferId) != matchedTransferIds.end())
        {
            continue;
        }
        if (requireCompleteCoverage)
        {
            throw std::runtime_error("FlowMonitor has no five-tuple for transfer " +
                                     std::to_string(metadata.transferId));
        }
        output << "0," << metadata.transferId << ',' << metadata.sourceAddress << ','
               << metadata.destinationAddress << ',' << static_cast<uint32_t>(metadata.protocol)
               << ',' << metadata.sourcePort << ',' << metadata.destinationPort << ','
               << metadata.plannedApplicationPayloadBytes << ','
               << metadata.receivedApplicationPayloadBytes << ",0,0,0,0,0,0,0,0,0,0\n";
    }
}

} // namespace ns3
