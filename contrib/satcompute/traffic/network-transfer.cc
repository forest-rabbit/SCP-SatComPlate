/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// 安装 NetworkTransfer 收发应用，并汇总配置、日志与传输结果。

#include "network-transfer.h"

#include "../para.h"

#include "ns3/abort.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <set>
#include <utility>

namespace ns3
{

namespace
{

uint64_t
CheckedAdd(uint64_t left, uint64_t right, const std::string& field)
{
    NS_ABORT_MSG_IF(left > std::numeric_limits<uint64_t>::max() - right,
                    "NetworkTransfer summary " << field << " 溢出");
    return left + right;
}

void
PrintTransferSample(const std::string& label, const NetworkTransfer& transfer)
{
    std::cout << "  " << label << " : id=" << transfer.transferId << " "
              << transfer.sourceSatelliteId << "->" << transfer.destinationSatelliteId
              << " size=" << transfer.sizeBytes << " packets=" << transfer.packetCount
              << " arrival_ns=" << transfer.arrivalTimeNs << std::endl;
}

void
PrintTransferSummary(const std::filesystem::path& filename,
                     const std::vector<NetworkTransfer>& transfers,
                     const std::string& chunkMode,
                     uint32_t fixedPayloadBytes,
                     const std::string& pacingMode)
{
    std::set<uint32_t> sources;
    std::set<uint32_t> destinations;
    std::set<std::pair<uint32_t, uint32_t>> pairs;
    uint64_t totalBytes = 0;
    uint64_t totalPackets = 0;
    uint64_t partialFinalPackets = 0;
    uint64_t minBytes = std::numeric_limits<uint64_t>::max();
    uint64_t maxBytes = 0;
    int64_t firstArrivalNs = std::numeric_limits<int64_t>::max();
    int64_t lastArrivalNs = 0;

    for (const NetworkTransfer& transfer : transfers)
    {
        sources.insert(transfer.sourceSatelliteId);
        destinations.insert(transfer.destinationSatelliteId);
        pairs.emplace(transfer.sourceSatelliteId, transfer.destinationSatelliteId);
        totalBytes = CheckedAdd(totalBytes, transfer.sizeBytes, "bytes");
        totalPackets = CheckedAdd(totalPackets, transfer.packetCount, "packet count");
        partialFinalPackets +=
            transfer.finalPacketPayloadBytes < transfer.payloadBytesPerPacket ? 1 : 0;
        minBytes = std::min(minBytes, transfer.sizeBytes);
        maxBytes = std::max(maxBytes, transfer.sizeBytes);
        firstArrivalNs = std::min(firstArrivalNs, transfer.arrivalTimeNs);
        lastArrivalNs = std::max(lastArrivalNs, transfer.arrivalTimeNs);
    }

    NS_ABORT_MSG_IF(totalPackets > std::numeric_limits<uint64_t>::max() / 28u,
                    "NetworkTransfer estimated header bytes 溢出");
    const uint64_t estimatedHeaderBytes = totalPackets * 28u;
    const long double meanBytes = transfers.empty()
                                      ? 0.0L
                                      : static_cast<long double>(totalBytes) / transfers.size();

    std::cout << "[TRANSFER:SUMMARY]" << std::endl
              << "  trace                       : " << filename << std::endl
              << "  transfers                   : " << transfers.size() << std::endl
              << "  unique sources              : " << sources.size() << std::endl
              << "  unique destinations         : " << destinations.size() << std::endl
              << "  unique source-dest pairs    : " << pairs.size() << std::endl
              << "  total application bytes     : " << totalBytes << std::endl
              << "  min/mean/max transfer bytes : " << minBytes << "/" << meanBytes << "/"
              << maxBytes << std::endl
              << "  first/last arrival ns       : " << firstArrivalNs << "/" << lastArrivalNs
              << std::endl
              << "  chunk mode                  : " << chunkMode << std::endl;
    if (chunkMode == "fixed")
    {
        std::cout << "  fixed payload cap bytes     : " << fixedPayloadBytes << std::endl;
    }
    std::cout << "  pacing mode                 : " << pacingMode << std::endl
              << "  total derived packets       : " << totalPackets << std::endl
              << "  final-packet count          : " << transfers.size() << std::endl
              << "  partial final packets       : " << partialFinalPackets << std::endl
              << "  estimated UDP+IPv4 headers  : " << estimatedHeaderBytes << " bytes"
              << std::endl;

    const uint32_t sampleCount =
        std::min<uint32_t>(3, static_cast<uint32_t>(transfers.size()));
    for (uint32_t index = 0; index < sampleCount; ++index)
    {
        PrintTransferSample("first[" + std::to_string(index) + "]", transfers[index]);
    }
    const uint32_t lastStart =
        transfers.size() > sampleCount
            ? static_cast<uint32_t>(transfers.size()) - sampleCount
            : 0;
    for (uint32_t index = lastStart; index < transfers.size(); ++index)
    {
        PrintTransferSample("last[" + std::to_string(index - lastStart) + "]",
                            transfers[index]);
    }
    std::cout << std::endl;
}

void
PrintTransferVerbose(const std::filesystem::path& filename,
                     const std::vector<NetworkTransfer>& transfers,
                     const std::string& chunkMode,
                     uint32_t fixedPayloadBytes,
                     uint16_t islMtuBytes,
                     const std::string& pacingMode)
{
    std::cout << "[TRANSFER]" << std::endl
              << "  trace              : " << filename << std::endl
              << "  chunk mode         : " << chunkMode << std::endl;
    if (chunkMode == "fixed")
    {
        std::cout << "  fixed payload      : " << fixedPayloadBytes << " bytes" << std::endl;
    }
    std::cout << "  pacing mode        : " << pacingMode << std::endl
              << "  ISL MTU            : " << islMtuBytes << " bytes" << std::endl
              << "  transfers          : " << transfers.size() << std::endl
              << std::endl;

    for (const NetworkTransfer& transfer : transfers)
    {
        std::cout << "[TRANSFER:" << transfer.transferId << "]" << std::endl
                  << "  source             : sat_id=" << transfer.sourceSatelliteId << " "
                  << transfer.sourceAddress << ":" << transfer.sourcePort << std::endl
                  << "  destination        : sat_id=" << transfer.destinationSatelliteId << " "
                  << transfer.destinationAddress << ":" << transfer.destinationPort
                  << std::endl
                  << "  size_bytes         : " << transfer.sizeBytes << std::endl
                  << "  payload_cap_bytes  : " << transfer.payloadBytesPerPacket << std::endl
                  << "  packet_count       : " << transfer.packetCount << std::endl
                  << "  final_payload_bytes: " << transfer.finalPacketPayloadBytes << std::endl
                  << "  arrival_time_ns    : " << transfer.arrivalTimeNs << std::endl
                  << "  isl_mtu_bytes      : " << islMtuBytes << std::endl
                  << std::endl;
    }
}

} // namespace

NetworkTransferState
InstallNetworkTransfersNs(const std::filesystem::path& filename,
                          const std::string& chunkMode,
                          uint32_t payloadBytes,
                          uint16_t islMtuBytes,
                          uint32_t receiverRcvBufBytes,
                          bool collectUdpSocketDrops,
                          const std::string& logMode,
                          int64_t simulationDurationNs,
                          SatelliteRuntimeView& topology)
{
    NS_ABORT_MSG_IF(logMode != "summary" && logMode != "verbose" && logMode != "silent",
                    "未知 transferLogMode: " << logMode);

    NetworkTransferState state;
    std::vector<NetworkTransfer> plans = ReadNetworkTransferTrace(filename,
                                                                  simulationDurationNs,
                                                                  chunkMode,
                                                                  payloadBytes,
                                                                  topology);
    state.engine = CreateObject<NetworkTransferEngine>();
    state.engine->Configure(topology,
                            chunkMode,
                            payloadBytes,
                            islMtuBytes,
                            receiverRcvBufBytes,
                            collectUdpSocketDrops,
                            simulationDurationNs);
    state.engine->RegisterPlans(std::move(plans));
    state.engine->ScheduleDeclaredTransfers();

    const std::vector<NetworkTransfer>& preparedPlans = state.engine->GetPlans();
    const std::string pacingMode = topology.IsCapacityAwareRouting()
                                       ? "path-bottleneck-serialization"
                                       : "first-hop-serialization";
    if (logMode == "summary")
    {
        PrintTransferSummary(filename,
                             preparedPlans,
                             chunkMode,
                             payloadBytes,
                             pacingMode);
    }
    else if (logMode == "verbose")
    {
        PrintTransferVerbose(filename,
                             preparedPlans,
                             chunkMode,
                             payloadBytes,
                             islMtuBytes,
                             pacingMode);
    }
    return state;
}

NetworkTransferState
InstallNetworkTransfers(const std::string& filename,
                        const std::string& chunkMode,
                        uint32_t payloadBytes,
                        uint16_t islMtuBytes,
                        uint32_t receiverRcvBufBytes,
                        bool collectUdpSocketDrops,
                        const std::string& logMode,
                        double simulationDurationSeconds,
                        SatelliteTopology& topology)
{
    return InstallNetworkTransfersNs(
        std::filesystem::path(filename),
        chunkMode,
        payloadBytes,
        islMtuBytes,
        receiverRcvBufBytes,
        collectUdpSocketDrops,
        logMode,
        SatComputeSecondsToNanoseconds(simulationDurationSeconds, "simulationDuration"),
        topology);
}

ApplicationMetrics
CollectNetworkTransferMetrics(const NetworkTransferState& state)
{
    NS_ABORT_MSG_IF(state.engine == nullptr, "NetworkTransferState 缺少 engine");
    return state.engine->CollectApplicationMetrics();
}

std::vector<TransferFlowMetadata>
CollectNetworkTransferFlowMetadata(const NetworkTransferState& state)
{
    NS_ABORT_MSG_IF(state.engine == nullptr, "NetworkTransferState 缺少 engine");
    return state.engine->CollectFlowMetadata();
}

std::vector<TransferSummaryRecord>
CollectNetworkTransferSummaries(const NetworkTransferState& state)
{
    NS_ABORT_MSG_IF(state.engine == nullptr, "NetworkTransferState 缺少 engine");
    return state.engine->CollectSummaries();
}

} // namespace ns3
