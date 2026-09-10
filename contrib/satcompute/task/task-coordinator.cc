/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Join the two real UDP transfers and one compute service into a task.

#include "task-coordinator.h"

#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <optional>
#include <set>
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
                            int64_t simulationDurationNs,
                            double computeDeadlineFactor)
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
        const uint64_t referenceRate =
            definition.taskProfile == TaskProfile::UNSPECIFIED
                ? GetComputeNodeProfile(computeProfile, definition.computeNodeId)
                      .computeRateWorkUnitsPerSecond
                : 100000;
        m_tasks.back().ConfigureComputeDeadline(referenceRate, computeDeadlineFactor);
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
TaskCoordinator::FindComputeService(uint32_t nodeId) const
{
    const auto service = m_servicesByNodeId.find(nodeId);
    return service == m_servicesByNodeId.end() ? nullptr : service->second;
}

Ptr<ComputeService>
TaskCoordinator::GetComputeService(uint32_t nodeId) const
{
    Ptr<ComputeService> service = FindComputeService(nodeId);
    NS_ABORT_MSG_IF(service == nullptr,
                    "TaskCoordinator has no compute service for the requested node");
    return service;
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
    m_taskTransition(m_taskEvents.back());
}

void
TaskCoordinator::ConnectTaskObserver(Callback<void, const TaskEventRecord&> callback)
{
    m_taskTransition.ConnectWithoutContext(callback);
}

void
TaskCoordinator::DisconnectTaskObserver(Callback<void, const TaskEventRecord&> callback)
{
    m_taskTransition.DisconnectWithoutContext(callback);
}

void
TaskCoordinator::HandleTaskArrival(uint64_t taskId)
{
    TaskRuntime& task = GetTask(taskId);
    if (IsTerminalTaskState(task.state))
    {
        return;
    }
    const int64_t timeNs = Simulator::Now().GetNanoSeconds();
    if (!IsSatelliteAvailable(task.definition.sourceNodeId))
    {
        FailTaskForSatelliteNode(task,
                                 task.definition.sourceNodeId,
                                 timeNs,
                                 "SOURCE_SATELLITE_UNAVAILABLE_AT_ARRIVAL");
        return;
    }
    if (!IsSatelliteAvailable(task.definition.computeNodeId))
    {
        FailTaskForSatelliteNode(task,
                                 task.definition.computeNodeId,
                                 timeNs,
                                 "COMPUTE_SATELLITE_UNAVAILABLE_AT_ARRIVAL");
        return;
    }
    if (!IsSatelliteAvailable(task.definition.resultNodeId))
    {
        FailTaskForSatelliteNode(task,
                                 task.definition.resultNodeId,
                                 timeNs,
                                 "RESULT_SATELLITE_UNAVAILABLE_AT_ARRIVAL");
        return;
    }
    if (!IsComputeAvailable(task.definition.computeNodeId))
    {
        RecordFaultTaskImpact(task, task.definition.computeNodeId,
                              "ARRIVAL_DURING_COMPUTE_OUTAGE");
    }
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
    if (IsTerminalTaskState(task.state))
    {
        return;
    }
    if (!IsSatelliteAvailable(task.definition.computeNodeId))
    {
        FailTaskForSatelliteNode(task,
                                 task.definition.computeNodeId,
                                 completionTimeNs,
                                 "COMPUTE_SATELLITE_UNAVAILABLE_AFTER_INPUT");
        return;
    }
    TransitionTask(task.definition.taskId,
                   TASK_QUEUED,
                   task.definition.computeNodeId,
                   completionTimeNs,
                   "INPUT_TRANSFER_COMPLETE");
    GetComputeService(task.definition.computeNodeId)
        ->SubmitTask(task.definition.taskId,
                     task.definition.computeWorkUnits,
                     completionTimeNs);
    if (!IsComputeAvailable(task.definition.computeNodeId))
    {
        RecordFaultTaskImpact(task, task.definition.computeNodeId, "QUEUED_DELAYED");
    }
}

