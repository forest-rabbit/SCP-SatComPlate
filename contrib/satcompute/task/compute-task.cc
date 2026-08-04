/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Keep the task lifecycle linear and timestamp-monotonic.

#include "compute-task.h"

#include "ns3/abort.h"

namespace ns3
{

namespace
{

bool
IsNextTaskState(TaskState currentState, TaskState requestedState)
{
    switch (currentState)
    {
    case TASK_PENDING:
        return requestedState == TASK_INPUT_TRANSFERRING;
    case TASK_INPUT_TRANSFERRING:
        return requestedState == TASK_QUEUED;
    case TASK_QUEUED:
        return requestedState == TASK_RUNNING;
    case TASK_RUNNING:
        return requestedState == TASK_RESULT_TRANSFERRING;
    case TASK_RESULT_TRANSFERRING:
        return requestedState == TASK_COMPLETED;
    case TASK_COMPLETED:
        return false;
    }
    return false;
}

} // namespace

const char*
TaskStateToString(TaskState state)
{
    switch (state)
    {
    case TASK_PENDING:
        return "PENDING";
    case TASK_INPUT_TRANSFERRING:
        return "INPUT_TRANSFERRING";
    case TASK_QUEUED:
        return "QUEUED";
    case TASK_RUNNING:
        return "RUNNING";
    case TASK_RESULT_TRANSFERRING:
        return "RESULT_TRANSFERRING";
    case TASK_COMPLETED:
        return "COMPLETED";
    }
    return "UNKNOWN";
}

TaskRuntime::TaskRuntime(const TaskDefinition& taskDefinition)
    : definition(taskDefinition)
{
    NS_ABORT_MSG_IF(definition.taskId == 0, "TaskRuntime requires a positive task ID");
}

void
TaskRuntime::TransitionTo(TaskState requestedState,
                          int64_t eventTimeNs,
                          const std::string& cause)
{
    NS_ABORT_MSG_IF(!IsNextTaskState(state, requestedState),
                    "invalid task-state transition for task_id=" << definition.taskId);
    NS_ABORT_MSG_IF(eventTimeNs < 0 ||
                        (lastTransitionTimeNs >= 0 && eventTimeNs < lastTransitionTimeNs),
                    "task-state transition time is not monotonic for task_id="
                        << definition.taskId);
    NS_ABORT_MSG_IF(state == TASK_PENDING && eventTimeNs != definition.arrivalTimeNs,
                    "first task transition must equal arrival_time_ns for task_id="
                        << definition.taskId);
    NS_ABORT_MSG_IF(cause.empty(), "task-state transition cause must not be empty");

    switch (requestedState)
    {
    case TASK_INPUT_TRANSFERRING:
        break;
    case TASK_QUEUED:
        inputTransferCompleteTimeNs = eventTimeNs;
        queueEnterTimeNs = eventTimeNs;
        break;
    case TASK_RUNNING:
        computeStartTimeNs = eventTimeNs;
        break;
    case TASK_RESULT_TRANSFERRING:
        computeCompleteTimeNs = eventTimeNs;
        resultTransferStartTimeNs = eventTimeNs;
        break;
    case TASK_COMPLETED:
        resultTransferCompleteTimeNs = eventTimeNs;
        break;
    case TASK_PENDING:
        NS_ABORT_MSG("a task cannot transition back to PENDING");
    }
    state = requestedState;
    lastTransitionTimeNs = eventTimeNs;
}

} // namespace ns3
