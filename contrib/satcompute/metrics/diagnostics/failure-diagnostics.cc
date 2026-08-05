/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Generate detailed evidence only for explicitly requested failed task runs.

#include "failure-diagnostics.h"

#include "../../common/time-conversion.h"
#include "../../task/task-coordinator.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace ns3
{

namespace
{

using Json = nlohmann::json;
using OutputQueueKey = std::pair<uint32_t, uint32_t>;
using DirectedLinkKey = std::tuple<uint32_t, uint32_t, uint32_t>;
using UdpReceiverKey = std::tuple<uint32_t, uint32_t, uint16_t, uint32_t>;

struct QueueDropSummaryRecord
{
    uint32_t sourceNodeId{};
    uint32_t destinationNodeId{};
    uint32_t outputInterface{};
    uint64_t dropPackets{};
    uint64_t dropBytes{};
    int64_t firstDropTimeNs{};
    int64_t lastDropTimeNs{};
};

struct UdpSocketDropSummaryRecord
{
    uint32_t destinationNodeId{};
    Ipv4Address destinationAddress;
    uint16_t destinationPort{};
    uint32_t receiverRcvBufBytes{};
    uint64_t dropPackets{};
    uint64_t dropBytes{};
    int64_t firstDropTimeNs{};
    int64_t lastDropTimeNs{};
};

struct FlowLinkSummaryRecord
{
    uint32_t sourceNodeId{};
    uint32_t destinationNodeId{};
    uint32_t outputInterface{};
    uint64_t uniqueTransferCount{};
    uint64_t plannedApplicationBytes{};
    uint64_t largeTransferCount{};
    uint64_t inputTransferCount{};
    uint64_t resultTransferCount{};
    bool adjacentToComputeNode{};
    uint64_t dropPackets{};
    uint64_t dropBytes{};
};

uint64_t
CheckedAdd(uint64_t left, uint64_t right, const std::string& field)
{
    if (left > std::numeric_limits<uint64_t>::max() - right)
    {
        throw std::runtime_error("failure diagnostic " + field + " overflow");
    }
    return left + right;
}

std::filesystem::path
RootPath(const std::string& directory)
{
    return directory.empty() ? std::filesystem::path(".") : std::filesystem::path(directory);
}

std::filesystem::path
OutputPath(const std::string& directory, const std::string& filename)
{
    return RootPath(directory) / filename;
}

void
EnsureDirectory(const std::filesystem::path& directory)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || !std::filesystem::is_directory(directory))
    {
        throw std::runtime_error("cannot create diagnostic directory " + directory.string() +
                                 (error ? ": " + error.message() : ""));
    }
}

void
RemoveKnownFile(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        return;
    }
    if (error)
    {
        throw std::runtime_error("cannot inspect stale diagnostic " + path.string() + ": " +
                                 error.message());
    }
    if (!std::filesystem::is_regular_file(status))
    {
        return;
    }
    std::filesystem::remove(path, error);
    if (error)
    {
        throw std::runtime_error("cannot remove stale diagnostic " + path.string() + ": " +
                                 error.message());
    }
}

void
RemoveEmptyDirectory(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error && error != std::errc::directory_not_empty &&
        error != std::errc::no_such_file_or_directory)
    {
        throw std::runtime_error("cannot remove empty diagnostic directory " + path.string() +
                                 ": " + error.message());
    }
}

DirectedLinkKey
MakeDirectedLinkKey(uint32_t sourceNodeId,
                    uint32_t destinationNodeId,
                    uint32_t outputInterface)
{
    return {sourceNodeId, destinationNodeId, outputInterface};
}

UdpReceiverKey
MakeUdpReceiverKey(const UdpSocketDropEvent& event)
{
    return {event.destinationSatelliteId,
            event.destinationAddress.Get(),
            event.destinationPort,
            event.receiverRcvBufBytes};
}

std::map<OutputQueueKey, IslDirectedLink>
IndexDirectedLinks(const std::vector<IslDirectedLink>& directedLinks)
{
    std::map<OutputQueueKey, IslDirectedLink> linksByOutputQueue;
    for (const IslDirectedLink& link : directedLinks)
    {
        const OutputQueueKey key = {link.sourceSatelliteId, link.outputInterface};
        if (!linksByOutputQueue.emplace(key, link).second)
        {
            throw std::runtime_error("duplicate directed ISL output queue");
        }
    }
    return linksByOutputQueue;
}

