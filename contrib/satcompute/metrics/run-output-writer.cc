/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Persist only metrics backed by the current runtime's explicit data sources.

#include "run-output-writer.h"

#include "../model/sha256.h"
#include "../third-party/nlohmann/json.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
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

struct TaskAggregate
{
    uint64_t computeNodeCount{};
    uint64_t taskCount{};
    uint64_t completedTaskCount{};
    uint64_t totalInputBytes{};
    uint64_t totalOutputBytes{};
    uint64_t totalComputeWorkUnits{};
    uint64_t totalCompletionDelayNs{};
    uint64_t meanCompletionDelayNs{};
    uint64_t maximumCompletionDelayNs{};
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

int64_t
OptionalDifference(int64_t endTimeNs, int64_t startTimeNs, const std::string& field)
{
    if (endTimeNs < 0 || startTimeNs < 0)
    {
        return -1;
    }
    if (endTimeNs < startTimeNs)
    {
        throw RunOutputError(field + " has decreasing timestamps");
    }
    return endTimeNs - startTimeNs;
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

TaskAggregate
CollectTaskAggregate(Ptr<TaskCoordinator> coordinator)
{
    TaskAggregate aggregate;
    if (coordinator == nullptr)
    {
        return aggregate;
    }
    aggregate.computeNodeCount = coordinator->GetComputeServices().size();
    aggregate.taskCount = coordinator->GetTaskRuntimes().size();
    for (const TaskRuntime& task : coordinator->GetTaskRuntimes())
    {
        aggregate.totalInputBytes = CheckedAdd(aggregate.totalInputBytes,
                                               task.definition.inputBytes,
                                               "task input bytes");
        aggregate.totalOutputBytes = CheckedAdd(aggregate.totalOutputBytes,
                                                task.definition.outputBytes,
                                                "task output bytes");
        aggregate.totalComputeWorkUnits = CheckedAdd(aggregate.totalComputeWorkUnits,
                                                     task.definition.computeWorkUnits,
                                                     "task compute work");
        if (task.state != TASK_COMPLETED)
        {
            continue;
        }
        ++aggregate.completedTaskCount;
        const int64_t delay = OptionalDifference(task.resultTransferCompleteTimeNs,
                                                 task.definition.arrivalTimeNs,
                                                 "task completion delay");
        aggregate.totalCompletionDelayNs = CheckedAdd(aggregate.totalCompletionDelayNs,
                                                      static_cast<uint64_t>(delay),
                                                      "task completion delay");
        aggregate.maximumCompletionDelayNs =
            std::max(aggregate.maximumCompletionDelayNs, static_cast<uint64_t>(delay));
    }
    if (aggregate.completedTaskCount > 0)
    {
        aggregate.meanCompletionDelayNs =
            aggregate.totalCompletionDelayNs / aggregate.completedTaskCount;
    }
    return aggregate;
}

std::filesystem::path
WriteTransferSummaries(const std::filesystem::path& outputDirectory,
                       const std::vector<TransferSummaryRecord>& summaries)
{
    std::ostringstream output;
    output << "transfer_id,source_node_id,destination_node_id,source_address,"
              "destination_address,source_port,destination_port,declared_size_bytes,"
              "effective_payload_bytes,pacing_mode,derived_packet_count,"
              "final_packet_payload_bytes,arrival_time_ns,last_send_time_ns,"
              "sent_application_bytes,sent_packet_count,received_application_bytes,"
              "received_packet_count,completion_time_ns,completion_delay_ns,transfer_state\n";
    for (const TransferSummaryRecord& summary : summaries)
    {
        output << summary.transferId << ',' << summary.sourceSatelliteId << ','
               << summary.destinationSatelliteId << ',' << summary.sourceAddress << ','
               << summary.destinationAddress << ',' << summary.sourcePort << ','
               << summary.destinationPort << ',' << summary.declaredSizeBytes << ','
               << summary.payloadBytesPerPacket << ',' << summary.pacingMode << ','
               << summary.derivedPacketCount << ',' << summary.finalPacketPayloadBytes << ','
               << summary.arrivalTimeNs << ',' << summary.lastSendTimeNs << ','
               << summary.sentApplicationBytes << ',' << summary.sentPacketCount << ','
               << summary.receivedApplicationBytes << ',' << summary.receivedPacketCount << ','
               << summary.completionTimeNs << ',' << summary.completionDelayNs << ','
               << summary.transferState << '\n';
    }
    return WriteCsv(outputDirectory, "transfer-summary.csv", output.str());
}

std::filesystem::path
WriteUdpSocketDrops(const std::filesystem::path& outputDirectory,
                    const std::vector<UdpSocketDropEvent>& events)
{
    std::ostringstream output;
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
    return WriteCsv(outputDirectory, "udp-socket-drops.csv", output.str());
}

std::filesystem::path
WriteTaskEvents(const std::filesystem::path& outputDirectory,
                Ptr<TaskCoordinator> coordinator)
{
    std::ostringstream output;
    output << "simulation_time_ns,task_id,from_state,to_state,node_id,cause\n";
    for (const TaskEventRecord& event : coordinator->GetTaskEvents())
    {
        output << event.simulationTimeNs << ',' << event.taskId << ','
               << TaskStateToString(event.fromState) << ',' << TaskStateToString(event.toState)
               << ',' << event.nodeId << ',' << event.cause << '\n';
    }
    return WriteCsv(outputDirectory, "task-events.csv", output.str());
}

std::map<uint32_t, uint64_t>
GetComputeRates(Ptr<TaskCoordinator> coordinator)
{
    std::map<uint32_t, uint64_t> rates;
    for (const Ptr<ComputeService>& service : coordinator->GetComputeServices())
    {
        if (!rates.emplace(service->GetNodeId(),
                           service->GetComputeRateWorkUnitsPerSecond())
                 .second)
        {
            throw RunOutputError("duplicate compute service in run output");
        }
    }
    return rates;
}

std::filesystem::path
WriteTaskSummaries(const std::filesystem::path& outputDirectory,
                   Ptr<TaskCoordinator> coordinator)
{
    const std::map<uint32_t, uint64_t> rates = GetComputeRates(coordinator);
    std::ostringstream output;
    output << "task_id,source_node_id,compute_node_id,result_node_id,input_bytes,output_bytes,"
              "compute_work_units,compute_rate_work_units_per_second,input_transfer_id,"
              "result_transfer_id,arrival_time_ns,input_transfer_complete_time_ns,"
              "queue_enter_time_ns,compute_start_time_ns,compute_complete_time_ns,"
              "result_transfer_start_time_ns,result_transfer_complete_time_ns,"
              "input_transfer_delay_ns,queue_delay_ns,compute_service_time_ns,"
              "result_transfer_delay_ns,end_to_end_completion_delay_ns,final_state\n";
    for (const TaskRuntime& task : coordinator->GetTaskRuntimes())
    {
        const auto rate = rates.find(task.definition.computeNodeId);
        if (rate == rates.end())
        {
            throw RunOutputError("task output has no matching compute service");
        }
        output << task.definition.taskId << ',' << task.definition.sourceNodeId << ','
               << task.definition.computeNodeId << ',' << task.definition.resultNodeId << ','
               << task.definition.inputBytes << ',' << task.definition.outputBytes << ','
               << task.definition.computeWorkUnits << ',' << rate->second << ','
               << task.definition.inputTransferId << ',' << task.definition.resultTransferId
               << ',' << task.definition.arrivalTimeNs << ','
               << task.inputTransferCompleteTimeNs << ',' << task.queueEnterTimeNs << ','
               << task.computeStartTimeNs << ',' << task.computeCompleteTimeNs << ','
               << task.resultTransferStartTimeNs << ',' << task.resultTransferCompleteTimeNs
               << ','
               << OptionalDifference(task.inputTransferCompleteTimeNs,
                                     task.definition.arrivalTimeNs,
                                     "input transfer delay")
               << ','
               << OptionalDifference(task.computeStartTimeNs,
                                     task.queueEnterTimeNs,
                                     "queue delay")
               << ','
               << OptionalDifference(task.computeCompleteTimeNs,
                                     task.computeStartTimeNs,
                                     "compute service time")
               << ','
               << OptionalDifference(task.resultTransferCompleteTimeNs,
                                     task.resultTransferStartTimeNs,
                                     "result transfer delay")
               << ','
               << OptionalDifference(task.resultTransferCompleteTimeNs,
                                     task.definition.arrivalTimeNs,
                                     "task completion delay")
               << ',' << TaskStateToString(task.state) << '\n';
    }
    return WriteCsv(outputDirectory, "task-summary.csv", output.str());
}

std::filesystem::path
WriteComputeNodeSummaries(const ResolvedSatComputeConfig& config,
                          const std::filesystem::path& outputDirectory,
                          Ptr<TaskCoordinator> coordinator)
{
    std::ostringstream output;
    output << "node_id,compute_rate_work_units_per_second,enqueued_tasks,completed_tasks,"
              "busy_time_ns,max_queue_length,utilization_percent\n";
    for (const Ptr<ComputeService>& service : coordinator->GetComputeServices())
    {
        if (service->GetBusyTimeNs() > static_cast<uint64_t>(config.simulation.durationNs))
        {
            throw RunOutputError("compute busy time exceeds simulation duration");
        }
        const long double utilization =
            static_cast<long double>(service->GetBusyTimeNs()) * 100.0L /
            static_cast<long double>(config.simulation.durationNs);
        output << std::setprecision(15) << service->GetNodeId() << ','
               << service->GetComputeRateWorkUnitsPerSecond() << ','
               << service->GetEnqueuedTaskCount() << ',' << service->GetCompletedTaskCount()
               << ',' << service->GetBusyTimeNs() << ',' << service->GetMaxQueueLength() << ','
               << static_cast<double>(utilization) << '\n';
    }
    return WriteCsv(outputDirectory, "compute-node-summary.csv", output.str());
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

std::filesystem::path
WriteIncompleteTransfers(const std::filesystem::path& diagnosticsDirectory,
                         const std::vector<TransferSummaryRecord>& summaries)
{
    std::ostringstream output;
    output << "transfer_id,transfer_state,source_node_id,destination_node_id,declared_size_bytes,"
              "sent_application_bytes,sent_packet_count,received_application_bytes,"
              "received_packet_count,missing_application_bytes,missing_packet_count,"
              "arrival_time_ns,last_send_time_ns,completion_time_ns\n";
    for (const TransferSummaryRecord& summary : summaries)
    {
        if (summary.transferState == "COMPLETED")
        {
            continue;
        }
        if (summary.receivedApplicationBytes > summary.declaredSizeBytes ||
            summary.receivedPacketCount > summary.derivedPacketCount)
        {
            throw RunOutputError("partial transfer counters exceed their declared totals");
        }
        output << summary.transferId << ',' << summary.transferState << ','
               << summary.sourceSatelliteId << ',' << summary.destinationSatelliteId << ','
               << summary.declaredSizeBytes << ',' << summary.sentApplicationBytes << ','
               << summary.sentPacketCount << ',' << summary.receivedApplicationBytes << ','
               << summary.receivedPacketCount << ','
               << summary.declaredSizeBytes - summary.receivedApplicationBytes << ','
               << summary.derivedPacketCount - summary.receivedPacketCount << ','
               << summary.arrivalTimeNs << ',' << summary.lastSendTimeNs << ','
               << summary.completionTimeNs << '\n';
    }
    return WriteCsv(diagnosticsDirectory, "incomplete-transfers.csv", output.str());
}

std::filesystem::path
WriteIncompleteTasks(const std::filesystem::path& diagnosticsDirectory,
                     Ptr<TaskCoordinator> coordinator)
{
    std::ostringstream output;
    output << "task_id,final_state,source_node_id,compute_node_id,result_node_id,arrival_time_ns,"
              "last_transition_time_ns,input_transfer_complete_time_ns,compute_start_time_ns,"
              "compute_complete_time_ns,result_transfer_complete_time_ns\n";
    if (coordinator != nullptr)
    {
        for (const TaskRuntime& task : coordinator->GetTaskRuntimes())
        {
            if (task.state == TASK_COMPLETED)
            {
                continue;
            }
            output << task.definition.taskId << ',' << TaskStateToString(task.state) << ','
                   << task.definition.sourceNodeId << ',' << task.definition.computeNodeId << ','
                   << task.definition.resultNodeId << ',' << task.definition.arrivalTimeNs << ','
                   << task.lastTransitionTimeNs << ',' << task.inputTransferCompleteTimeNs << ','
                   << task.computeStartTimeNs << ',' << task.computeCompleteTimeNs << ','
                   << task.resultTransferCompleteTimeNs << '\n';
        }
    }
    return WriteCsv(diagnosticsDirectory, "incomplete-tasks.csv", output.str());
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
    std::vector<UdpSocketDropEvent> udpDrops;
    ApplicationMetrics applicationMetrics;
    if (transferEngine != nullptr)
    {
        transfers = transferEngine->CollectSummaries();
        udpDrops = transferEngine->CollectUdpSocketDropEvents();
        applicationMetrics = transferEngine->CollectApplicationMetrics();
    }
    const TransferAggregate transferAggregate = CollectTransferAggregate(transfers);
    const TaskAggregate taskAggregate = CollectTaskAggregate(taskCoordinator);
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

    RunOutputResult result;
    result.complete = complete;
    if (transferEngine != nullptr)
    {
        result.files.push_back(WriteTransferSummaries(outputDirectory, transfers));
        result.files.push_back(WriteUdpSocketDrops(outputDirectory, udpDrops));
    }
    if (taskCoordinator != nullptr)
    {
        result.files.push_back(WriteTaskEvents(outputDirectory, taskCoordinator));
        result.files.push_back(WriteTaskSummaries(outputDirectory, taskCoordinator));
        result.files.push_back(WriteComputeNodeSummaries(config,
                                                         outputDirectory,
                                                         taskCoordinator));
    }
    if (context.flowRouteRegistry != nullptr)
    {
        result.files.push_back(WriteReservationEvents(outputDirectory,
                                                      context.flowRouteRegistry));
    }
    result.files.push_back(WriteRoutingSummary(config, context, outputDirectory));

    uint64_t udpDropBytes = 0;
    std::set<std::pair<uint32_t, uint16_t>> droppedReceivers;
    for (const UdpSocketDropEvent& event : udpDrops)
    {
        if (event.receiverRcvBufBytes != config.network.receiverRcvBufBytes)
        {
            throw RunOutputError("UDP drop receiver buffer differs from resolved config");
        }
        udpDropBytes = CheckedAdd(udpDropBytes, event.packetSizeBytes, "UDP drop bytes");
        droppedReceivers.emplace(event.destinationSatelliteId, event.destinationPort);
    }

    const std::string workloadMode = taskCoordinator != nullptr
                                         ? "task"
                                         : (transferEngine != nullptr ? "transfer" : "none");
    Json summary = {
        {"schema_version", "0.1"},
        {"run_name", config.runName},
        {"config_schema_version", config.schemaVersion},
        {"effective_config",
         {{"path", effectiveConfig.string()}, {"sha256", Sha256File(effectiveConfig)}}},
        {"simulation_duration_ns", config.simulation.durationNs},
        {"wall_clock_ns", context.wallClockNs},
        {"run_status", complete ? "COMPLETE" : "PARTIAL"},
        {"task_completion_policy", config.workloads.taskCompletionPolicy},
        {"workload_mode", workloadMode},
        {"topology_source", config.network.topologySource},
        {"applied_topology_slice_count", context.appliedTopologySliceCount},
        {"routing_mode", config.routing.mode},
        {"hash_seed", config.routing.hashSeed},
        {"route_computation_count", context.routeComputationCount},
        {"transfer",
         {{"sink_application_count", applicationMetrics.sinkApplications},
          {"transfer_count", transferAggregate.transferCount},
          {"completed_transfer_count", transferAggregate.completedTransferCount},
          {"declared_application_bytes", transferAggregate.declaredBytes},
          {"sent_application_bytes", transferAggregate.sentBytes},
          {"received_application_bytes", transferAggregate.receivedBytes},
          {"derived_udp_packets", transferAggregate.derivedPackets},
          {"sent_udp_packets", transferAggregate.sentPackets},
          {"received_udp_packets", transferAggregate.receivedPackets},
          {"udp_socket_drop_packets", udpDrops.size()},
          {"udp_socket_drop_bytes", udpDropBytes},
          {"udp_socket_dropped_receiver_count", droppedReceivers.size()}}},
        {"task",
         {{"compute_node_count", taskAggregate.computeNodeCount},
          {"task_count", taskAggregate.taskCount},
          {"completed_task_count", taskAggregate.completedTaskCount},
          {"total_input_bytes", taskAggregate.totalInputBytes},
          {"total_output_bytes", taskAggregate.totalOutputBytes},
          {"total_compute_work_units", taskAggregate.totalComputeWorkUnits},
          {"mean_completion_delay_ns", taskAggregate.meanCompletionDelayNs},
          {"maximum_completion_delay_ns", taskAggregate.maximumCompletionDelayNs}}}};

    if (!complete)
    {
        const std::filesystem::path diagnostics = outputDirectory / "diagnostics";
        result.files.push_back(WriteIncompleteTransfers(diagnostics, transfers));
        result.files.push_back(WriteIncompleteTasks(diagnostics, taskCoordinator));
        Json diagnosticSummary = {
            {"schema_version", "0.1"},
            {"run_status", "PARTIAL"},
            {"incomplete_transfer_count",
             transferAggregate.transferCount - transferAggregate.completedTransferCount},
            {"incomplete_task_count", taskAggregate.taskCount - taskAggregate.completedTaskCount},
            {"udp_socket_drop_packets", udpDrops.size()},
            {"udp_socket_drop_bytes", udpDropBytes}};
        result.files.push_back(WriteJson(diagnostics,
                                         "diagnostic-summary.json",
                                         diagnosticSummary));
        result.diagnosticsGenerated = true;
    }
    summary["diagnostics_generated"] = result.diagnosticsGenerated;
    result.runSummaryPath = WriteJson(outputDirectory, "run-summary.json", summary);
    result.files.push_back(result.runSummaryPath);
    return result;
}

} // namespace ns3
