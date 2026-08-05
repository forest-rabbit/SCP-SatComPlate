/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Persist only metrics backed by the current runtime's explicit data sources.

#include "run-output-writer.h"

#include "core/flow-metrics.h"
#include "core/run-summary.h"
#include "core/task-metrics.h"
#include "core/transfer-metrics.h"
#include "diagnostics/failure-diagnostics.h"
#include "diagnostics/flow-drop-reason-diagnostics.h"
#include "routing/capacity-aware-metrics.h"
#include "routing/ecmp-metrics.h"
#include "routing/size-aware-metrics.h"
#include "../model/sha256.h"
#include "../third-party/nlohmann/json.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
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
        throw RunOutputError("run output " + field + " overflow");
    }
    return left + right;
}

void
WriteTextFile(const std::filesystem::path& filename, const std::string& content)
{
    std::error_code error;
    std::filesystem::create_directories(filename.parent_path(), error);
    if (error)
    {
        throw RunOutputError("cannot create output directory " +
                             filename.parent_path().string() + ": " + error.message());
    }
    std::filesystem::path temporary = filename;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw RunOutputError("cannot write output file " + temporary.string());
        }
        output << content;
        output.close();
        if (!output)
        {
            throw RunOutputError("cannot finish output file " + temporary.string());
        }
    }
    std::filesystem::rename(temporary, filename, error);
    if (error)
    {
        std::filesystem::remove(temporary);
        throw RunOutputError("cannot publish output file " + filename.string() + ": " +
                             error.message());
    }
}

std::filesystem::path
WriteCsv(const std::filesystem::path& outputDirectory,
         const std::string& filename,
         const std::string& content)
{
    const std::filesystem::path path = outputDirectory / filename;
    WriteTextFile(path, content);
    return path;
}

std::filesystem::path
WriteJson(const std::filesystem::path& outputDirectory,
          const std::string& filename,
          const Json& content)
{
    const std::filesystem::path path = outputDirectory / filename;
    WriteTextFile(path, content.dump(2) + "\n");
    return path;
}

TransferAggregate
CollectTransferAggregate(const std::vector<TransferSummaryRecord>& summaries)
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
                                              "derived packets");
        aggregate.sentPackets =
            CheckedAdd(aggregate.sentPackets, summary.sentPacketCount, "sent packets");
        aggregate.receivedPackets = CheckedAdd(aggregate.receivedPackets,
                                               summary.receivedPacketCount,
                                               "received packets");
        if (summary.transferState == "COMPLETED")
        {
            ++aggregate.completedTransferCount;
        }
    }
    return aggregate;
}

std::filesystem::path
WriteReservationEvents(const std::filesystem::path& outputDirectory,
                       Ptr<FlowRouteRegistry> registry)
{
    std::ostringstream output;
    output << "simulation_time_ns,action,selection_reason,route_epoch,node_id,transfer_id,"
              "declared_bytes,source_address,destination_address,protocol,source_port,"
              "destination_port,gateway,output_interface,route_destination,"
              "route_destination_mask,candidate_reserved_before,candidate_reserved_after,"
              "total_reserved_before,total_reserved_after\n";
    for (const FlowRouteReservationEvent& event : registry->GetEvents())
    {
        output << event.simulationTimeNs << ',' << event.action << ',' << event.selectionReason
               << ',' << event.routeEpoch << ',' << event.nodeId << ',' << event.transferId << ','
               << event.declaredBytes << ',' << event.flowKey.sourceAddress << ','
               << event.flowKey.destinationAddress << ','
               << static_cast<uint32_t>(event.flowKey.protocol) << ',' << event.flowKey.sourcePort
               << ',' << event.flowKey.destinationPort << ',' << event.candidate.gateway << ','
               << event.candidate.outputInterface << ',' << event.candidate.destination << ','
               << event.candidate.destinationMask << ',' << event.candidateReservedBefore << ','
               << event.candidateReservedAfter << ',' << event.totalReservedBefore << ','
               << event.totalReservedAfter << '\n';
    }
    return WriteCsv(outputDirectory, "routing-reservation-events.csv", output.str());
}