std::vector<QueueDropSummaryRecord>
CollectQueueDropSummaries(const std::vector<IslDirectedLink>& directedLinks,
                          const std::vector<IslQueueDropEvent>& queueDropEvents)
{
    const auto linksByOutputQueue = IndexDirectedLinks(directedLinks);
    std::map<DirectedLinkKey, QueueDropSummaryRecord> summariesByLink;
    for (const IslQueueDropEvent& event : queueDropEvents)
    {
        const OutputQueueKey outputQueue = {event.sourceSatelliteId, event.outputInterface};
        const auto mappedLink = linksByOutputQueue.find(outputQueue);
        if (mappedLink == linksByOutputQueue.end() ||
            mappedLink->second.destinationSatelliteId != event.destinationSatelliteId)
        {
            throw std::runtime_error("ISL queue drop cannot be mapped to a directed link");
        }
        if (event.packetSizeBytes == 0)
        {
            throw std::runtime_error("ISL queue drop packet size must be positive");
        }
        const DirectedLinkKey key = MakeDirectedLinkKey(event.sourceSatelliteId,
                                                        event.destinationSatelliteId,
                                                        event.outputInterface);
        auto insertion = summariesByLink.emplace(
            key,
            QueueDropSummaryRecord{event.sourceSatelliteId,
                                   event.destinationSatelliteId,
                                   event.outputInterface,
                                   0,
                                   0,
                                   event.simulationTimeNs,
                                   event.simulationTimeNs});
        QueueDropSummaryRecord& summary = insertion.first->second;
        ++summary.dropPackets;
        summary.dropBytes = CheckedAdd(summary.dropBytes,
                                       event.packetSizeBytes,
                                       "ISL queue drop bytes");
        summary.lastDropTimeNs = event.simulationTimeNs;
        if (event.cumulativeDropPackets != summary.dropPackets ||
            event.cumulativeDropBytes != summary.dropBytes)
        {
            throw std::runtime_error("ISL queue drop cumulative totals disagree");
        }
    }

    std::vector<QueueDropSummaryRecord> summaries;
    summaries.reserve(summariesByLink.size());
    for (const auto& item : summariesByLink)
    {
        summaries.push_back(item.second);
    }
    std::sort(summaries.begin(),
              summaries.end(),
              [](const QueueDropSummaryRecord& left, const QueueDropSummaryRecord& right) {
                  if (left.dropBytes != right.dropBytes)
                  {
                      return left.dropBytes > right.dropBytes;
                  }
                  if (left.dropPackets != right.dropPackets)
                  {
                      return left.dropPackets > right.dropPackets;
                  }
                  return std::tie(left.sourceNodeId,
                                  left.destinationNodeId,
                                  left.outputInterface) <
                         std::tie(right.sourceNodeId,
                                  right.destinationNodeId,
                                  right.outputInterface);
              });
    return summaries;
}

void
WriteIslQueueDrops(const std::vector<IslQueueDropEvent>& events,
                   const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "isl-queue-drops.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write isl-queue-drops.csv");
    }
    output << "simulation_time_ns,source_node_id,destination_node_id,output_interface,"
              "packet_size_bytes,cumulative_drop_packets,cumulative_drop_bytes\n";
    for (const IslQueueDropEvent& event : events)
    {
        output << event.simulationTimeNs << ',' << event.sourceSatelliteId << ','
               << event.destinationSatelliteId << ',' << event.outputInterface << ','
               << event.packetSizeBytes << ',' << event.cumulativeDropPackets << ','
               << event.cumulativeDropBytes << '\n';
    }
}

void
WriteIslQueueDropSummaries(const std::vector<QueueDropSummaryRecord>& summaries,
                           const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "isl-queue-drop-summary.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write isl-queue-drop-summary.csv");
    }
    output << "source_node_id,destination_node_id,output_interface,drop_packets,drop_bytes,"
              "first_drop_time_ns,last_drop_time_ns\n";
    for (const QueueDropSummaryRecord& summary : summaries)
    {
        output << summary.sourceNodeId << ',' << summary.destinationNodeId << ','
               << summary.outputInterface << ',' << summary.dropPackets << ','
               << summary.dropBytes << ',' << summary.firstDropTimeNs << ','
               << summary.lastDropTimeNs << '\n';
    }
}

