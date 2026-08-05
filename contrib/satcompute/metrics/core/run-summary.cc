/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Write one run-level summary while preserving both legacy and ns-3.48 evidence.

#include "run-summary.h"

#include "task-metrics.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace ns3
{

namespace
{

using Json = nlohmann::json;

struct TransferAggregate
{
    uint64_t transferCount{};
    uint64_t completedTransferCount{};
    uint64_t declaredBytes{};
    uint64_t sentBytes{};
    uint64_t receivedBytes{};
    uint64_t derivedPackets{};
    uint64_t sentPackets{};
    uint64_t receivedPackets{};
};

uint64_t
CheckedAdd(uint64_t left, uint64_t right, const std::string& field)
{
    if (left > std::numeric_limits<uint64_t>::max() - right)
    {
        throw std::runtime_error("run summary " + field + " overflow");
    }
    return left + right;
}

double
SafeDivide(double numerator, double denominator)
{
    return denominator > 0.0 ? numerator / denominator : 0.0;
}

std::filesystem::path
OutputPath(const std::string& directory)
{
    const std::filesystem::path root = directory.empty() ? "." : directory;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        throw std::runtime_error("cannot create metrics directory " + root.string() + ": " +
                                 error.message());
    }
    return root / "run-summary.json";
}

TransferAggregate
CollectTransfers(const std::vector<TransferSummaryRecord>& summaries)
{
    TransferAggregate aggregate;
    aggregate.transferCount = summaries.size();
    for (const TransferSummaryRecord& summary : summaries)
    {
        aggregate.declaredBytes =
            CheckedAdd(aggregate.declaredBytes, summary.declaredSizeBytes, "declared bytes");
        aggregate.sentBytes =
            CheckedAdd(aggregate.sentBytes, summary.sentApplicationBytes, "sent bytes");
        aggregate.receivedBytes = CheckedAdd(aggregate.receivedBytes,
                                             summary.receivedApplicationBytes,
                                             "received bytes");
        aggregate.derivedPackets = CheckedAdd(aggregate.derivedPackets,
                                              summary.derivedPacketCount,
                                              "derived packet count");
        aggregate.sentPackets =
            CheckedAdd(aggregate.sentPackets, summary.sentPacketCount, "sent packet count");
        aggregate.receivedPackets = CheckedAdd(aggregate.receivedPackets,
                                               summary.receivedPacketCount,
                                               "received packet count");
        if (summary.transferState == "COMPLETED")
        {
            ++aggregate.completedTransferCount;
        }
    }
    return aggregate;
}

bool
LegacyRunComplete(const RunMetadata& metadata,
                  const TransferAggregate& transfers,
                  const TaskAggregate& tasks,
                  const TaskCoordinator* taskCoordinator)
{
    const bool taskRunComplete =
        taskCoordinator == nullptr || tasks.completedTaskCount == tasks.taskCount;
    const bool capacityTransferRunComplete =
        metadata.routingMode != "global-capacity-aware-hrw" || taskCoordinator != nullptr ||
        transfers.transferCount == 0 ||
        transfers.completedTransferCount == transfers.transferCount;
    return taskRunComplete && capacityTransferRunComplete;
}

Json
BuildDropReasons(const FlowAggregate& aggregate)
{
    Json reasons = Json::array();
    const std::size_t reasonCount =
        std::max<std::size_t>(GetIpv4DropReasonCount(),
                              std::max(aggregate.droppedPacketsByReason.size(),
                                       aggregate.droppedBytesByReason.size()));
    for (std::size_t reason = 0; reason < reasonCount; ++reason)
    {
        const uint64_t packets = reason < aggregate.droppedPacketsByReason.size()
                                     ? aggregate.droppedPacketsByReason[reason]
                                     : 0;
        const uint64_t bytes = reason < aggregate.droppedBytesByReason.size()
                                   ? aggregate.droppedBytesByReason[reason]
                                   : 0;
        reasons.push_back({{"reason_code", reason},
                           {"reason_name",
                            GetIpv4DropReasonName(static_cast<uint32_t>(reason))},
                           {"dropped_packets", packets},
                           {"dropped_bytes", bytes}});
    }
    return reasons;
}

void
WriteSummary(const FlowAggregate& flow,
             const RunMetadata& metadata,
             const RunSummaryEvidence& evidence,
             const ApplicationMetrics& applications,
             const std::vector<TransferSummaryRecord>& transferSummaries,
             const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
             const TaskCoordinator* taskCoordinator,
             const std::string& outputDirectory)
{
    if (evidence.simulationDurationNs <= 0 || evidence.wallClockNs < 0)
    {
        throw std::runtime_error("run summary duration metadata is invalid");
    }
    const TransferAggregate transfers = CollectTransfers(transferSummaries);
    const TaskAggregate tasks = CollectTaskAggregate(taskCoordinator);
    uint64_t sentBytes = transfers.sentBytes;
    uint64_t receivedBytes = transfers.receivedBytes;
    if (transferSummaries.empty())
    {
        sentBytes = applications.sentBytes;
        receivedBytes = applications.receivedBytes;
    }
    else if (sentBytes != applications.sentBytes || receivedBytes != applications.receivedBytes)
    {
        throw std::runtime_error("transfer summaries and application metrics disagree");
    }

    if (!metadata.udpSocketDropCollectionEnabled && !udpSocketDropEvents.empty())
    {
        throw std::runtime_error("UDP socket drop events exist while collection is disabled");
    }
    uint64_t udpDropBytes = 0;
    std::set<std::pair<uint32_t, uint16_t>> droppedReceivers;
    for (const UdpSocketDropEvent& event : udpSocketDropEvents)
    {
        if (event.receiverRcvBufBytes != metadata.receiverRcvBufBytes)
        {
            throw std::runtime_error("UDP socket drop buffer differs from run metadata");
        }
        udpDropBytes = CheckedAdd(udpDropBytes, event.packetSizeBytes, "UDP drop bytes");
        droppedReceivers.emplace(event.destinationSatelliteId, event.destinationPort);
    }

    const double durationSeconds =
        static_cast<double>(evidence.simulationDurationNs) / 1000000000.0;
    const double wallClockSeconds = static_cast<double>(evidence.wallClockNs) / 1000000000.0;
    Json summary = {
        {"schema_version", "0.1"},
        {"simulation_duration_s", durationSeconds},
        {"wall_clock_s", wallClockSeconds},
        {"mode", metadata.mode},
        {"run_status", evidence.runComplete ? "COMPLETE" : "PARTIAL"},
        {"task_completion_policy", metadata.taskCompletionPolicy},
        {"routing_mode", metadata.routingMode},
        {"ecmp_hash_seed", metadata.ecmpHashSeed},
        {"isl_mtu_bytes", metadata.islMtuBytes},
        {"isl_queue_bytes", metadata.islQueueBytes},
        {"receiver_rcv_buf_bytes", metadata.receiverRcvBufBytes},
        {"udp_socket_drop_collection_enabled", metadata.udpSocketDropCollectionEnabled},
        {"diagnostic_mode", metadata.diagnosticMode},
        {"pacing_mode", metadata.pacingMode},
        {"transfer_chunk_mode", metadata.transferChunkMode},
        {"transfer_count", transfers.transferCount},
        {"declared_application_bytes", transfers.declaredBytes},
        {"sent_application_bytes", sentBytes},
        {"received_application_bytes", receivedBytes},
        {"derived_udp_packets", transfers.derivedPackets},
        {"flow_monitor_tx_packets", flow.txPackets},
        {"flow_monitor_rx_packets", flow.rxPackets},
        {"flow_monitor_lost_packets", flow.lostPackets},
        {"flow_monitor_reported_drop_packets", flow.ReportedDropPackets()},
        {"flow_monitor_unattributed_lost_packets", flow.UnattributedLostPackets()},
        {"flow_monitor_drop_reasons", BuildDropReasons(flow)},
        {"compute_node_count", tasks.computeNodeCount},
        {"task_count", tasks.taskCount},
        {"completed_task_count", tasks.completedTaskCount},
        {"task_completion_rate_percent",
         SafeDivide(tasks.completedTaskCount * 100.0, tasks.taskCount)},
        {"total_input_bytes", tasks.totalInputBytes},
        {"total_output_bytes", tasks.totalOutputBytes},
        {"total_compute_work_units", tasks.totalComputeWorkUnits},
        {"mean_task_completion_delay_ns", tasks.meanCompletionDelayNs},
        {"max_task_completion_delay_ns", tasks.maxCompletionDelayNs},
        {"run_name", evidence.runName},
        {"config_schema_version", evidence.configSchemaVersion},
        {"effective_config",
         {{"path", evidence.effectiveConfigPath.string()},
          {"sha256", evidence.effectiveConfigSha256}}},
        {"simulation_duration_ns", evidence.simulationDurationNs},
        {"wall_clock_ns", evidence.wallClockNs},
        {"workload_mode", evidence.workloadMode},
        {"topology_source", evidence.topologySource},
        {"applied_topology_slice_count", evidence.appliedTopologySliceCount},
        {"hash_seed", metadata.ecmpHashSeed},
        {"route_computation_count", evidence.routeComputationCount},
        {"diagnostics_generated", evidence.diagnosticsGenerated},
        {"transfer",
         {{"sink_application_count", applications.sinkApplications},
          {"transfer_count", transfers.transferCount},
          {"completed_transfer_count", transfers.completedTransferCount},
          {"declared_application_bytes", transfers.declaredBytes},
          {"sent_application_bytes", sentBytes},
          {"received_application_bytes", receivedBytes},
          {"derived_udp_packets", transfers.derivedPackets},
          {"sent_udp_packets", transfers.sentPackets},
          {"received_udp_packets", transfers.receivedPackets},
          {"udp_socket_drop_packets", udpSocketDropEvents.size()},
          {"udp_socket_drop_bytes", udpDropBytes},
          {"udp_socket_dropped_receiver_count", droppedReceivers.size()}}},
        {"task",
         {{"compute_node_count", tasks.computeNodeCount},
          {"task_count", tasks.taskCount},
          {"completed_task_count", tasks.completedTaskCount},
          {"total_input_bytes", tasks.totalInputBytes},
          {"total_output_bytes", tasks.totalOutputBytes},
          {"total_compute_work_units", tasks.totalComputeWorkUnits},
          {"mean_completion_delay_ns", tasks.meanCompletionDelayNs},
          {"maximum_completion_delay_ns", tasks.maxCompletionDelayNs}}}};

    summary["fixed_payload_bytes"] = metadata.transferChunkMode == "fixed"
                                           ? Json(metadata.fixedPayloadBytes)
                                           : Json(nullptr);
    summary["compute_profile_path"] = metadata.computeProfilePath.empty()
                                           ? Json(nullptr)
                                           : Json(metadata.computeProfilePath);
    summary["task_trace_path"] =
        metadata.taskTracePath.empty() ? Json(nullptr) : Json(metadata.taskTracePath);
    if (metadata.udpSocketDropCollectionEnabled)
    {
        summary["udp_socket_drop_packets"] = udpSocketDropEvents.size();
        summary["udp_socket_drop_bytes"] = udpDropBytes;
        summary["udp_socket_dropped_receiver_count"] = droppedReceivers.size();
    }
    else
    {
        summary["udp_socket_drop_packets"] = nullptr;
        summary["udp_socket_drop_bytes"] = nullptr;
        summary["udp_socket_dropped_receiver_count"] = nullptr;
    }

    std::ofstream output(OutputPath(outputDirectory), std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write run-summary.json");
    }
    output << summary.dump(2) << '\n';
}

} // namespace

