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

// 协调 INPUT 传输、FCFS 计算和 RESULT 传输组成的任务闭环。

#include "task-coordinator.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <iostream>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(TaskCoordinator);

TypeId
TaskCoordinator::GetTypeId()
{
  static TypeId typeId =
    TypeId("ns3::TaskCoordinator")
      .SetParent<Object>()
      .SetGroupName("SatCompute")
      .AddConstructor<TaskCoordinator>();
  return typeId;
}

TaskCoordinator::TaskCoordinator()
  : m_initialized(false)
{
}

TaskCoordinator::~TaskCoordinator()
{
}

void
TaskCoordinator::Initialize(const ComputeProfile& computeProfile,
                            const TaskTrace& taskTrace,
                            SatelliteTopology& topology,
                            const std::string& transferChunkMode,
                            uint32_t transferPayloadBytes,
                            uint16_t islMtuBytes,
                            uint32_t receiverRcvBufBytes,
                            bool collectUdpSocketDrops,
                            double simulationDurationSeconds,
                            const std::string& taskLogMode)
{
  NS_ABORT_MSG_IF(m_initialized,
                  "TaskCoordinator 只能初始化一次");
  NS_ABORT_MSG_IF(taskLogMode != "summary"
                    && taskLogMode != "verbose"
                    && taskLogMode != "silent",
                  "taskLogMode 必须是 summary、verbose 或 silent");
  NS_ABORT_MSG_IF(computeProfile.nodes.empty(),
                  "TaskCoordinator 要求非空 ComputeProfile");
  NS_ABORT_MSG_IF(taskTrace.tasks.empty(),
                  "TaskCoordinator 要求非空 TaskTrace");
  m_taskLogMode = taskLogMode;

  int64_t simulationDurationNs =
    Seconds(simulationDurationSeconds).GetNanoSeconds();
  NS_ABORT_MSG_IF(simulationDurationNs <= 0,
                  "simulationDuration 无法表示为正的纳秒时长");

  m_tasks.reserve(taskTrace.tasks.size());
  for (uint32_t index = 0; index < taskTrace.tasks.size(); ++index)
    {
      const TaskDefinition& definition = taskTrace.tasks[index];
      NS_ABORT_MSG_IF(
        !m_taskIndexes.insert(std::make_pair(definition.taskId, index)).second,
        "TaskCoordinator 包含重复 task_id=" << definition.taskId);
      m_tasks.push_back(TaskRuntime(definition));
    }

  m_computeServices.reserve(computeProfile.nodes.size());
  for (const auto& profile : computeProfile.nodes)
    {
      Ptr<ComputeService> service = CreateObject<ComputeService>();
      service->Configure(
        profile.nodeId,
        profile.computeRateWorkUnitsPerSecond,
        MakeCallback(&TaskCoordinator::HandleComputeStart, this),
        MakeCallback(&TaskCoordinator::HandleComputeComplete, this));
      topology.GetNodeBySatelliteId(profile.nodeId)->AddApplication(service);
      service->SetStartTime(NanoSeconds(0));
      service->SetStopTime(NanoSeconds(simulationDurationNs));
      NS_ABORT_MSG_IF(
        !m_servicesByNodeId.insert(std::make_pair(profile.nodeId, service)).second,
        "TaskCoordinator 包含重复 ComputeService node_id="
          << profile.nodeId);
      m_computeServices.push_back(service);
    }

  std::vector<NetworkTransfer> plans;
  plans.reserve(taskTrace.tasks.size() * 2);
  for (const auto& task : taskTrace.tasks)
    {
      NetworkTransfer input;
      input.transferId = task.inputTransferId;
      input.sourceSatelliteId = task.sourceNodeId;
      input.destinationSatelliteId = task.computeNodeId;
      input.sizeBytes = task.inputBytes;
      input.arrivalTimeNs = task.arrivalTimeNs;
      plans.push_back(input);
      NS_ABORT_MSG_IF(
        !m_inputTransferTasks.insert(
          std::make_pair(task.inputTransferId, task.taskId)).second,
        "TaskCoordinator 重复 INPUT transfer_id=" << task.inputTransferId);

      NetworkTransfer result;
      result.transferId = task.resultTransferId;
      result.sourceSatelliteId = task.computeNodeId;
      result.destinationSatelliteId = task.resultNodeId;
      result.sizeBytes = task.outputBytes;
      result.arrivalTimeNs = -1;
      plans.push_back(result);
      NS_ABORT_MSG_IF(
        !m_resultTransferTasks.insert(
          std::make_pair(task.resultTransferId, task.taskId)).second,
        "TaskCoordinator 重复 RESULT transfer_id=" << task.resultTransferId);
    }

  m_transferEngine = CreateObject<NetworkTransferEngine>();
  m_transferEngine->Configure(topology,
                              transferChunkMode,
                              transferPayloadBytes,
                              islMtuBytes,
                              receiverRcvBufBytes,
                              collectUdpSocketDrops,
                              simulationDurationSeconds);
  m_transferEngine->RegisterPlans(plans);

  for (const auto& task : taskTrace.tasks)
    {
      Simulator::Schedule(NanoSeconds(task.arrivalTimeNs),
                          &TaskCoordinator::HandleTaskArrival,
                          this,
                          task.taskId);
    }
  m_initialized = true;
}