std::vector<UdpSocketDropSummaryRecord>
CollectUdpSocketDropSummaries(const std::vector<UdpSocketDropEvent>& events,
                              const RunMetadata& metadata)
{
    std::map<UdpReceiverKey, UdpSocketDropSummaryRecord> summariesByReceiver;
    int64_t previousTimeNs = -1;
    for (const UdpSocketDropEvent& event : events)
    {
        if (event.simulationTimeNs < previousTimeNs ||
            event.destinationAddress == Ipv4Address::GetAny() || event.destinationPort == 0 ||
            event.packetSizeBytes == 0)
        {
            throw std::runtime_error("invalid or unsorted UDP socket drop event");
        }
        if (event.receiverRcvBufBytes != metadata.receiverRcvBufBytes)
        {
            throw std::runtime_error("UDP socket drop buffer differs from run metadata");
        }
        previousTimeNs = event.simulationTimeNs;
        const UdpReceiverKey key = MakeUdpReceiverKey(event);
        auto insertion = summariesByReceiver.emplace(
            key,
            UdpSocketDropSummaryRecord{event.destinationSatelliteId,
                                       event.destinationAddress,
                                       event.destinationPort,
                                       event.receiverRcvBufBytes,
                                       0,
                                       0,
                                       event.simulationTimeNs,
                                       event.simulationTimeNs});
        UdpSocketDropSummaryRecord& summary = insertion.first->second;
        ++summary.dropPackets;
        summary.dropBytes =
            CheckedAdd(summary.dropBytes, event.packetSizeBytes, "UDP socket drop bytes");
        summary.lastDropTimeNs = event.simulationTimeNs;
        if (event.cumulativeDropPackets != summary.dropPackets ||
            event.cumulativeDropBytes != summary.dropBytes)
        {
            throw std::runtime_error("UDP socket drop cumulative totals disagree");
        }
    }

    std::vector<UdpSocketDropSummaryRecord> summaries;
    summaries.reserve(summariesByReceiver.size());
    for (const auto& item : summariesByReceiver)
    {
        summaries.push_back(item.second);
    }
    std::sort(summaries.begin(),
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
                                         left.receiverRcvBufBytes) <
                         std::make_tuple(right.destinationNodeId,
                                         right.destinationAddress.Get(),
                                         right.destinationPort,
                                         right.receiverRcvBufBytes);
              });
    return summaries;
}

void
WriteUdpSocketDrops(const std::vector<UdpSocketDropEvent>& events,
                    const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "udp-socket-drops.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write udp-socket-drops.csv");
    }
    output << "simulation_time_ns,destination_node_id,destination_address,destination_port,"
              "packet_size_bytes,cumulative_drop_packets,cumulative_drop_bytes,"
              "receiver_rcv_buf_bytes\n";
    for (const UdpSocketDropEvent& event : events)
    {
        output << event.simulationTimeNs << ',' << event.destinationSatelliteId << ','
               << event.destinationAddress << ',' << event.destinationPort << ','
               << event.packetSizeBytes << ',' << event.cumulativeDropPackets << ','
               << event.cumulativeDropBytes << ',' << event.receiverRcvBufBytes << '\n';
    }
}

void
WriteUdpSocketDropSummaries(const std::vector<UdpSocketDropSummaryRecord>& summaries,
                            const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "udp-socket-drop-summary.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write udp-socket-drop-summary.csv");
    }
    output << "destination_node_id,destination_address,destination_port,"
              "receiver_rcv_buf_bytes,drop_packets,drop_bytes,first_drop_time_ns,"
              "last_drop_time_ns\n";
    for (const UdpSocketDropSummaryRecord& summary : summaries)
    {
        output << summary.destinationNodeId << ',' << summary.destinationAddress << ','
               << summary.destinationPort << ',' << summary.receiverRcvBufBytes << ','
               << summary.dropPackets << ',' << summary.dropBytes << ','
               << summary.firstDropTimeNs << ',' << summary.lastDropTimeNs << '\n';
    }
}