void
TaskCoordinator::HandleComputeStart(uint64_t taskId, uint32_t nodeId, int64_t startTimeNs)
{
    const TaskRuntime& task = GetTask(taskId);
    NS_ABORT_MSG_IF(task.definition.computeNodeId != nodeId,
                    "compute-start node does not match the task definition");
    if (IsTerminalTaskState(task.state))
    {
        return;
    }
    TransitionTask(taskId, TASK_RUNNING, nodeId, startTimeNs, "COMPUTE_DISPATCH");
    if (!m_deadlineEvents.contains(taskId))
    {
        m_deadlineEvents[taskId] =
            Simulator::Schedule(NanoSeconds(task.computeDeadlineTimeNs - startTimeNs),
                                &TaskCoordinator::HandleComputeDeadline,
                                this,
                                taskId);
    }
}

void
TaskCoordinator::CancelComputeDeadline(uint64_t taskId)
{
    const auto found = m_deadlineEvents.find(taskId);
    if (found != m_deadlineEvents.end() && found->second.IsPending())
    {
        Simulator::Cancel(found->second);
    }
}

void
TaskCoordinator::HandleComputeDeadline(uint64_t taskId)
{
    TaskRuntime& task = GetTask(taskId);
    if (IsTerminalTaskState(task.state) || task.computeCompleteTimeNs >= 0)
        return;
    if (GetComputeService(task.definition.computeNodeId)->CompleteTaskIfDue(taskId))
        return;
    FailTaskForComputeNode(task,
                           Simulator::Now().GetNanoSeconds(),
                           "COMPUTE_DEADLINE_EXCEEDED",
                           TaskFailureReason::COMPUTE_DEADLINE_EXCEEDED);
}

void
TaskCoordinator::DoDispose()
{
    for (const auto& [taskId, event] : m_deadlineEvents)
        CancelComputeDeadline(taskId);
    m_deadlineEvents.clear();
    Object::DoDispose();
}