std::filesystem::path
WriteRoutingSummary(const ResolvedSatComputeConfig& config,
                    const RunOutputContext& context,
                    const std::filesystem::path& outputDirectory)
{
    Json routing = {{"schema_version", "0.1"},
                    {"routing_mode", config.routing.mode},
                    {"hash_seed", config.routing.hashSeed},
                    {"route_computation_count", context.routeComputationCount}};
    if (context.flowRouteRegistry == nullptr)
    {
        routing["flow_registry"] = nullptr;
    }
    else
    {
        routing["flow_registry"] = {
            {"registered_flow_count", context.flowRouteRegistry->GetRegisteredFlowCount()},
            {"active_flow_count_at_end", context.flowRouteRegistry->GetActiveFlowCount()},
            {"assignment_count_at_end", context.flowRouteRegistry->GetAssignmentCount()},
            {"reservation_event_count", context.flowRouteRegistry->GetEvents().size()},
            {"reserved_bytes_at_end", context.flowRouteRegistry->GetTotalReservedBytes()},
            {"peak_reserved_bytes", context.flowRouteRegistry->GetPeakReservedBytes()},
            {"peak_candidate_reserved_bytes",
             context.flowRouteRegistry->GetPeakCandidateReservedBytes()}};
    }
    if (context.capacityAwareSummary)
    {
        routing["capacity_aware"] = {
            {"active_path_count_at_end", context.capacityAwareSummary->activePathCountAtEnd},
            {"reserved_directed_link_count_at_end",
             context.capacityAwareSummary->reservedDirectedLinkCountAtEnd},
            {"total_reserved_rate_bps_at_end",
             context.capacityAwareSummary->totalReservedRateBpsAtEnd},
            {"pending_transfer_count_at_end",
             context.capacityAwareSummary->pendingTransferCountAtEnd}};
    }
    else
    {
        routing["capacity_aware"] = nullptr;
    }
    return WriteJson(outputDirectory, "routing-summary.json", routing);
}

void
ValidateInputs(const ResolvedSatComputeConfig& config,
               Ptr<NetworkTransferEngine> transferEngine,
               Ptr<TaskCoordinator> taskCoordinator)
{
    const bool directMode = config.workloads.transferTrace.has_value();
    const bool taskMode = config.workloads.computeProfile.has_value() &&
                          config.workloads.taskTrace.has_value();
    if (directMode)
    {
        if (transferEngine == nullptr || taskCoordinator != nullptr)
        {
            throw RunOutputError("direct-transfer config and runtime outputs disagree");
        }
    }
    else if (taskMode)
    {
        if (transferEngine == nullptr || taskCoordinator == nullptr ||
            taskCoordinator->GetTransferEngine() != transferEngine)
        {
            throw RunOutputError("task config and runtime outputs disagree");
        }
    }
    else if (transferEngine != nullptr || taskCoordinator != nullptr)
    {
        throw RunOutputError("workload-free config has runtime workload outputs");
    }
}

} // namespace