std::vector<FlowLinkSummaryRecord>
CollectFlowLinkSummaries(const std::vector<TransferFlowMetadata>& transferFlows,
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
        uint64_t plannedApplicationBytes{};
        uint64_t largeTransferCount{};
        uint64_t inputTransferCount{};
        uint64_t resultTransferCount{};
        uint64_t dropPackets{};
        uint64_t dropBytes{};
    };

    const auto linksByOutputQueue = IndexDirectedLinks(directedLinks);
    std::map<EcmpFlowKey, TransferFlowMetadata> metadataByFlow;
    for (const TransferFlowMetadata& metadata : transferFlows)
    {
        EcmpFlowKey key;
        key.sourceAddress = metadata.sourceAddress;
        key.destinationAddress = metadata.destinationAddress;
        key.protocol = metadata.protocol;
        key.sourcePort = metadata.sourcePort;
        key.destinationPort = metadata.destinationPort;
        if (!metadataByFlow.emplace(key, metadata).second)
        {
            throw std::runtime_error("duplicate transfer ECMP flow key");
        }
    }

    std::set<uint64_t> inputTransferIds;
    std::set<uint64_t> resultTransferIds;
    std::set<uint32_t> computeNodeIds;
    if (coordinator != nullptr)
    {
        for (const TaskRuntime& task : coordinator->GetTaskRuntimes())
        {
            inputTransferIds.insert(task.definition.inputTransferId);
            resultTransferIds.insert(task.definition.resultTransferId);
        }
        for (const Ptr<ComputeService>& service : coordinator->GetComputeServices())
        {
            computeNodeIds.insert(service->GetNodeId());
        }
    }

    std::map<DirectedLinkKey, FlowLinkAccumulator> accumulators;
    for (const EcmpRouteDecisionEvent& event : routeEvents)
    {
        if (!event.hasFiveTuple || event.selectedOutputInterface < 0)
        {
            continue;
        }
        const auto metadata = metadataByFlow.find(event.flowKey);
        if (metadata == metadataByFlow.end())
        {
            continue;
        }

        const uint32_t outputInterface =
            static_cast<uint32_t>(event.selectedOutputInterface);
        const auto mappedLink = linksByOutputQueue.find({event.nodeId, outputInterface});
        if (mappedLink == linksByOutputQueue.end())
        {
            throw std::runtime_error("ECMP route event cannot be mapped to a directed ISL");
        }
        const IslDirectedLink& link = mappedLink->second;
        const DirectedLinkKey key = MakeDirectedLinkKey(link.sourceSatelliteId,
                                                        link.destinationSatelliteId,
                                                        link.outputInterface);
        auto insertion = accumulators.emplace(key, FlowLinkAccumulator(link));
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
        if (inputTransferIds.count(metadata->second.transferId) != 0)
        {
            ++accumulator.inputTransferCount;
        }
        else if (resultTransferIds.count(metadata->second.transferId) != 0)
        {
            ++accumulator.resultTransferCount;
        }
        else if (coordinator != nullptr)
        {
            throw std::runtime_error("task transfer cannot be classified as input or result");
        }
    }

    for (const QueueDropSummaryRecord& drop : queueDropSummaries)
    {
        const DirectedLinkKey key = MakeDirectedLinkKey(drop.sourceNodeId,
                                                        drop.destinationNodeId,
                                                        drop.outputInterface);
        const IslDirectedLink link = {drop.sourceNodeId,
                                      drop.destinationNodeId,
                                      drop.outputInterface};
        auto insertion = accumulators.emplace(key, FlowLinkAccumulator(link));
        insertion.first->second.dropPackets = drop.dropPackets;
        insertion.first->second.dropBytes = drop.dropBytes;
    }

    std::vector<FlowLinkSummaryRecord> summaries;
    summaries.reserve(accumulators.size());
    for (const auto& item : accumulators)
    {
        const FlowLinkAccumulator& accumulator = item.second;
        summaries.push_back(
            {accumulator.link.sourceSatelliteId,
             accumulator.link.destinationSatelliteId,
             accumulator.link.outputInterface,
             accumulator.transferIds.size(),
             accumulator.plannedApplicationBytes,
             accumulator.largeTransferCount,
             accumulator.inputTransferCount,
             accumulator.resultTransferCount,
             computeNodeIds.count(accumulator.link.sourceSatelliteId) != 0 ||
                 computeNodeIds.count(accumulator.link.destinationSatelliteId) != 0,
             accumulator.dropPackets,
             accumulator.dropBytes});
    }
    std::sort(summaries.begin(),
              summaries.end(),
              [](const FlowLinkSummaryRecord& left, const FlowLinkSummaryRecord& right) {
                  if (left.plannedApplicationBytes != right.plannedApplicationBytes)
                  {
                      return left.plannedApplicationBytes > right.plannedApplicationBytes;
                  }
                  if (left.uniqueTransferCount != right.uniqueTransferCount)
                  {
                      return left.uniqueTransferCount > right.uniqueTransferCount;
                  }
                  return std::tie(left.sourceNodeId,
                                  left.destinationNodeId,
                                  left.outputInterface) <
                         std::tie(right.sourceNodeId,
                                  right.destinationNodeId,
                                  right.outputInterface);
              });
    return summaries;
}