void
TaskCoordinator::HandleComputeComplete(uint64_t taskId,
                                       uint32_t nodeId,
                                       int64_t completionTimeNs)
{
    TaskRuntime& task = GetTask(taskId);
    NS_ABORT_MSG_IF(task.definition.computeNodeId != nodeId,
                    "compute-completion node does not match the task definition");
    if (IsTerminalTaskState(task.state))
    {
        return;
    }
    NS_ABORT_MSG_IF(completionTimeNs > task.computeDeadlineTimeNs,
                    "compute completion occurred after deadline");
    CancelComputeDeadline(taskId);
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
    if (IsTerminalTaskState(task.state))
    {
        return;
    }
    NS_ABORT_MSG_IF(!task.ComputeDeadlineMet(), "RESULT cannot complete without on-time compute");
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
    return m_transferEngine->AreAllTransfersCompleted(false) &&
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
    NS_ABORT_MSG_IF(!m_transferEngine->AreAllTransfersCompleted(false),
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

const std::vector<FaultTaskImpactRecord>&
TaskCoordinator::GetFaultTaskImpacts() const
{
    return m_faultTaskImpacts;
}

void
TaskCoordinator::RecordFaultTaskImpact(const TaskRuntime& task,
                                       uint32_t faultNodeId,
                                       const std::string& impactType)
{
    const auto active = m_activeFaults.find(faultNodeId);
    if (active == m_activeFaults.end() || active->second.faultId == 0)
    {
        return;
    }
    const auto& fault = active->second;
    const auto duplicate = std::find_if(m_faultTaskImpacts.begin(), m_faultTaskImpacts.end(),
        [&](const auto& record) {
            return record.fault.faultId == fault.faultId &&
                   record.taskId == task.definition.taskId && record.impactType == impactType;
        });
    if (duplicate != m_faultTaskImpacts.end())
    {
        return;
    }
    FaultTaskImpactRecord record;
    record.fault = fault;
    record.taskId = task.definition.taskId;
    record.impactTimeNs = Simulator::Now().GetNanoSeconds();
    record.stateBeforeImpact = task.state;
    record.impactType = impactType;
    record.computeStartTimeNs = task.computeStartTimeNs;
    record.deadlineTimeNs = task.computeDeadlineTimeNs;
    if (task.state == TASK_RUNNING)
    {
        const auto service = GetComputeService(task.definition.computeNodeId);
        const auto snapshot = service->GetRunningTaskSnapshot();
        NS_ABORT_MSG_IF(!snapshot || snapshot->taskId != task.definition.taskId,
                        "fault impact lost its running compute snapshot");
        const unsigned __int128 work =
            static_cast<unsigned __int128>(service->GetComputeRateWorkUnitsPerSecond()) *
            static_cast<uint64_t>(snapshot->elapsedTimeNs) / 1000000000u;
        record.progressValid = true;
        record.completedWorkUnits = static_cast<uint64_t>(
            std::min(work, static_cast<unsigned __int128>(task.definition.computeWorkUnits)));
        record.remainingWorkUnits = task.definition.computeWorkUnits - record.completedWorkUnits;
    }
    m_faultTaskImpacts.push_back(record);
}

TaskFaultImpact
TaskCoordinator::FailTaskForComputeNode(TaskRuntime& task,
                                        int64_t eventTimeNs,
                                        const std::string& cause,
                                        TaskFailureReason reason)
{
    TaskFaultImpact impact;
    if (IsTerminalTaskState(task.state))
    {
        return impact;
    }
    const TaskState fromState = task.state;
    NS_ABORT_MSG_IF(fromState == TASK_RESULT_TRANSFERRING,
                    "compute-only failure cannot fail a result-transferring task");
    Ptr<ComputeService> service = GetComputeService(task.definition.computeNodeId);
    if (fromState == TASK_QUEUED)
    {
        NS_ABORT_MSG_IF(!service->RemoveQueuedTaskForFailure(task.definition.taskId),
                        "queued task was absent from ComputeService");
    }
    else if (fromState == TASK_RUNNING)
    {
        NS_ABORT_MSG_IF(!service->CancelRunningTaskForFailure(task.definition.taskId),
                        "running task was absent from ComputeService");
    }

    CancelComputeDeadline(task.definition.taskId);
    NS_ABORT_MSG_IF(!task.FailIfActive(eventTimeNs, reason, cause),
                    "active task could not enter TASK_FAILED");
    m_taskEvents.push_back({eventTimeNs,
                            task.definition.taskId,
                            fromState,
                            TASK_FAILED,
                            task.definition.computeNodeId,
                            cause});
    m_taskTransition(m_taskEvents.back());
    impact.affectedTaskCount = 1;

    if (fromState == TASK_PENDING || fromState == TASK_INPUT_TRANSFERRING)
    {
        impact.affectedTransferCount +=
            m_transferEngine->FinalizeTransferIfActive(
                task.definition.inputTransferId,
                TransferTerminalState::CANCELLED,
                TransferTerminalReason::TASK_FAILED)
                ? 1
                : 0;
    }
    impact.affectedTransferCount +=
        m_transferEngine->FinalizeTransferIfActive(
            task.definition.resultTransferId,
            TransferTerminalState::CANCELLED,
            TransferTerminalReason::TASK_FAILED)
            ? 1
            : 0;
    return impact;
}

TaskFaultImpact
TaskCoordinator::FailTaskForSatelliteNode(TaskRuntime& task,
                                          uint32_t failedNodeId,
                                          int64_t eventTimeNs,
                                          const std::string& cause)
{
    TaskFaultImpact impact;
    if (IsTerminalTaskState(task.state))
    {
        return impact;
    }

    const bool isSource = task.definition.sourceNodeId == failedNodeId;
    const bool isCompute = task.definition.computeNodeId == failedNodeId;
    const bool isResult = task.definition.resultNodeId == failedNodeId;
    const TaskState fromState = task.state;
    bool affected = false;
    switch (fromState)
    {
    case TASK_PENDING:
    case TASK_INPUT_TRANSFERRING:
        affected = isSource || isCompute || isResult;
        break;
    case TASK_QUEUED:
    case TASK_RUNNING:
    case TASK_RESULT_TRANSFERRING:
        affected = isCompute || isResult;
        break;
    case TASK_COMPLETED:
    case TASK_FAILED:
        break;
    }
    if (!affected)
    {
        return impact;
    }

    const std::string permanentImpact =
        fromState == TASK_RUNNING ? "RUNNING_INTERRUPTED_PERMANENT" :
        fromState == TASK_QUEUED ? "QUEUED_INVALIDATED_PERMANENT" :
        fromState == TASK_INPUT_TRANSFERRING ? "INPUT_TRANSFER_ABORTED_PERMANENT" :
        fromState == TASK_RESULT_TRANSFERRING ? "RESULT_TRANSFER_ABORTED_PERMANENT" :
        "ENDPOINT_UNAVAILABLE_PERMANENT";
    RecordFaultTaskImpact(task, failedNodeId, permanentImpact);

    if (fromState == TASK_QUEUED)
    {
        NS_ABORT_MSG_IF(
            !GetComputeService(task.definition.computeNodeId)
                 ->RemoveQueuedTaskForFailure(task.definition.taskId),
            "satellite fault queued task was absent from ComputeService");
    }
    else if (fromState == TASK_RUNNING)
    {
        NS_ABORT_MSG_IF(
            !GetComputeService(task.definition.computeNodeId)
                 ->CancelRunningTaskForFailure(task.definition.taskId),
            "satellite fault running task was absent from ComputeService");
    }

    TaskFailureReason failureReason = TaskFailureReason::RESULT_SATELLITE_FAILURE;
    if ((fromState == TASK_PENDING || fromState == TASK_INPUT_TRANSFERRING) && isSource)
    {
        failureReason = TaskFailureReason::SOURCE_SATELLITE_FAILURE;
    }
    else if (isCompute)
    {
        failureReason = TaskFailureReason::COMPUTE_SATELLITE_FAILURE;
    }
    CancelComputeDeadline(task.definition.taskId);
    NS_ABORT_MSG_IF(!task.FailIfActive(eventTimeNs, failureReason, cause),
                    "active task could not enter satellite TASK_FAILED");
    m_taskEvents.push_back({eventTimeNs,
                            task.definition.taskId,
                            fromState,
                            TASK_FAILED,
                            failedNodeId,
                            cause});
    m_taskTransition(m_taskEvents.back());
    impact.affectedTaskCount = 1;

    const auto finalize = [this, &impact](uint64_t transferId,
                                          TransferTerminalState state,
                                          TransferTerminalReason reason) {
        if (m_transferEngine->FinalizeTransferIfActive(transferId, state, reason))
        {
            ++impact.affectedTransferCount;
        }
    };
    switch (fromState)
    {
    case TASK_PENDING:
        finalize(task.definition.inputTransferId,
                 TransferTerminalState::CANCELLED,
                 TransferTerminalReason::TASK_FAILED);
        finalize(task.definition.resultTransferId,
                 TransferTerminalState::CANCELLED,
                 TransferTerminalReason::TASK_FAILED);
        break;
    case TASK_INPUT_TRANSFERRING:
        if (isSource)
        {
            finalize(task.definition.inputTransferId,
                     TransferTerminalState::FAILED,
                     TransferTerminalReason::SOURCE_SATELLITE_FAILED);
        }
        else if (isCompute)
        {
            finalize(task.definition.inputTransferId,
                     TransferTerminalState::FAILED,
                     TransferTerminalReason::DESTINATION_SATELLITE_FAILED);
        }
        else
        {
            finalize(task.definition.inputTransferId,
                     TransferTerminalState::CANCELLED,
                     TransferTerminalReason::TASK_FAILED);
        }
        finalize(task.definition.resultTransferId,
                 TransferTerminalState::CANCELLED,
                 TransferTerminalReason::TASK_FAILED);
        break;
    case TASK_QUEUED:
    case TASK_RUNNING:
        finalize(task.definition.resultTransferId,
                 TransferTerminalState::CANCELLED,
                 TransferTerminalReason::TASK_FAILED);
        break;
    case TASK_RESULT_TRANSFERRING:
        finalize(task.definition.resultTransferId,
                 TransferTerminalState::FAILED,
                 isCompute ? TransferTerminalReason::SOURCE_SATELLITE_FAILED
                           : TransferTerminalReason::DESTINATION_SATELLITE_FAILED);
        break;
    case TASK_COMPLETED:
    case TASK_FAILED:
        NS_ABORT_MSG("terminal task reached satellite failure cleanup");
    }
    return impact;
}

std::map<uint32_t, TaskFaultImpact>
TaskCoordinator::ApplyComputeFaultBatch(
    const std::vector<uint32_t>& recoveredNodeIds,
    const std::vector<uint32_t>& startedNodeIds)
{
    std::vector<TaskFaultNodeChange> recovered;
    recovered.reserve(recoveredNodeIds.size());
    for (const uint32_t nodeId : recoveredNodeIds)
    {
        recovered.push_back({nodeId, TaskFaultKind::COMPUTE});
    }
    std::vector<TaskFaultNodeChange> started;
    started.reserve(startedNodeIds.size());
    for (const uint32_t nodeId : startedNodeIds)
    {
        started.push_back({nodeId, TaskFaultKind::COMPUTE});
    }
    return ApplyFaultBatch(recovered, started);
}

std::map<uint32_t, TaskFaultImpact>
TaskCoordinator::ApplyFaultBatch(
    const std::vector<TaskFaultNodeChange>& recoveredNodes,
    const std::vector<TaskFaultNodeChange>& startedNodes)
{
    NS_ABORT_MSG_IF(!m_initialized,
                    "TaskCoordinator is not initialized for fault execution");
    std::set<std::pair<uint32_t, TaskFaultKind>> recoveredKeys;
    for (const TaskFaultNodeChange& change : recoveredNodes)
    {
        m_activeFaults.erase(change.nodeId);
        NS_ABORT_MSG_IF(!recoveredKeys.emplace(change.nodeId, change.kind).second,
                        "fault recovery batch contains a duplicate node and kind");
        if (change.kind == TaskFaultKind::SATELLITE)
        {
            NS_ABORT_MSG_IF(m_unavailableSatelliteNodes.erase(change.nodeId) != 1,
                            "satellite recovery did not match task availability state");
            Ptr<ComputeService> service = FindComputeService(change.nodeId);
            if (service != nullptr)
            {
                service->SetComputeAvailable(true);
            }
        }
        else
        {
            GetComputeService(change.nodeId)->SetComputeAvailable(true);
        }
    }

    std::set<std::pair<uint32_t, TaskFaultKind>> startedKeys;
    std::map<uint32_t, TaskFaultImpact> impacts;
    for (const TaskFaultNodeChange& change : startedNodes)
    {
        m_activeFaults[change.nodeId] = change.fault;
        NS_ABORT_MSG_IF(!startedKeys.emplace(change.nodeId, change.kind).second,
                        "fault start batch contains a duplicate node and kind");
        NS_ABORT_MSG_IF(impacts.contains(change.nodeId),
                        "fault start batch contains two types on one node");
        impacts.emplace(change.nodeId, TaskFaultImpact{});
        if (change.kind == TaskFaultKind::SATELLITE)
        {
            NS_ABORT_MSG_IF(!m_unavailableSatelliteNodes.insert(change.nodeId).second,
                            "satellite start overlaps task availability state");
            Ptr<ComputeService> service = FindComputeService(change.nodeId);
            if (service != nullptr)
            {
                service->SetComputeAvailable(false);
            }
        }
        else
        {
            GetComputeService(change.nodeId)->SetComputeAvailable(false);
        }
    }

    const int64_t eventTimeNs = Simulator::Now().GetNanoSeconds();
    std::set<uint32_t> satelliteStarts;
    for (const TaskFaultNodeChange& change : startedNodes)
    {
        if (change.kind == TaskFaultKind::SATELLITE)
        {
            satelliteStarts.insert(change.nodeId);
        }
    }
    for (TaskRuntime& task : m_tasks)
    {
        if (IsTerminalTaskState(task.state) ||
            (task.state == TASK_PENDING &&
             task.definition.arrivalTimeNs != eventTimeNs))
        {
            continue;
        }
        std::optional<uint32_t> failedNodeId;
        switch (task.state)
        {
        case TASK_PENDING:
            if (satelliteStarts.contains(task.definition.sourceNodeId))
            {
                failedNodeId = task.definition.sourceNodeId;
            }
            else if (satelliteStarts.contains(task.definition.computeNodeId))
            {
                failedNodeId = task.definition.computeNodeId;
            }
            else if (satelliteStarts.contains(task.definition.resultNodeId))
            {
                failedNodeId = task.definition.resultNodeId;
            }
            break;
        case TASK_INPUT_TRANSFERRING:
            if (satelliteStarts.contains(task.definition.sourceNodeId))
            {
                failedNodeId = task.definition.sourceNodeId;
            }
            else if (satelliteStarts.contains(task.definition.computeNodeId))
            {
                failedNodeId = task.definition.computeNodeId;
            }
            else if (satelliteStarts.contains(task.definition.resultNodeId))
            {
                failedNodeId = task.definition.resultNodeId;
            }
            break;
        case TASK_QUEUED:
        case TASK_RUNNING:
        case TASK_RESULT_TRANSFERRING:
            if (satelliteStarts.contains(task.definition.computeNodeId))
            {
                failedNodeId = task.definition.computeNodeId;
            }
            else if (satelliteStarts.contains(task.definition.resultNodeId))
            {
                failedNodeId = task.definition.resultNodeId;
            }
            break;
        case TASK_COMPLETED:
        case TASK_FAILED:
            break;
        }
        if (failedNodeId.has_value())
        {
            const TaskFaultImpact taskImpact =
                FailTaskForSatelliteNode(task,
                                         failedNodeId.value(),
                                         eventTimeNs,
                                         "SATELLITE_FAULT_START");
            TaskFaultImpact& nodeImpact = impacts.at(failedNodeId.value());
            nodeImpact.affectedTaskCount += taskImpact.affectedTaskCount;
            nodeImpact.affectedTransferCount += taskImpact.affectedTransferCount;
        }
    }

    for (const TaskFaultNodeChange& change : startedNodes)
    {
        if (change.kind != TaskFaultKind::COMPUTE)
        {
            continue;
        }
        TaskFaultImpact& nodeImpact = impacts.at(change.nodeId);
        for (TaskRuntime& task : m_tasks)
        {
            if (IsTerminalTaskState(task.state) ||
                task.definition.computeNodeId != change.nodeId ||
                task.state == TASK_RESULT_TRANSFERRING ||
                (task.state == TASK_PENDING && task.definition.arrivalTimeNs != eventTimeNs))
            {
                continue;
            }
            const std::string impactType =
                task.state == TASK_RUNNING ? "RUNNING_INTERRUPTED" :
                task.state == TASK_QUEUED ? "QUEUED_DELAYED" :
                task.state == TASK_INPUT_TRANSFERRING ? "INPUT_CONTINUES_DURING_COMPUTE_OUTAGE" :
                "ARRIVAL_DURING_COMPUTE_OUTAGE";
            RecordFaultTaskImpact(task, change.nodeId, impactType);
            if (task.state != TASK_RUNNING)
            {
                continue;
            }
            const TaskFaultImpact taskImpact =
                FailTaskForComputeNode(task, eventTimeNs, "COMPUTE_FAULT_START");
            nodeImpact.affectedTaskCount += taskImpact.affectedTaskCount;
            nodeImpact.affectedTransferCount += taskImpact.affectedTransferCount;
        }
    }
    return impacts;
}

bool
TaskCoordinator::IsComputeAvailable(uint32_t nodeId) const
{
    NS_ABORT_MSG_IF(!m_initialized,
                    "TaskCoordinator is not initialized for compute availability");
    return GetComputeService(nodeId)->IsComputeAvailable();
}

bool
TaskCoordinator::IsSatelliteAvailable(uint32_t nodeId) const
{
    NS_ABORT_MSG_IF(!m_initialized,
                    "TaskCoordinator is not initialized for satellite availability");
    return !m_unavailableSatelliteNodes.contains(nodeId);
}

} // namespace ns3
