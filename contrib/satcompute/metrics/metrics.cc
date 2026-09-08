/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Orchestrate the restored legacy metric layers with ns-3.48 runtime evidence.

#include "metrics.h"

#include "ns3/fault-controller.h"
#include "ns3/fault-model-engine.h"
#include "ns3/fault-prediction-engine.h"
#include "core/fault-metrics.h"
#include "core/flow-metrics.h"
#include "core/run-summary.h"
#include "core/task-metrics.h"
#include "core/transfer-metrics.h"
#include "diagnostics/failure-diagnostics.h"
#include "diagnostics/flow-drop-reason-diagnostics.h"
#include "routing/capacity-aware-metrics.h"
#include "routing/ecmp-metrics.h"
#include "routing/size-aware-metrics.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace ns3
{

namespace
{

struct TransferAggregate
{
    uint64_t transferCount{};
    uint64_t completedTransferCount{};
    uint64_t terminalTransferCount{};
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
        throw MetricsError("metrics " + field + " overflow");
    }
    return left + right;
}

void
RemoveObsoleteWriterOutputs(const std::filesystem::path& outputDirectory)
{
    static const std::vector<std::string> filenames = {"routing-summary.json",
                                                        "routing-reservation-events.csv"};
    for (const std::string& filename : filenames)
    {
        const std::filesystem::path path = outputDirectory / filename;
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            continue;
        }
        if (error)
        {
            throw MetricsError("cannot inspect obsolete writer output " + path.string() +
                               ": " + error.message());
        }
        if (!std::filesystem::is_regular_file(status))
        {
            continue;
        }
        std::filesystem::remove(path, error);
        if (error)
        {
            throw MetricsError("cannot remove obsolete writer output " + path.string() +
                               ": " + error.message());
        }
    }
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
        if (summary.transferState == "COMPLETED" || summary.transferState == "FAILED" ||
            summary.transferState == "CANCELLED")
        {
            ++aggregate.terminalTransferCount;
        }
    }
    return aggregate;
}

void
ValidateInputs(const SatComputeConfig& config,
               Ptr<FaultController> faultController,
               Ptr<FaultModelEngine> faultModelEngine,
               Ptr<FaultPredictionEngine> faultPredictionEngine,
               Ptr<NetworkTransferEngine> transferEngine,
               Ptr<TaskCoordinator> taskCoordinator)
{
    if ((config.faultMode == "none") != (faultController == nullptr))
    {
        throw MetricsError("fault config and runtime metrics disagree");
    }
    if ((config.faultMode == "generate") != (faultModelEngine != nullptr))
    {
        throw MetricsError("fault generation and runtime metrics disagree");
    }
    const bool taskMode = !config.computeProfile.empty() && !config.taskTrace.empty();
    if (config.faultProbabilityAudit != (faultPredictionEngine != nullptr))
    {
        throw MetricsError("fault probability audit and runtime metrics disagree");
    }
    if (faultPredictionEngine != nullptr &&
        (faultController == nullptr || !taskMode))
    {
        throw MetricsError("fault prediction and runtime metrics disagree");
    }
    if (taskMode)
    {
        if (transferEngine == nullptr || taskCoordinator == nullptr ||
            taskCoordinator->GetTransferEngine() != transferEngine)
        {
            throw MetricsError("task config and runtime metrics disagree");
        }
    }
    else if (transferEngine != nullptr || taskCoordinator != nullptr)
    {
        throw MetricsError("workload-free config has runtime workload metrics");
    }
}

} // namespace

MetricsRecorder::MetricsRecorder(const SatComputeConfig& config,
                                 MetricsRuntimeContext context,
                                 Ptr<NetworkTransferEngine> transferEngine,
                                 Ptr<TaskCoordinator> taskCoordinator)
    : m_config(config),
      m_context(std::move(context)),
      m_transferEngine(transferEngine),
      m_taskCoordinator(taskCoordinator)
{
}