void
WriteFlowLinkSummaries(const std::vector<FlowLinkSummaryRecord>& summaries,
                       const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "flow-link-concentration.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write flow-link-concentration.csv");
    }
    output << "source_node_id,destination_node_id,output_interface,unique_transfer_count,"
              "planned_application_bytes,large_transfer_count,input_transfer_count,"
              "result_transfer_count,adjacent_to_compute_node,drop_packets,drop_bytes\n";
    for (const FlowLinkSummaryRecord& summary : summaries)
    {
        output << summary.sourceNodeId << ',' << summary.destinationNodeId << ','
               << summary.outputInterface << ',' << summary.uniqueTransferCount << ','
               << summary.plannedApplicationBytes << ',' << summary.largeTransferCount << ','
               << summary.inputTransferCount << ',' << summary.resultTransferCount << ','
               << (summary.adjacentToComputeNode ? 1 : 0) << ',' << summary.dropPackets << ','
               << summary.dropBytes << '\n';
    }
}

void
WriteIncompleteTasks(const TaskCoordinator& coordinator, const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "incomplete-tasks.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write incomplete-tasks.csv");
    }
    output << "task_id,state,source_node_id,compute_node_id,result_node_id,input_transfer_id,"
              "result_transfer_id,arrival_time_ns,last_transition_time_ns,"
              "input_transfer_complete_time_ns,queue_enter_time_ns,compute_start_time_ns,"
              "compute_complete_time_ns,result_transfer_complete_time_ns\n";
    for (const TaskRuntime& task : coordinator.GetTaskRuntimes())
    {
        if (task.state == TASK_COMPLETED)
        {
            continue;
        }
        output << task.definition.taskId << ',' << TaskStateToString(task.state) << ','
               << task.definition.sourceNodeId << ',' << task.definition.computeNodeId << ','
               << task.definition.resultNodeId << ',' << task.definition.inputTransferId << ','
               << task.definition.resultTransferId << ',' << task.definition.arrivalTimeNs << ','
               << task.lastTransitionTimeNs << ',' << task.inputTransferCompleteTimeNs << ','
               << task.queueEnterTimeNs << ',' << task.computeStartTimeNs << ','
               << task.computeCompleteTimeNs << ',' << task.resultTransferCompleteTimeNs << '\n';
    }
}

void
WriteIncompleteTransfers(const std::vector<TransferSummaryRecord>& summaries,
                         const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "incomplete-transfers.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write incomplete-transfers.csv");
    }
    output << "transfer_id,transfer_state,source_node_id,destination_node_id,source_address,"
              "destination_address,source_port,destination_port,declared_size_bytes,"
              "payload_bytes_per_packet,derived_packet_count,sent_application_bytes,"
              "sent_packet_count,received_application_bytes,received_packet_count,"
              "missing_application_bytes,missing_packet_count_lower_bound,arrival_time_ns,"
              "last_send_time_ns,completion_time_ns\n";
    for (const TransferSummaryRecord& summary : summaries)
    {
        if (summary.transferState == "COMPLETED")
        {
            continue;
        }
        if (summary.receivedApplicationBytes > summary.declaredSizeBytes ||
            summary.receivedPacketCount > summary.derivedPacketCount)
        {
            throw std::runtime_error("incomplete transfer counters exceed declared totals");
        }
        output << summary.transferId << ',' << summary.transferState << ','
               << summary.sourceSatelliteId << ',' << summary.destinationSatelliteId << ','
               << summary.sourceAddress << ',' << summary.destinationAddress << ','
               << summary.sourcePort << ',' << summary.destinationPort << ','
               << summary.declaredSizeBytes << ',' << summary.payloadBytesPerPacket << ','
               << summary.derivedPacketCount << ',' << summary.sentApplicationBytes << ','
               << summary.sentPacketCount << ',' << summary.receivedApplicationBytes << ','
               << summary.receivedPacketCount << ','
               << summary.declaredSizeBytes - summary.receivedApplicationBytes << ','
               << summary.derivedPacketCount - summary.receivedPacketCount << ','
               << summary.arrivalTimeNs << ',' << summary.lastSendTimeNs << ','
               << summary.completionTimeNs << '\n';
    }
}

