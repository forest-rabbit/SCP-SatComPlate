/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Join the two real UDP transfers and one compute service into a task.

#include "task-coordinator.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <utility>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(TaskCoordinator);

TypeId
TaskCoordinator::GetTypeId()
{
    static TypeId typeId = TypeId("ns3::TaskCoordinator")
                               .SetParent<Object>()
                               .SetGroupName("SatCompute")
                               .AddConstructor<TaskCoordinator>();
    return typeId;
}

TaskCoordinator::TaskCoordinator() = default;

TaskCoordinator::~TaskCoordinator() = default;

void
TaskCoordinator::Initialize(const ComputeProfile& computeProfile,
                            const TaskTrace& taskTrace,
                            SatelliteRuntimeView& topology,
                            const std::string& transferChunkMode,
                            uint32_t transferPayloadBytes,
                            uint16_t islMtuBytes,
                            uint32_t receiverRcvBufBytes,
                            bool collectUdpSocketDrops,
                            int64_t simulationDurationNs)
{
    NS_ABORT_MSG_IF(m_initialized, "TaskCoordinator can only be initialized once");
    NS_ABORT_MSG_IF(computeProfile.nodes.empty(),
                    "TaskCoordinator requires a non-empty compute profile");
    NS_ABORT_MSG_IF(taskTrace.tasks.empty(),
                    "TaskCoordinator requires a non-empty task trace");
    NS_ABORT_MSG_IF(simulationDurationNs <= 0,
                    "TaskCoordinator simulation duration must be positive");

    m_tasks.reserve(taskTrace.tasks.size());
    for (uint32_t index = 0; index < taskTrace.tasks.size(); ++index)
    {
        const TaskDefinition& definition = taskTrace.tasks[index];
        NS_ABORT_MSG_IF(!m_taskIndexes.emplace(definition.taskId, index).second,
                        "TaskCoordinator has a duplicate task ID");
        m_tasks.emplace_back(definition);
    }

    m_computeServices.reserve(computeProfile.nodes.size());
    for (const ComputeNodeProfile& profile : computeProfile.nodes)
    {
        Ptr<ComputeService> service = CreateObject<ComputeService>();
        service->Configure(profile.nodeId,
                           profile.computeRateWorkUnitsPerSecond,
                           MakeCallback(&TaskCoordinator::HandleComputeStart, this),
                           MakeCallback(&TaskCoordinator::HandleComputeComplete, this));
        topology.GetNodeBySatelliteId(profile.nodeId)->AddApplication(service);
        service->SetStartTime(NanoSeconds(0));
        service->SetStopTime(NanoSeconds(simulationDurationNs));
        NS_ABORT_MSG_IF(!m_servicesByNodeId.emplace(profile.nodeId, service).second,
                        "TaskCoordinator has a duplicate compute-service node ID");
        m_computeServices.push_back(service);
    }

    std::vector<NetworkTransfer> plans;
    plans.reserve(taskTrace.tasks.size() * 2);
    for (const TaskDefinition& task : taskTrace.tasks)
    {
        NetworkTransfer input;
        input.transferId = task.inputTransferId;
        input.sourceSatelliteId = task.sourceNodeId;
        input.destinationSatelliteId = task.computeNodeId;
        input.sizeBytes = task.inputBytes;
        input.arrivalTimeNs = task.arrivalTimeNs;
        plans.push_back(input);
        NS_ABORT_MSG_IF(!m_inputTransferTasks.emplace(task.inputTransferId, task.taskId).second,
                        "TaskCoordinator has a duplicate input transfer ID");

        NetworkTransfer result;
        result.transferId = task.resultTransferId;
        result.sourceSatelliteId = task.computeNodeId;
        result.destinationSatelliteId = task.resultNodeId;
        result.sizeBytes = task.outputBytes;
        result.arrivalTimeNs = -1;
        plans.push_back(result);
        NS_ABORT_MSG_IF(!m_resultTransferTasks.emplace(task.resultTransferId, task.taskId).second,
                        "TaskCoordinator has a duplicate result transfer ID");
    }

    m_transferEngine = CreateObject<NetworkTransferEngine>();
    m_transferEngine->Configure(topology,
                                transferChunkMode,
                                transferPayloadBytes,
                                islMtuBytes,
                                receiverRcvBufBytes,
                                collectUdpSocketDrops,
                                simulationDurationNs);
    m_transferEngine->RegisterPlans(std::move(plans));

    for (const TaskDefinition& task : taskTrace.tasks)
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
    const auto task = m_taskIndexes.find(taskId);
    NS_ABORT_MSG_IF(task == m_taskIndexes.end(), "TaskCoordinator has no requested task ID");
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
    const auto service = m_servicesByNodeId.find(nodeId);
    NS_ABORT_MSG_IF(service == m_servicesByNodeId.end(),
                    "TaskCoordinator has no compute service for the requested node");
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
                    "task event time must equal the current simulation time");
    TaskRuntime& task = GetTask(taskId);
    const TaskState fromState = task.state;
    task.TransitionTo(requestedState, eventTimeNs, cause);
    m_taskEvents.push_back({eventTimeNs, taskId, fromState, requestedState, nodeId, cause});
}