MetricsRecordResult
MetricsRecorder::Record()
{
    const SatComputeConfig& config = m_config;
    const MetricsRuntimeContext& context = m_context;
    const Ptr<NetworkTransferEngine> transferEngine = m_transferEngine;
    const Ptr<TaskCoordinator> taskCoordinator = m_taskCoordinator;
    ValidateInputs(config,
                   context.faultController,
                   context.faultModelEngine,
                   context.faultPredictionEngine,
                   transferEngine,
                   taskCoordinator);
    const bool reservationAware = config.routingMode == "global-size-aware-hrw" ||
                                  config.routingMode == "global-capacity-aware-hrw";
    const bool capacityAware = config.routingMode == "global-capacity-aware-hrw";
    if (reservationAware != (context.flowRouteRegistry != nullptr))
    {
        throw MetricsError("routing mode and flow-registry metrics context disagree");
    }
    if (capacityAware != context.capacityAwareSummary.has_value())
    {
        throw MetricsError("routing mode and capacity-aware metrics context disagree");
    }
    if (context.simulationDurationNs <= 0 || context.wallClockNs < 0)
    {
        throw MetricsError("metrics context has invalid duration metadata");
    }
    if (context.flowMonitor == nullptr)
    {
        throw MetricsError("metrics context requires the simulation FlowMonitor");
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
        throw MetricsError("transfer summaries and application metrics disagree");
    }

    const bool transfersComplete =
        transferAggregate.completedTransferCount == transferAggregate.transferCount;
    const bool transfersSettled =
        transferAggregate.terminalTransferCount == transferAggregate.transferCount;
    const bool tasksComplete = taskAggregate.completedTaskCount == taskAggregate.taskCount;
    const bool complete = transfersComplete && tasksComplete;
    if (transfersSettled && context.flowRouteRegistry != nullptr &&
        (context.flowRouteRegistry->GetActiveFlowCount() != 0 ||
         context.flowRouteRegistry->GetAssignmentCount() != 0 ||
         context.flowRouteRegistry->GetTotalReservedBytes() != 0))
    {
        throw MetricsError("terminal transfers leaked flow reservation state");
    }
    if (transfersSettled && context.capacityAwareSummary &&
        (context.capacityAwareSummary->activePathCountAtEnd != 0 ||
         context.capacityAwareSummary->reservedDirectedLinkCountAtEnd != 0 ||
         context.capacityAwareSummary->totalReservedRateBpsAtEnd != 0 ||
         context.capacityAwareSummary->pendingTransferCountAtEnd != 0))
    {
        throw MetricsError("terminal transfers leaked capacity-aware path state");
    }

    const std::string workloadMode = taskCoordinator != nullptr ? "task" : "none";
    const std::string runMode = workloadMode;
    const std::string pacingMode = transfers.empty()
                                       ? (transferEngine == nullptr
                                              ? "none"
                                              : (capacityAware
                                                     ? "path-bottleneck-serialization"
                                                     : "first-hop-serialization"))
                                       : transfers.front().pacingMode;
    const RunMetadata runMetadata = {
        runMode,
        config.routingMode,
        config.ecmpHashSeed,
        config.islMtuBytes,
        config.islQueueBytes,
        config.receiverRcvBufBytes,
        transferEngine != nullptr && config.diagnosticMode == "failure",
        config.diagnosticMode,
        config.taskCompletionPolicy,
        pacingMode,
        transferEngine != nullptr ? config.transferChunkMode : "none",
        transferEngine != nullptr && config.transferChunkMode == "fixed"
            ? config.transferPayloadBytes
            : 0,
        config.computeProfile,
        config.taskTrace};

    RemoveFailureDiagnosticOutputs(outputDirectory.string());
    RemoveObsoleteWriterOutputs(outputDirectory);

    MetricsRecordResult result;
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
                           context.simulationDurationNs,
                           outputDirectory.string());
        result.files.push_back(outputDirectory / "task-events.csv");
        result.files.push_back(outputDirectory / "task-summary.csv");
        result.files.push_back(outputDirectory / "compute-node-summary.csv");
    }
    if (context.faultController != nullptr)
    {
        WriteFaultMetrics(*context.faultController,
                          PeekPointer(context.faultModelEngine),
                          PeekPointer(context.faultPredictionEngine),
                          PeekPointer(taskCoordinator),
                          transfers,
                          outputDirectory.string());
        result.files.push_back(outputDirectory / "fault-events.csv");
        result.files.push_back(outputDirectory / "fault-summary.json");
        if (taskCoordinator != nullptr)
        {
            result.files.push_back(outputDirectory / "fault-task-impact.csv");
        }
        if (context.faultPredictionEngine != nullptr)
        {
            result.files.push_back(outputDirectory / "fault-predictions.csv");
            result.files.push_back(outputDirectory /
                                   "fault-prediction-summary.json");
        }
        if (context.faultModelEngine != nullptr &&
            context.faultPredictionEngine != nullptr)
        {
            result.files.push_back(outputDirectory /
                                   "fault-model-probabilities.csv");
            result.files.push_back(outputDirectory / "fault-model-state.csv");
        }
    }
    else
    {
        RemoveFaultMetrics(outputDirectory.string());
    }
    if (context.flowRouteRegistry != nullptr)
    {
        WriteSizeAwareMetrics(context.flowRouteRegistry, outputDirectory.string());
        result.files.push_back(outputDirectory / "size-aware-reservation-events.csv");
        result.files.push_back(outputDirectory / "size-aware-summary.json");
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
    const bool writeFailureDiagnostics = config.diagnosticMode == "failure" &&
                                         taskCoordinator != nullptr && !tasksComplete;
    const bool writeFlowDropReasons = config.diagnosticMode == "failure" &&
                                      taskCoordinator != nullptr && !tasksComplete;
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
                                  context.simulationDurationNs,
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
    const RunSummaryEvidence evidence = {context.simulationDurationNs,
                                         context.wallClockNs,
                                         workloadMode,
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