void
WriteDiagnosticSummary(const FlowAggregate& aggregate,
                       int64_t simulationDurationNs,
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
    for (const TaskRuntime& task : coordinator.GetTaskRuntimes())
    {
        ++tasksByState[TaskStateToString(task.state)];
        completedTasks += task.state == TASK_COMPLETED ? 1 : 0;
    }
    std::map<std::string, uint64_t> transfersByState;
    uint64_t completedTransfers = 0;
    for (const TransferSummaryRecord& transfer : transferSummaries)
    {
        ++transfersByState[transfer.transferState];
        completedTransfers += transfer.transferState == "COMPLETED" ? 1 : 0;
    }

    uint64_t queueDropPackets = 0;
    uint64_t queueDropBytes = 0;
    for (const QueueDropSummaryRecord& drop : queueDropSummaries)
    {
        queueDropPackets = CheckedAdd(queueDropPackets, drop.dropPackets, "queue drop packets");
        queueDropBytes = CheckedAdd(queueDropBytes, drop.dropBytes, "queue drop bytes");
    }
    uint64_t udpSocketDropPackets = 0;
    uint64_t udpSocketDropBytes = 0;
    for (const UdpSocketDropSummaryRecord& drop : udpSocketDropSummaries)
    {
        udpSocketDropPackets =
            CheckedAdd(udpSocketDropPackets, drop.dropPackets, "UDP socket drop packets");
        udpSocketDropBytes =
            CheckedAdd(udpSocketDropBytes, drop.dropBytes, "UDP socket drop bytes");
    }

    Json topDroppedLinks = Json::array();
    const std::size_t droppedLinkLimit = std::min<std::size_t>(10, queueDropSummaries.size());
    for (std::size_t index = 0; index < droppedLinkLimit; ++index)
    {
        const QueueDropSummaryRecord& drop = queueDropSummaries[index];
        topDroppedLinks.push_back({{"source_node_id", drop.sourceNodeId},
                                   {"destination_node_id", drop.destinationNodeId},
                                   {"output_interface", drop.outputInterface},
                                   {"drop_packets", drop.dropPackets},
                                   {"drop_bytes", drop.dropBytes},
                                   {"first_drop_time_ns", drop.firstDropTimeNs},
                                   {"last_drop_time_ns", drop.lastDropTimeNs}});
    }
    Json topPlannedLoadLinks = Json::array();
    for (const FlowLinkSummaryRecord& link : flowLinkSummaries)
    {
        if (topPlannedLoadLinks.size() == 10 || link.plannedApplicationBytes == 0)
        {
            break;
        }
        topPlannedLoadLinks.push_back(
            {{"source_node_id", link.sourceNodeId},
             {"destination_node_id", link.destinationNodeId},
             {"output_interface", link.outputInterface},
             {"unique_transfer_count", link.uniqueTransferCount},
             {"planned_application_bytes", link.plannedApplicationBytes},
             {"large_transfer_count", link.largeTransferCount},
             {"adjacent_to_compute_node", link.adjacentToComputeNode},
             {"drop_packets", link.dropPackets},
             {"drop_bytes", link.dropBytes}});
    }

    const Json summary = {
        {"run_status", "INCOMPLETE"},
        {"simulation_duration_s", static_cast<double>(simulationDurationNs) / 1000000000.0},
        {"task_count", coordinator.GetTaskRuntimes().size()},
        {"completed_task_count", completedTasks},
        {"incomplete_task_count", coordinator.GetTaskRuntimes().size() - completedTasks},
        {"tasks_by_state",
         {{"PENDING", tasksByState["PENDING"]},
          {"INPUT_TRANSFERRING", tasksByState["INPUT_TRANSFERRING"]},
          {"QUEUED", tasksByState["QUEUED"]},
          {"RUNNING", tasksByState["RUNNING"]},
          {"RESULT_TRANSFERRING", tasksByState["RESULT_TRANSFERRING"]},
          {"COMPLETED", tasksByState["COMPLETED"]}}},
        {"transfer_count", transferSummaries.size()},
        {"registered_transfer_count", transfersByState["REGISTERED"]},
        {"started_transfer_count", transfersByState["STARTED"]},
        {"completed_transfer_count", completedTransfers},
        {"incomplete_transfer_count", transferSummaries.size() - completedTransfers},
        {"flowmonitor_tx_packets", aggregate.txPackets},
        {"flowmonitor_rx_packets", aggregate.rxPackets},
        {"flowmonitor_lost_packets", aggregate.lostPackets},
        {"queue_drop_packets", queueDropPackets},
        {"queue_drop_bytes", queueDropBytes},
        {"dropped_directed_link_count", queueDropSummaries.size()},
        {"receiver_rcv_buf_bytes", runMetadata.receiverRcvBufBytes},
        {"udp_socket_drop_packets", udpSocketDropPackets},
        {"udp_socket_drop_bytes", udpSocketDropBytes},
        {"udp_socket_dropped_receiver_count", udpSocketDropSummaries.size()},
        {"top_dropped_links", topDroppedLinks},
        {"top_planned_load_links", topPlannedLoadLinks},
        {"compute_node_count", coordinator.GetComputeServices().size()},
        {"queue_bytes_per_device", runMetadata.islQueueBytes},
        {"ecmp_hash_seed", runMetadata.ecmpHashSeed}};
    std::ofstream output(OutputPath(outputDirectory, "diagnostic-summary.json"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write diagnostic-summary.json");
    }
    output << summary.dump(2) << '\n';
}

} // namespace