void
TaskCoordinator::HandleTaskArrival(uint64_t taskId)
{
    TaskRuntime& task = GetTask(taskId);
    const int64_t timeNs = Simulator::Now().GetNanoSeconds();
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
TaskCoordinator::HandleInputTransferComplete(uint64_t transferId, int64_t completionTimeNs)
{
    const auto mapping = m_inputTransferTasks.find(transferId);
    NS_ABORT_MSG_IF(mapping == m_inputTransferTasks.end(),
                    "TaskCoordinator received an unknown input-transfer completion");
    TaskRuntime& task = GetTask(mapping->second);
    NS_ABORT_MSG_IF(task.definition.inputTransferId != transferId,
                    "TaskCoordinator input-transfer mapping is inconsistent");
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
TaskCoordinator::HandleComputeStart(uint64_t taskId, uint32_t nodeId, int64_t startTimeNs)
{
    const TaskRuntime& task = GetTask(taskId);
    NS_ABORT_MSG_IF(task.definition.computeNodeId != nodeId,
                    "compute-start node does not match the task definition");
    TransitionTask(taskId, TASK_RUNNING, nodeId, startTimeNs, "COMPUTE_DISPATCH");
}

void
TaskCoordinator::HandleComputeComplete(uint64_t taskId,
                                       uint32_t nodeId,
                                       int64_t completionTimeNs)
{
    TaskRuntime& task = GetTask(taskId);
    NS_ABORT_MSG_IF(task.definition.computeNodeId != nodeId,
                    "compute-completion node does not match the task definition");
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
    const auto mapping = m_resultTransferTasks.find(transferId);
    NS_ABORT_MSG_IF(mapping == m_resultTransferTasks.end(),
                    "TaskCoordinator received an unknown result-transfer completion");
    TaskRuntime& task = GetTask(mapping->second);
    NS_ABORT_MSG_IF(task.definition.resultTransferId != transferId,
                    "TaskCoordinator result-transfer mapping is inconsistent");
    TransitionTask(task.definition.taskId,
                   TASK_COMPLETED,
                   task.definition.resultNodeId,
                   completionTimeNs,
                   "RESULT_TRANSFER_COMPLETE");
}

bool
TaskCoordinator::IsComplete() const
{
    NS_ABORT_MSG_IF(!m_initialized, "TaskCoordinator is not initialized");
    return m_transferEngine->AreAllTransfersCompleted() &&
           std::all_of(m_tasks.begin(), m_tasks.end(), [](const TaskRuntime& task) {
               return task.state == TASK_COMPLETED;
           });
}

void
TaskCoordinator::ValidateCompleted() const
{
    NS_ABORT_MSG_IF(!m_initialized, "TaskCoordinator is not initialized");
    for (const TaskRuntime& task : m_tasks)
    {
        NS_ABORT_MSG_IF(task.state != TASK_COMPLETED,
                        "TaskCoordinator has an incomplete task_id="
                            << task.definition.taskId);
        NS_ABORT_MSG_IF(!m_transferEngine->IsCompleted(task.definition.inputTransferId) ||
                            !m_transferEngine->IsCompleted(task.definition.resultTransferId),
                        "TaskCoordinator has an incomplete task transfer");
    }
    NS_ABORT_MSG_IF(!m_transferEngine->AreAllTransfersCompleted(),
                    "TaskCoordinator has incomplete network transfers");
    NS_ABORT_MSG_IF(m_taskEvents.size() != m_tasks.size() * 5,
                    "every completed task must have exactly five state transitions");
    for (const Ptr<ComputeService>& service : m_computeServices)
    {
        NS_ABORT_MSG_IF(!service->IsIdle(),
                        "TaskCoordinator has a non-idle compute service at simulation end");
        NS_ABORT_MSG_IF(service->GetEnqueuedTaskCount() !=
                            service->GetCompletedTaskCount(),
                        "compute-service enqueue and completion counts differ");
    }
}

Ptr<NetworkTransferEngine>
TaskCoordinator::GetTransferEngine() const
{
    NS_ABORT_MSG_IF(m_transferEngine == nullptr,
                    "TaskCoordinator has no network transfer engine");
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