uint32_t
TaskCoordinator::GetTaskIndex(uint64_t taskId) const
{
  auto task = m_taskIndexes.find(taskId);
  NS_ABORT_MSG_IF(task == m_taskIndexes.end(),
                  "TaskCoordinator 不包含 task_id=" << taskId);
  return task->second;
}

TaskRuntime&
TaskCoordinator::GetTask(uint64_t taskId)
{
  return m_tasks[GetTaskIndex(taskId)];
}

const TaskRuntime&
TaskCoordinator::GetTask(uint64_t taskId) const
{
  return m_tasks[GetTaskIndex(taskId)];
}

Ptr<ComputeService>
TaskCoordinator::GetComputeService(uint32_t nodeId) const
{
  auto service = m_servicesByNodeId.find(nodeId);
  NS_ABORT_MSG_IF(service == m_servicesByNodeId.end(),
                  "TaskCoordinator 不包含 ComputeService node_id=" << nodeId);
  return service->second;
}

void
TaskCoordinator::TransitionTask(uint64_t taskId,
                                TaskState requestedState,
                                uint32_t nodeId,
                                int64_t eventTimeNs,
                                const std::string& cause)
{
  NS_ABORT_MSG_IF(eventTimeNs != Simulator::Now().GetNanoSeconds(),
                  "task event time 必须等于当前仿真时间，task_id=" << taskId);
  TaskRuntime& task = GetTask(taskId);
  TaskState fromState = task.state;
  task.TransitionTo(requestedState, eventTimeNs, cause);
  TaskEventRecord event = {
    eventTimeNs,
    taskId,
    fromState,
    requestedState,
    nodeId,
    cause
  };
  m_taskEvents.push_back(event);
  if (m_taskLogMode == "verbose")
    {
      std::cout << "[TASK:" << taskId << "] "
                << TaskStateToString(fromState) << " -> "
                << TaskStateToString(requestedState)
                << " @" << eventTimeNs << "ns"
                << " node=" << nodeId
                << " cause=" << cause << std::endl;
    }
}

void
TaskCoordinator::HandleTaskArrival(uint64_t taskId)
{
  TaskRuntime& task = GetTask(taskId);
  int64_t timeNs = Simulator::Now().GetNanoSeconds();
  TransitionTask(taskId,
                 TASK_INPUT_TRANSFERRING,
                 task.definition.sourceNodeId,
                 timeNs,
                 "TASK_ARRIVAL");
  m_transferEngine->StartTransferNow(
    task.definition.inputTransferId,
    MakeCallback(&TaskCoordinator::HandleInputTransferComplete, this));
}

void
TaskCoordinator::HandleInputTransferComplete(uint64_t transferId,
                                             int64_t completionTimeNs)
{
  auto mapping = m_inputTransferTasks.find(transferId);
  NS_ABORT_MSG_IF(mapping == m_inputTransferTasks.end(),
                  "未知 INPUT transfer completion，transfer_id=" << transferId);
  TaskRuntime& task = GetTask(mapping->second);
  NS_ABORT_MSG_IF(task.definition.inputTransferId != transferId,
                  "INPUT transfer/task 映射不一致，transfer_id=" << transferId);
  TransitionTask(task.definition.taskId,
                 TASK_QUEUED,
                 task.definition.computeNodeId,
                 completionTimeNs,
                 "INPUT_TRANSFER_COMPLETE");
  GetComputeService(task.definition.computeNodeId)
    ->SubmitTask(task.definition.taskId,
                 task.definition.computeWorkUnits,
                 completionTimeNs);
}

void
TaskCoordinator::HandleComputeStart(uint64_t taskId,
                                    uint32_t nodeId,
                                    int64_t startTimeNs)
{
  const TaskRuntime& task = GetTask(taskId);
  NS_ABORT_MSG_IF(task.definition.computeNodeId != nodeId,
                  "compute start node 与任务定义不一致，task_id=" << taskId);
  TransitionTask(taskId,
                 TASK_RUNNING,
                 nodeId,
                 startTimeNs,
                 "COMPUTE_DISPATCH");
}