std::string
GetFailureDiagnosticDirectory(const std::string& outputDirectory)
{
    return (RootPath(outputDirectory) / "diagnostics" / "failure").string();
}

void
PrepareFailureDiagnosticDirectory(const std::string& outputDirectory)
{
    EnsureDirectory(RootPath(outputDirectory));
    EnsureDirectory(RootPath(outputDirectory) / "diagnostics");
    EnsureDirectory(GetFailureDiagnosticDirectory(outputDirectory));
}

void
RemoveFailureDiagnosticOutputs(const std::string& outputDirectory)
{
    static const std::vector<std::string> filenames = {
        "incomplete-tasks.csv",
        "incomplete-transfers.csv",
        "isl-queue-drops.csv",
        "isl-queue-drop-summary.csv",
        "udp-socket-drops.csv",
        "udp-socket-drop-summary.csv",
        "flow-link-concentration.csv",
        "flow-drop-reasons.csv",
        "diagnostic-summary.json"};
    const std::filesystem::path root = RootPath(outputDirectory);
    const std::filesystem::path diagnostics = root / "diagnostics";
    const std::filesystem::path failure = diagnostics / "failure";
    for (const std::string& filename : filenames)
    {
        RemoveKnownFile(root / filename);
        RemoveKnownFile(diagnostics / filename);
        RemoveKnownFile(failure / filename);
    }
    RemoveEmptyDirectory(failure);
    RemoveEmptyDirectory(diagnostics);
}

void
WriteFailureDiagnostics(const FlowAggregate& aggregate,
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
    WriteFailureDiagnosticsNs(aggregate,
                              SatComputeSecondsToNanoseconds(simulationDurationSeconds,
                                                             "simulationDuration"),
                              runMetadata,
                              transferFlows,
                              transferSummaries,
                              routeEvents,
                              directedLinks,
                              queueDropEvents,
                              udpSocketDropEvents,
                              coordinator,
                              outputDirectory);
}

void
WriteFailureDiagnosticsNs(const FlowAggregate& aggregate,
                          int64_t simulationDurationNs,
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
    if (!runMetadata.udpSocketDropCollectionEnabled)
    {
        throw std::runtime_error("failure diagnostics require UDP socket drop collection");
    }
    if (simulationDurationNs <= 0)
    {
        throw std::runtime_error("failure diagnostic duration must be positive");
    }
    PrepareFailureDiagnosticDirectory(outputDirectory);
    const std::string failureDirectory = GetFailureDiagnosticDirectory(outputDirectory);
    const std::vector<QueueDropSummaryRecord> queueDropSummaries =
        CollectQueueDropSummaries(directedLinks, queueDropEvents);
    const std::vector<UdpSocketDropSummaryRecord> udpSocketDropSummaries =
        CollectUdpSocketDropSummaries(udpSocketDropEvents, runMetadata);
    const std::vector<FlowLinkSummaryRecord> flowLinkSummaries =
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
                           simulationDurationNs,
                           runMetadata,
                           transferSummaries,
                           queueDropSummaries,
                           udpSocketDropSummaries,
                           flowLinkSummaries,
                           coordinator,
                           failureDirectory);
}

} // namespace ns3