void
WriteRunSummary(const FlowAggregate& aggregate,
                double simulationDurationSeconds,
                double wallClockSeconds,
                const RunMetadata& runMetadata,
                const ApplicationMetrics& applicationMetrics,
                const std::vector<TransferSummaryRecord>& transferSummaries,
                const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
                const TaskCoordinator* taskCoordinator,
                const std::string& outputDirectory)
{
    const TransferAggregate transfers = CollectTransfers(transferSummaries);
    const TaskAggregate tasks = CollectTaskAggregate(taskCoordinator);
    const RunSummaryEvidence evidence = {
        "",
        "",
        {},
        "",
        static_cast<int64_t>(simulationDurationSeconds * 1000000000.0),
        static_cast<int64_t>(wallClockSeconds * 1000000000.0),
        runMetadata.mode == "task"
            ? "task"
            : (runMetadata.mode == "network-transfer" ? "transfer" : "none"),
        "",
        0,
        0,
        LegacyRunComplete(runMetadata, transfers, tasks, taskCoordinator),
        false};
    WriteSummary(aggregate,
                 runMetadata,
                 evidence,
                 applicationMetrics,
                 transferSummaries,
                 udpSocketDropEvents,
                 taskCoordinator,
                 outputDirectory);
}

void
WriteRunSummaryWithEvidence(
    const FlowAggregate& aggregate,
    const RunMetadata& runMetadata,
    const RunSummaryEvidence& evidence,
    const ApplicationMetrics& applicationMetrics,
    const std::vector<TransferSummaryRecord>& transferSummaries,
    const std::vector<UdpSocketDropEvent>& udpSocketDropEvents,
    const TaskCoordinator* taskCoordinator,
    const std::string& outputDirectory)
{
    WriteSummary(aggregate,
                 runMetadata,
                 evidence,
                 applicationMetrics,
                 transferSummaries,
                 udpSocketDropEvents,
                 taskCoordinator,
                 outputDirectory);
}

} // namespace ns3