void
TaskCoordinator::HandleComputeComplete(uint64_t taskId,
                                       uint32_t nodeId,
                                       int64_t completionTimeNs)
{
  TaskRuntime& task = GetTask(taskId);
  NS_ABORT_MSG_IF(task.definition.computeNodeId != nodeId,
                  "compute completion node 与任务定义不一致，task_id="
                    << taskId);
  TransitionTask(taskId,
                 TASK_RESULT_TRANSFERRING,
                 nodeId,
                 completionTimeNs,
                 "COMPUTE_COMPLETE");
  m_transferEngine->StartTransferNow(
    task.definition.resultTransferId,
    MakeCallback(&TaskCoordinator::HandleResultTransferComplete, this));
}

void
TaskCoordinator::HandleResultTransferComplete(uint64_t transferId,
                                              int64_t completionTimeNs)
{
  auto mapping = m_resultTransferTasks.find(transferId);
  NS_ABORT_MSG_IF(mapping == m_resultTransferTasks.end(),
                  "未知 RESULT transfer completion，transfer_id=" << transferId);
  TaskRuntime& task = GetTask(mapping->second);
  NS_ABORT_MSG_IF(task.definition.resultTransferId != transferId,
                  "RESULT transfer/task 映射不一致，transfer_id=" << transferId);
  TransitionTask(task.definition.taskId,
                 TASK_COMPLETED,
                 task.definition.resultNodeId,
                 completionTimeNs,
                 "RESULT_TRANSFER_COMPLETE");
}

bool
TaskCoordinator::IsComplete() const
{
  NS_ABORT_MSG_IF(!m_initialized,
                  "TaskCoordinator 尚未初始化");
  return m_transferEngine->AreAllTransfersCompleted()
         && std::all_of(m_tasks.begin(),
                        m_tasks.end(),
                        [](const TaskRuntime& task) {
                          return task.state == TASK_COMPLETED;
                        });
}

void
TaskCoordinator::ValidateCompleted() const
{
  NS_ABORT_MSG_IF(!m_initialized,
                  "TaskCoordinator 尚未初始化");
  for (const auto& task : m_tasks)
    {
      NS_ABORT_MSG_IF(
        task.state != TASK_COMPLETED,
        "任务未完成: task_id=" << task.definition.taskId
          << " state=" << TaskStateToString(task.state)
          << " last_transition_time_ns=" << task.lastTransitionTimeNs);
      NS_ABORT_MSG_IF(
        !m_transferEngine->IsCompleted(task.definition.inputTransferId)
          || !m_transferEngine->IsCompleted(task.definition.resultTransferId),
        "任务传输未全部完成: task_id=" << task.definition.taskId);
    }
  NS_ABORT_MSG_IF(!m_transferEngine->AreAllTransfersCompleted(),
                  "TaskCoordinator 存在未完成 NetworkTransfer");
  NS_ABORT_MSG_IF(m_taskEvents.size() != m_tasks.size() * 5,
                  "每个完成任务必须恰好包含 5 次状态转换");
  for (const auto& service : m_computeServices)
    {
      NS_ABORT_MSG_IF(
        !service->IsIdle(),
        "ComputeService 运行结束时非空: node_id=" << service->GetNodeId()
          << " running=" << service->HasRunningTask()
          << " queue_size=" << service->GetQueueSize());
      NS_ABORT_MSG_IF(
        service->GetEnqueuedTaskCount()
          != service->GetCompletedTaskCount(),
        "ComputeService 入队与完成计数不一致: node_id="
          << service->GetNodeId()
          << " enqueued=" << service->GetEnqueuedTaskCount()
          << " completed=" << service->GetCompletedTaskCount());
    }
}

Ptr<NetworkTransferEngine>
TaskCoordinator::GetTransferEngine() const
{
  NS_ABORT_MSG_IF(m_transferEngine == nullptr,
                  "TaskCoordinator 尚未创建 NetworkTransferEngine");
  return m_transferEngine;
}

const std::vector<TaskRuntime>&
TaskCoordinator::GetTaskRuntimes() const
{
  return m_tasks;
}

const std::vector<Ptr<ComputeService>>&
TaskCoordinator::GetComputeServices() const
{
  return m_computeServices;
}

const std::vector<TaskEventRecord>&
TaskCoordinator::GetTaskEvents() const
{
  return m_taskEvents;
}

} // namespace ns3