RunOutputResult
WriteRunOutputs(const ResolvedSatComputeConfig& config,
                const RunOutputContext& context,
                Ptr<NetworkTransferEngine> transferEngine,
                Ptr<TaskCoordinator> taskCoordinator)
{
    ValidateInputs(config, transferEngine, taskCoordinator);
    const bool reservationAware = config.routing.mode == "global-size-aware-hrw" ||
                                  config.routing.mode == "global-capacity-aware-hrw";
    const bool capacityAware = config.routing.mode == "global-capacity-aware-hrw";
    if (reservationAware != (context.flowRouteRegistry != nullptr))
    {
        throw RunOutputError("routing mode and flow-registry output context disagree");
    }
    if (capacityAware != context.capacityAwareSummary.has_value())
    {
        throw RunOutputError("routing mode and capacity-aware output context disagree");
    }
    if (config.simulation.durationNs <= 0 || context.wallClockNs < 0)
    {
        throw RunOutputError("run output has invalid duration metadata");
    }
    if (context.flowMonitor == nullptr)
    {
        throw RunOutputError("run output requires the simulation FlowMonitor");
    }
    std::error_code error;
    const std::filesystem::path effectiveConfig =
        std::filesystem::weakly_canonical(context.effectiveConfigPath, error);
    if (error || !std::filesystem::is_regular_file(effectiveConfig))
    {
        throw RunOutputError("effective config must be an existing regular file");
    }
    const std::filesystem::path outputDirectory =
        std::filesystem::absolute(context.outputDirectory).lexically_normal();

    std::vector<TransferSummaryRecord> transfers;
    std::vector<TransferFlowMetadata> transferFlows;
    std::vector<UdpSocketDropEvent> udpDrops;
    ApplicationMetrics applicationMetrics;
    if (transferEngine != nullptr)
    {
        transfers = transferEngine->CollectSummaries();
        transferFlows = transferEngine->CollectFlowMetadata();
        udpDrops = transferEngine->CollectUdpSocketDropEvents();
        applicationMetrics = transferEngine->CollectApplicationMetrics();
    }
    const FlowAggregate flowAggregate = CollectFlowAggregate(context.flowMonitor);
    const TransferAggregate transferAggregate = CollectTransferAggregate(transfers);
    const TaskAggregate taskAggregate = CollectTaskAggregate(PeekPointer(taskCoordinator));
    if (transferAggregate.sentBytes != applicationMetrics.sentBytes ||
        transferAggregate.receivedBytes != applicationMetrics.receivedBytes)
    {
        throw RunOutputError("transfer summaries and application metrics disagree");
    }

    const bool transfersComplete =
        transferAggregate.completedTransferCount == transferAggregate.transferCount;
    const bool tasksComplete = taskAggregate.completedTaskCount == taskAggregate.taskCount;
    const bool complete = transfersComplete && tasksComplete;
    if (complete && context.flowRouteRegistry != nullptr &&
        (context.flowRouteRegistry->GetActiveFlowCount() != 0 ||
         context.flowRouteRegistry->GetAssignmentCount() != 0 ||
         context.flowRouteRegistry->GetTotalReservedBytes() != 0))
    {
        throw RunOutputError("complete run leaked flow reservation state");
    }
    if (complete && context.capacityAwareSummary &&
        (context.capacityAwareSummary->activePathCountAtEnd != 0 ||
         context.capacityAwareSummary->reservedDirectedLinkCountAtEnd != 0 ||
         context.capacityAwareSummary->totalReservedRateBpsAtEnd != 0 ||
         context.capacityAwareSummary->pendingTransferCountAtEnd != 0))
    {
        throw RunOutputError("complete run leaked capacity-aware path state");
    }

    const std::string workloadMode = taskCoordinator != nullptr
                                         ? "task"
                                         : (transferEngine != nullptr ? "transfer" : "none");
    const std::string runMode = workloadMode == "transfer" ? "network-transfer" : workloadMode;
    const std::string pacingMode = transfers.empty()
                                       ? (transferEngine == nullptr
                                              ? "none"
                                              : (capacityAware
                                                     ? "path-bottleneck-serialization"
                                                     : "first-hop-serialization"))
                                       : transfers.front().pacingMode;
    const RunMetadata runMetadata = {
        runMode,
        config.routing.mode,
        config.routing.hashSeed,
        config.network.islMtuBytes,
        config.network.islQueueBytes,
        config.network.receiverRcvBufBytes,
        transferEngine != nullptr && config.logging.diagnosticMode == "failure",
        config.logging.diagnosticMode,
        config.workloads.taskCompletionPolicy,
        pacingMode,
        transferEngine != nullptr ? config.workloads.transferChunkMode : "none",
        transferEngine != nullptr && config.workloads.transferChunkMode == "fixed"
            ? config.workloads.transferPayloadBytes
            : 0,
        config.workloads.computeProfile ? config.workloads.computeProfile->string() : "",
        config.workloads.taskTrace ? config.workloads.taskTrace->string() : ""};

    RemoveFailureDiagnosticOutputs(outputDirectory.string());

    RunOutputResult result;
    result.complete = complete;
    WriteNetworkMetrics(flowAggregate, outputDirectory.string());
    result.files.push_back(outputDirectory / "network-flow-metrics.csv");
    WriteNetworkFlowDetails(context.flowMonitor,
                            transferFlows,
                            outputDirectory.string(),
                            complete);
    result.files.push_back(outputDirectory / "network-flow-details.csv");
    WriteEcmpRouteEvents(context.routeEvents, outputDirectory.string());
    result.files.push_back(outputDirectory / "ecmp-route-events.csv");
    if (transferEngine != nullptr)
    {
        WriteTransferSummaries(transfers, outputDirectory.string());
        result.files.push_back(outputDirectory / "transfer-summary.csv");
    }
    if (taskCoordinator != nullptr)
    {
        WriteTaskMetricsNs(*taskCoordinator,
                           config.simulation.durationNs,
                           outputDirectory.string());
        result.files.push_back(outputDirectory / "task-events.csv");
        result.files.push_back(outputDirectory / "task-summary.csv");
        result.files.push_back(outputDirectory / "compute-node-summary.csv");
    }
    if (context.flowRouteRegistry != nullptr)
    {
        WriteSizeAwareMetrics(context.flowRouteRegistry, outputDirectory.string());
        result.files.push_back(outputDirectory / "size-aware-reservation-events.csv");
        result.files.push_back(outputDirectory / "size-aware-summary.json");
        result.files.push_back(WriteReservationEvents(outputDirectory,
                                                      context.flowRouteRegistry));
    }
    else
    {
        RemoveSizeAwareMetrics(outputDirectory.string());
    }
    if (context.capacityAwareSummary)
    {
        WriteCapacityAwareMetrics(*context.capacityAwareSummary, outputDirectory.string());
        result.files.push_back(outputDirectory / "capacity-aware-summary.json");
    }
    else
    {
        RemoveCapacityAwareMetrics(outputDirectory.string());
    }
    result.files.push_back(WriteRoutingSummary(config, context, outputDirectory));

    const bool writeFailureDiagnostics = config.logging.diagnosticMode == "failure" &&
                                         taskCoordinator != nullptr && !tasksComplete;
    const bool writeFlowDropReasons = config.logging.diagnosticMode == "failure" &&
                                      (runMode == "network-transfer" ||
                                       (taskCoordinator != nullptr && !tasksComplete));
    const std::filesystem::path failureDirectory =
        outputDirectory / "diagnostics" / "failure";
    if (writeFlowDropReasons)
    {
        WriteFlowDropReasons(context.flowMonitor, transferFlows, outputDirectory.string());
        result.files.push_back(failureDirectory / "flow-drop-reasons.csv");
    }
    if (writeFailureDiagnostics)
    {
        WriteFailureDiagnosticsNs(flowAggregate,
                                  config.simulation.durationNs,
                                  runMetadata,
                                  transferFlows,
                                  transfers,
                                  context.routeEvents,
                                  context.directedLinks,
                                  context.queueDropEvents,
                                  udpDrops,
                                  *taskCoordinator,
                                  outputDirectory.string());
        static const std::vector<std::string> fullDiagnosticFiles = {
            "incomplete-tasks.csv",
            "incomplete-transfers.csv",
            "isl-queue-drops.csv",
            "isl-queue-drop-summary.csv",
            "udp-socket-drops.csv",
            "udp-socket-drop-summary.csv",
            "flow-link-concentration.csv",
            "diagnostic-summary.json"};
        for (const std::string& filename : fullDiagnosticFiles)
        {
            result.files.push_back(failureDirectory / filename);
        }
    }
    result.diagnosticsGenerated = writeFlowDropReasons || writeFailureDiagnostics;
    const RunSummaryEvidence evidence = {config.runName,
                                         config.schemaVersion,
                                         effectiveConfig,
                                         Sha256File(effectiveConfig),
                                         config.simulation.durationNs,
                                         context.wallClockNs,
                                         workloadMode,
                                         config.network.topologySource,
                                         context.appliedTopologySliceCount,
                                         context.routeComputationCount,
                                         complete,
                                         result.diagnosticsGenerated};
    WriteRunSummaryWithEvidence(flowAggregate,
                                runMetadata,
                                evidence,
                                applicationMetrics,
                                transfers,
                                udpDrops,
                                PeekPointer(taskCoordinator),
                                outputDirectory.string());
    result.runSummaryPath = outputDirectory / "run-summary.json";
    result.files.push_back(result.runSummaryPath);
    return result;
}

} // namespace ns3
