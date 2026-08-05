/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Aggregate task completion and write task/compute-node timelines.

#include "task-metrics.h"

#include "../../para.h"
#include "../../task/task-coordinator.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <stdexcept>

namespace ns3
{

namespace
{

uint64_t
CheckedAdd(uint64_t left, uint64_t right, const std::string& field)
{
    if (left > std::numeric_limits<uint64_t>::max() - right)
    {
        throw std::runtime_error("task aggregate " + field + " overflow");
    }
    return left + right;
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

uint64_t
NonNegativeDifference(int64_t endTimeNs,
                      int64_t startTimeNs,
                      const std::string& field,
                      uint64_t taskId)
{
    if (startTimeNs < 0 || endTimeNs < startTimeNs)
    {
        throw std::runtime_error("invalid task timestamp for task " + std::to_string(taskId) +
                                 ": " + field);
    }
    return static_cast<uint64_t>(endTimeNs - startTimeNs);
}

int64_t
OptionalDifference(int64_t endTimeNs,
                   int64_t startTimeNs,
                   const std::string& field,
                   uint64_t taskId)
{
    if (startTimeNs < 0 || endTimeNs < 0)
    {
        return -1;
    }
    if (endTimeNs < startTimeNs)
    {
        throw std::runtime_error("invalid task timestamp for task " + std::to_string(taskId) +
                                 ": " + field);
    }
    return endTimeNs - startTimeNs;
}

void
WriteTaskEvents(const TaskCoordinator& coordinator, const std::string& outputDirectory)
{
    std::ofstream output(OutputPath(outputDirectory, "task-events.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write task-events.csv");
    }
    output << "simulation_time_ns,task_id,from_state,to_state,node_id,cause\n";
    for (const TaskEventRecord& event : coordinator.GetTaskEvents())
    {
        output << event.simulationTimeNs << ',' << event.taskId << ','
               << TaskStateToString(event.fromState) << ',' << TaskStateToString(event.toState)
               << ',' << event.nodeId << ',' << event.cause << '\n';
    }
}

void
WriteTaskSummaries(const TaskCoordinator& coordinator, const std::string& outputDirectory)
{
    std::map<uint32_t, uint64_t> ratesByNodeId;
    for (const Ptr<ComputeService>& service : coordinator.GetComputeServices())
    {
        if (!ratesByNodeId
                 .emplace(service->GetNodeId(), service->GetComputeRateWorkUnitsPerSecond())
                 .second)
        {
            throw std::runtime_error("duplicate ComputeService node id");
        }
    }

    std::ofstream output(OutputPath(outputDirectory, "task-summary.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write task-summary.csv");
    }
    output << "task_id,source_node_id,compute_node_id,result_node_id,input_bytes,output_bytes,"
              "compute_work_units,compute_rate_work_units_per_second,input_transfer_id,"
              "result_transfer_id,arrival_time_ns,input_transfer_complete_time_ns,"
              "queue_enter_time_ns,compute_start_time_ns,compute_complete_time_ns,"
              "result_transfer_start_time_ns,result_transfer_complete_time_ns,"
              "input_transfer_delay_ns,queue_delay_ns,compute_service_time_ns,"
              "result_transfer_delay_ns,end_to_end_completion_delay_ns,final_state\n";
    for (const TaskRuntime& task : coordinator.GetTaskRuntimes())
    {
        const auto rate = ratesByNodeId.find(task.definition.computeNodeId);
        if (rate == ratesByNodeId.end())
        {
            throw std::runtime_error("task summary has no matching ComputeService");
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
                                     "input_transfer_delay_ns",
                                     task.definition.taskId)
               << ','
               << OptionalDifference(task.computeStartTimeNs,
                                     task.queueEnterTimeNs,
                                     "queue_delay_ns",
                                     task.definition.taskId)
               << ','
               << OptionalDifference(task.computeCompleteTimeNs,
                                     task.computeStartTimeNs,
                                     "compute_service_time_ns",
                                     task.definition.taskId)
               << ','
               << OptionalDifference(task.resultTransferCompleteTimeNs,
                                     task.resultTransferStartTimeNs,
                                     "result_transfer_delay_ns",
                                     task.definition.taskId)
               << ','
               << OptionalDifference(task.resultTransferCompleteTimeNs,
                                     task.definition.arrivalTimeNs,
                                     "end_to_end_completion_delay_ns",
                                     task.definition.taskId)
               << ',' << TaskStateToString(task.state) << '\n';
    }
}

void
WriteComputeNodeSummaries(const TaskCoordinator& coordinator,
                          int64_t simulationDurationNs,
                          const std::string& outputDirectory)
{
    if (simulationDurationNs <= 0)
    {
        throw std::runtime_error("simulation duration must be positive");
    }
    std::ofstream output(OutputPath(outputDirectory, "compute-node-summary.csv"),
                         std::ios::out | std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("cannot write compute-node-summary.csv");
    }
    output << "node_id,compute_rate_work_units_per_second,enqueued_tasks,completed_tasks,"
              "busy_time_ns,max_queue_length,utilization_percent\n";
    for (const Ptr<ComputeService>& service : coordinator.GetComputeServices())
    {
        if (service->GetBusyTimeNs() > static_cast<uint64_t>(simulationDurationNs))
        {
            throw std::runtime_error("ComputeService busy time exceeds simulation duration");
        }
        const long double utilization =
            static_cast<long double>(service->GetBusyTimeNs()) * 100.0L /
            static_cast<long double>(simulationDurationNs);
        output << std::setprecision(15) << service->GetNodeId() << ','
               << service->GetComputeRateWorkUnitsPerSecond() << ','
               << service->GetEnqueuedTaskCount() << ',' << service->GetCompletedTaskCount()
               << ',' << service->GetBusyTimeNs() << ',' << service->GetMaxQueueLength() << ','
               << static_cast<double>(utilization) << '\n';
    }
}

} // namespace

TaskAggregate
CollectTaskAggregate(const TaskCoordinator* coordinator)
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
        aggregate.totalInputBytes =
            CheckedAdd(aggregate.totalInputBytes, task.definition.inputBytes, "input bytes");
        aggregate.totalOutputBytes =
            CheckedAdd(aggregate.totalOutputBytes, task.definition.outputBytes, "output bytes");
        aggregate.totalComputeWorkUnits = CheckedAdd(aggregate.totalComputeWorkUnits,
                                                     task.definition.computeWorkUnits,
                                                     "compute work units");
        if (task.state != TASK_COMPLETED)
        {
            continue;
        }
        ++aggregate.completedTaskCount;
        const uint64_t completionDelayNs =
            NonNegativeDifference(task.resultTransferCompleteTimeNs,
                                  task.definition.arrivalTimeNs,
                                  "end_to_end_completion_delay_ns",
                                  task.definition.taskId);
        aggregate.totalCompletionDelayNs = CheckedAdd(aggregate.totalCompletionDelayNs,
                                                       completionDelayNs,
                                                       "completion delay");
        aggregate.maxCompletionDelayNs =
            std::max(aggregate.maxCompletionDelayNs, completionDelayNs);
    }
    if (aggregate.completedTaskCount > 0)
    {
        aggregate.meanCompletionDelayNs =
            aggregate.totalCompletionDelayNs / aggregate.completedTaskCount;
    }
    return aggregate;
}

void
WriteTaskMetrics(const TaskCoordinator& coordinator,
                 double simulationDurationSeconds,
                 const std::string& outputDirectory)
{
    WriteTaskMetricsNs(coordinator,
                       SatComputeSecondsToNanoseconds(simulationDurationSeconds,
                                                     "simulationDurationSeconds"),
                       outputDirectory);
}

void
WriteTaskMetricsNs(const TaskCoordinator& coordinator,
                   int64_t simulationDurationNs,
                   const std::string& outputDirectory)
{
    WriteTaskEvents(coordinator, outputDirectory);
    WriteTaskSummaries(coordinator, outputDirectory);
    WriteComputeNodeSummaries(coordinator, simulationDurationNs, outputDirectory);
}

} // namespace ns3
