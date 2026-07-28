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

// 汇总任务完成情况，并写出任务时间线和计算节点利用率。

#include "task-metrics.h"

#include "../../task/task-coordinator.h"

#include "ns3/abort.h"
#include "ns3/nstime.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sys/stat.h>

namespace ns3 {

namespace {

uint64_t
CheckedAdd(uint64_t left, uint64_t right, const std::string& field)
{
  NS_ABORT_MSG_IF(left > std::numeric_limits<uint64_t>::max() - right,
                  "run summary " << field << " 溢出");
  return left + right;
}

std::string
OutputPath(const std::string& directory, const std::string& filename)
{
  if (directory.empty() || directory == ".")
    {
      return filename;
    }
  mkdir(directory.c_str(), 0755);
  return directory.back() == '/' ? directory + filename : directory + "/" + filename;
}

uint64_t
NonNegativeDifference(int64_t endTimeNs,
                      int64_t startTimeNs,
                      const std::string& field,
                      uint64_t taskId)
{
  NS_ABORT_MSG_IF(startTimeNs < 0 || endTimeNs < startTimeNs,
                  "任务时间戳无效: task_id=" << taskId
                    << " field=" << field
                    << " start=" << startTimeNs
                    << " end=" << endTimeNs);
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
  NS_ABORT_MSG_IF(endTimeNs < startTimeNs,
                  "任务时间戳无效: task_id=" << taskId
                    << " field=" << field
                    << " start=" << startTimeNs
                    << " end=" << endTimeNs);
  return endTimeNs - startTimeNs;
}

void
WriteTaskEvents(const TaskCoordinator& coordinator,
                const std::string& outputDirectory)
{
  std::ofstream output(OutputPath(outputDirectory, "task-events.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 task events CSV");
  output << "simulation_time_ns,task_id,from_state,to_state,node_id,cause\n";
  for (const auto& event : coordinator.GetTaskEvents())
    {
      output << event.simulationTimeNs << ","
             << event.taskId << ","
             << TaskStateToString(event.fromState) << ","
             << TaskStateToString(event.toState) << ","
             << event.nodeId << ","
             << event.cause << "\n";
    }
}

void
WriteTaskSummaries(const TaskCoordinator& coordinator,
                   const std::string& outputDirectory)
{
  std::map<uint32_t, uint64_t> ratesByNodeId;
  for (const auto& service : coordinator.GetComputeServices())
    {
      NS_ABORT_MSG_IF(
        !ratesByNodeId.insert(
          std::make_pair(
            service->GetNodeId(),
            service->GetComputeRateWorkUnitsPerSecond())).second,
        "重复 ComputeService node_id=" << service->GetNodeId());
    }

  std::ofstream output(OutputPath(outputDirectory, "task-summary.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 task summary CSV");
  output
    << "task_id,source_node_id,compute_node_id,result_node_id,input_bytes,"
       "output_bytes,compute_work_units,compute_rate_work_units_per_second,"
       "input_transfer_id,result_transfer_id,arrival_time_ns,"
       "input_transfer_complete_time_ns,queue_enter_time_ns,"
       "compute_start_time_ns,compute_complete_time_ns,"
       "result_transfer_start_time_ns,result_transfer_complete_time_ns,"
       "input_transfer_delay_ns,queue_delay_ns,compute_service_time_ns,"
       "result_transfer_delay_ns,end_to_end_completion_delay_ns,final_state\n";
  for (const auto& task : coordinator.GetTaskRuntimes())
    {
      auto rate = ratesByNodeId.find(task.definition.computeNodeId);
      NS_ABORT_MSG_IF(rate == ratesByNodeId.end(),
                      "task summary 缺少 ComputeService，task_id="
                        << task.definition.taskId);
      int64_t inputDelayNs =
        OptionalDifference(task.inputTransferCompleteTimeNs,
                           task.definition.arrivalTimeNs,
                           "input_transfer_delay_ns",
                           task.definition.taskId);
      int64_t queueDelayNs =
        OptionalDifference(task.computeStartTimeNs,
                           task.queueEnterTimeNs,
                           "queue_delay_ns",
                           task.definition.taskId);
      int64_t serviceTimeNs =
        OptionalDifference(task.computeCompleteTimeNs,
                           task.computeStartTimeNs,
                           "compute_service_time_ns",
                           task.definition.taskId);
      int64_t resultDelayNs =
        OptionalDifference(task.resultTransferCompleteTimeNs,
                           task.resultTransferStartTimeNs,
                           "result_transfer_delay_ns",
                           task.definition.taskId);
      int64_t completionDelayNs =
        OptionalDifference(task.resultTransferCompleteTimeNs,
                           task.definition.arrivalTimeNs,
                           "end_to_end_completion_delay_ns",
                           task.definition.taskId);

      output << task.definition.taskId << ","
             << task.definition.sourceNodeId << ","
             << task.definition.computeNodeId << ","
             << task.definition.resultNodeId << ","
             << task.definition.inputBytes << ","
             << task.definition.outputBytes << ","
             << task.definition.computeWorkUnits << ","
             << rate->second << ","
             << task.definition.inputTransferId << ","
             << task.definition.resultTransferId << ","
             << task.definition.arrivalTimeNs << ","
             << task.inputTransferCompleteTimeNs << ","
             << task.queueEnterTimeNs << ","
             << task.computeStartTimeNs << ","
             << task.computeCompleteTimeNs << ","
             << task.resultTransferStartTimeNs << ","
             << task.resultTransferCompleteTimeNs << ","
             << inputDelayNs << ","
             << queueDelayNs << ","
             << serviceTimeNs << ","
             << resultDelayNs << ","
             << completionDelayNs << ","
             << TaskStateToString(task.state) << "\n";
    }
}

void
WriteComputeNodeSummaries(const TaskCoordinator& coordinator,
                          double simulationDurationSeconds,
                          const std::string& outputDirectory)
{
  int64_t simulationDurationNs =
    Seconds(simulationDurationSeconds).GetNanoSeconds();
  NS_ABORT_MSG_IF(simulationDurationNs <= 0,
                  "simulationDuration 无法表示为正的纳秒时长");

  std::ofstream output(OutputPath(outputDirectory, "compute-node-summary.csv"),
                       std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF(!output.is_open(), "无法写入 compute node summary CSV");
  output
    << "node_id,compute_rate_work_units_per_second,enqueued_tasks,"
       "completed_tasks,busy_time_ns,max_queue_length,utilization_percent\n";
  for (const auto& service : coordinator.GetComputeServices())
    {
      NS_ABORT_MSG_IF(
        service->GetBusyTimeNs()
          > static_cast<uint64_t>(simulationDurationNs),
        "ComputeService busy_time_ns 超过 simulationDuration，node_id="
          << service->GetNodeId());
      long double utilizationPercent =
        static_cast<long double>(service->GetBusyTimeNs()) * 100.0L
        / static_cast<long double>(simulationDurationNs);
      output << std::setprecision(15)
             << service->GetNodeId() << ","
             << service->GetComputeRateWorkUnitsPerSecond() << ","
             << service->GetEnqueuedTaskCount() << ","
             << service->GetCompletedTaskCount() << ","
             << service->GetBusyTimeNs() << ","
             << service->GetMaxQueueLength() << ","
             << static_cast<double>(utilizationPercent) << "\n";
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
  for (const auto& task : coordinator->GetTaskRuntimes())
    {
      aggregate.totalInputBytes =
        CheckedAdd(aggregate.totalInputBytes,
                   task.definition.inputBytes,
                   "task input bytes");
      aggregate.totalOutputBytes =
        CheckedAdd(aggregate.totalOutputBytes,
                   task.definition.outputBytes,
                   "task output bytes");
      aggregate.totalComputeWorkUnits =
        CheckedAdd(aggregate.totalComputeWorkUnits,
                   task.definition.computeWorkUnits,
                   "task compute work units");
      if (task.state != TASK_COMPLETED)
        {
          continue;
        }
      ++aggregate.completedTaskCount;
      uint64_t completionDelayNs =
        NonNegativeDifference(task.resultTransferCompleteTimeNs,
                              task.definition.arrivalTimeNs,
                              "end_to_end_completion_delay_ns",
                              task.definition.taskId);
      aggregate.totalCompletionDelayNs =
        CheckedAdd(aggregate.totalCompletionDelayNs,
                   completionDelayNs,
                   "task completion delay");
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
  WriteTaskEvents(coordinator, outputDirectory);
  WriteTaskSummaries(coordinator, outputDirectory);
  WriteComputeNodeSummaries(coordinator,
                            simulationDurationSeconds,
                            outputDirectory);
}

} // namespace ns3
